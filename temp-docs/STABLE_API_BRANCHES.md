# STABLE_API_BRANCHES.md — две ветки решения: со Stable API и без

Новый документ (старые не правятся). Фиксирует уже принятые решения, разделяет оставшиеся вопросы на две ветки и показывает, **что каждая ветка даёт в итоге**.

Дополняет: `REPORT.md` (что менять по файлам), `STABLE_ABI.md` (детали abi3), `ARCHITECTURE_QUESTIONS.md` (полные формулировки вопросов), `MIGRATION_CHECKLIST.md` (пофайловый чек-лист).

---

## 0. Зафиксированные решения

| Решение | Статус | Следствия |
|---|---|---|
| **Релиз big-bang** | принято | одна ветка/один релиз, без гибридного периода pybind11+nanobind; фазы работ — внутри одной ветки (см. §7) |
| **pybind11 убираем полностью** | принято | удаляются CPM-пакет pybind11, `pybind11_add_module`, `pybind11::pybind11/module/embed`, все `#include <pybind11/...>` и `py::`; в `cmake/desbordante_helpers.cmake` и `pyproject.toml` упоминаний pybind11 не остаётся |
| **«По хорошему ничего не должен ломать»** | цель | Python-API сохраняем; известные поведенческие отличия nanobind перечислены и проверяются тестами (§8) |
| **Q10 «big-bang или поэтапно»** | закрыт big-bang'ом | вопрос Q10 из `ARCHITECTURE_QUESTIONS.md` снимается (остаётся только назначить владельца процесса пререлизов) |

Обе ветки ниже **начинаются одинаково**: полная замена pybind11 на nanobind во всех ~106 файлах биндингов. Различие — только в режиме сборки/упаковки и в одном файле, зависящем от limited API.

---

## 1. Что одинаково в обеих ветках (общий объём работ)

Это важно для оценки: выбор «со Stable API / без» **не удваивает** работу.

1. **Замена API в биндингах** (общая для обеих веток): `py::` → `nb::`, 115 × `obj.cast<T>()` → `nb::cast<T>(obj)`, 22 × `py::pickle` → `__getstate__`/`__setstate__`, `py::class_<T, std::shared_ptr<T>>` → `nb::class_<T>`, `py::init`-лямбды → placement new, `py::typing::Callable` → `nb::typed<nb::callable, ...>`, `py::arg_v` → `nb::arg(...) = value`, `py::enum_`, `py::self`, `make_iterator`, кастомный `type_caster`.
2. **Точка входа**: `PYBIND11_MODULE` → `NB_MODULE`; имя модуля = имени цели `nanobind_add_module`.
3. **Логирование** (`py_util/logging.cpp`): переписывается от суб-интерпретаторов к одному process-wide sink'у. **Это требование nanobind, а не abi3** — в ветке 2 оно тоже обязательно (nanobind не поддерживает мультиинтерпретаторы вообще).
4. **`Pyro`/`PyroUCC`**: у nanobind нет `py::multiple_inheritance()`; проверка/обход нужны в обеих ветках (риск R2 из `REPORT.md`).
5. **`get_py_type.cpp`**: `py::type::of` → `nb::type<T>()` в обеих ветках. Различие только в судьбе `&Py*_Type` (см. §3/§4).
6. **`statistics/bind_statistics.cpp`**: кастомный caster переписывается в обеих ветках (это API nanobind, а не abi3).
7. **CMake/CI/доки**: убирается pybind11, добавляется nanobind-цель, обновляются README; конкретика различается между ветками (см. таблицу §5).

**Вывод:** ~90–95 % работ одинаковы; ветка отличается сборочными файлами, CI, `requires-python` и одним файлом (`get_py_type.cpp`).

---

## 2. Ветка 1 — хотим Stable API (abi3)

### 2.1 Что делаем

```cmake
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module
             OPTIONAL_COMPONENTS Development.SABIModule)   # CMake >= 3.26
set(NANOBIND_LIB nanobind-static-abi3)
nanobind_build_library(${NANOBIND_LIB})
nanobind_add_module(desbordante STABLE_ABI NB_STATIC bind_main_classes.cpp bindings.cpp)
```

```toml
[build-system]
requires = ["scikit-build-core", "nanobind>=3.1", "cmake>=3.26"]
[project]
requires-python = ">=3.12"           # либо >=3.10 при split mode
[tool.scikit-build]
wheel.py-api = "cp312"               # либо "cp310" при split mode
```

* `get_py_type.cpp`: `&PyBool_Type`…`&PyFunction_Type` заменяются на рантайм-получение типов из `builtins`/`types` (в limited API `PyFunction_Type` недоступен).
* `CMakeLists.txt` корня: ≥ 3.26; опция `DESBORDANTE_BINDINGS_STABLE_ABI` (если хочется уметь выключать).
* CI: сборка на минимальной версии (`CIBW_BUILD: "cp312-*"`), `abi3audit` автоматически, шаг проверки `.abi3.` в имени модуля.
* Free-threading: в linked-режиме невозможен (`STABLE_ABI` игнорируется на FT-интерпретаторе); при необходимости — только split mode + `abi3t` (Python 3.15t+).

### 2.2 Что получаем в итоге

* **Одно колесо на платформу** (`cp312-abi3-…`), которое устанавливается на все GIL-сборки CPython ≥ 3.12: 3.13, 3.14, 3.15, … — **без пересборки и без нового релиза**.
* Матрица CI: вместо «N версий Python × M платформ» — «M платформ» (плюс, при желании, один прогон на пререлизе).
* Релизный процесс: выход нового CPython больше не требует «бампать» колёса; достаточно прогнать тесты на пререлизе.
* Требования к сборке выше: CMake ≥ 3.26, `Python::SABIModule`, `wheel.py-api`, floor 3.12 (или split mode для 3.10+).
* Ограничения: нельзя использовать не-limited API CPython (в проекте это только `get_py_type.cpp`); в linked-режиме нет free-threading; возможен небольшой регресс производительности биндингов.
* Если 3.10/3.11 обязательны — split mode: то же самое, но floor 3.10, появляется runtime-зависимость `nanobind-backend` и теряются musllinux/PyPy-бэкенды.

### 2.3 Что ещё нужно решить в этой ветке (вопросы владельцам)

| Вопрос | Варианты | Рекомендация |
|---|---|---|
| Минимальная версия Python | 3.12+ (linked) / 3.10+ (split) / гибрид | 3.12+ (3.10 EOL, 3.11 — единственная жертва) |
| Linked или split | linked (проще, без зависимостей) / split (floor 3.10, abi3t) | linked |
| Free-threaded | отказ / FT без abi3 (отдельные колёса `cp313t`) / abi3t (split + 3.15t) | отказ на этом этапе |
| Готовы ли отказаться от суб-интерпретаторов | да / нет (тогда abi3 невозможен) | да (иначе ветка 1 закрывается) |

Всё остальное (big-bang, pickle 1:1, enum-отличия, LTO по результатам бенчмарков) — общее с веткой 2.

---

## 3. Ветка 2 — без Stable API

### 3.1 Что делаем

```cmake
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)  # CMake 3.25 достаточно
nanobind_add_module(desbordante NB_STATIC bind_main_classes.cpp bindings.cpp) # без STABLE_ABI
# при желании: FREE_THREADED
```

```toml
[build-system]
requires = ["scikit-build-core", "nanobind>=3.1", "cmake>=3.25"]
[project]
requires-python = ">=3.10"           # можно сохранить как есть
# wheel.py-api НЕ нужен
```

* `get_py_type.cpp`: можно **оставить** статические `&PyBool_Type`, `&PyLong_Type`, …, `&PyFunction_Type` — ограничений limited API нет. (Меняется только `py::type::of` → `nb::type<T>()`, `py::tuple` → `nb::tuple` и т. д.)
* CMake остаётся 3.25; `Development.SABIModule` не нужен.
* CI почти не меняется: та же per-version матрица, только вместо сборки pybind11-модуля — nanobind-модуль.
* Free-threading: доступен через `FREE_THREADED` (nanobind 2.2+, Python 3.13+), но требует аудита кода и отдельных колёс `cp313t`/`cp314t`.

### 3.2 Что получаем в итоге

1. **pybind11 полностью удалён**, проект на nanobind 3.1.0.
2. **Выигрыш nanobind как такового** (заявленные upstream бенчмарки): компиляция в ~2,7–4,4 раза быстрее, бинарник в ~3–5 раз меньше, накладные расходы на переход Python↔C++ ниже; меньше заголовочного кода, убраны holder'ы и связанные с ними крайние случаи владения.
3. **Матрица колёс и релизный процесс — как сейчас**: новое колесо под каждую версию Python (`cp310`…`cp315`), выход нового CPython = пересборка + релиз. Того эффекта «старое колесо работает на новом Python» **нет**.
4. **Ограничения abi3 не действуют**: `Py*_Type` можно оставить; CMake 3.25; `requires-python >= 3.10`; нет `abi3audit`; не нужен `wheel.py-api`; нет ограничения «3.12+»; free-threaded доступен без split mode.
5. **Что НЕ отпадает** (важно, чтобы не было ложного ощущения «ветка 2 сильно проще»):
   * суб-интерпретаторы всё равно недоступны — `logging.cpp` переписывается;
   * `py::pickle` → ручные `__getstate__`/`__setstate__` (22 места);
   * holder'ы `py::class_<T, std::shared_ptr<T>>` → `nb::class_<T>`;
   * `py::multiple_inheritance()` → проверка `Pyro`/`PyroUCC`;
   * кастомный caster в `statistics` переписывается;
   * enum/None-отличия nanobind остаются;
   * LTO/`-Os` и бенчмарки — та же история.
6. **Если free-threading нужен** — это единственная ветка, где он достижим относительно просто: `FREE_THREADED`, отдельные колёса, аудит потокобезопасности. В ветке 1 (linked abi3) его нет вовсе, в split — только `abi3t` на 3.15t+.

### 3.3 Что ещё нужно решить в этой ветке

| Вопрос | Варианты | Рекомендация |
|---|---|---|
| Оставляем ли 3.10 | да (как сейчас) / нет (3.11+) | решить отдельно; на сборку не влияет |
| Free-threaded | отказ / поддержка (отдельные колёса) | отказ на этом этапе, вернуться после аудита |
| Целевое время перехода на abi3 в будущем | никогда / в следующем релизе / «код abi3-ready» | «код abi3-ready»: сразу заменить `get_py_type.cpp` на рантайм-типы (цена — один файл), чтобы будущий переезд в ветку 1 не трогал C++ |
| Суб-интерпретаторы | отказ / фиксация ограничения | отказ (иначе и эта ветка не спасает) |

---

## 4. Прямое сравнение: что даёт каждая ветка

| Критерий | Ветка 1: со Stable API | Ветка 2: без Stable API |
|---|---|---|
| pybind11 удалён | да | да |
| NanoBind-выигрыш (скорость сборки/размер/overhead) | да | да |
| Новый CPython без пересборки и релиза | **да**, для GIL-сборок ≥ 3.12 | нет: нужно новое колесо на каждую версию |
| Колёс на платформу | 1 (для GIL ≥ 3.12) | по одному на каждую версию Python |
| Матрица CI | сокращается до «платформа × 1 версия» | как сейчас |
| Минимальный Python | 3.12 (linked) / 3.10 (split) | 3.10+ (как сейчас) |
| CMake | ≥ 3.26 + `Development.SABIModule` | 3.25 достаточно |
| `wheel.py-api`, `abi3audit`, `.abi3.so` | обязательны | не нужны |
| `get_py_type.cpp` (`Py*_Type`) | переписать на рантайм-типы (1 файл) | можно оставить как есть |
| Free-threaded | нет (linked); только `abi3t` в split mode, Python 3.15t+ | да (`FREE_THREADED`), при условии аудита |
| Runtime-зависимости | нет (linked) / `nanobind-backend` (split) | нет |
| Регресс производительности биндингов | возможен (limited API) | нет дополнительного |
| Суб-интерпретаторы | недоступны | недоступны |
| Pickle/enum/None/`Pyro`/caster | одинаково | одинаково |
| Дополнительная работа относительно ветки 2 | ~1 файл кода (`get_py_type.cpp`) + CMake/pyproject/CI | — |

**Ключевой вывод:** ветка 1 добавляет небольшую порцию работы (один файл + сборочные настройки) и снимает основную релизную боль; ветка 2 проще в настройке, но сохраняет «бампание» матрицы и не даёт заявленного эффекта «старые версии работают на новом Python».

---

## 5. Как две ветки отвечают на вопросы Q1–Q10

| № | Вопрос | Ветка 1 (со Stable API) | Ветка 2 (без Stable API) |
|---|---|---|---|
| Q1 | Минимальный Python | 3.12+ (linked) или 3.10+ (split) | остаётся 3.10+ |
| Q2 | abi3 обязателен | да (это и есть ветка) | нет |
| Q3 | Free-threaded | в linked — нет; только abi3t (split, 3.15t+) | да, `FREE_THREADED` + аудит + отдельные колёса |
| Q4 | Linked или split | linked (или split ради 3.10/3.15t) | не применимо |
| Q5 | Структура модулей | один модуль, `NB_STATIC` (split mode не требует `NB_SHARED`) | один модуль, `NB_STATIC` |
| Q6 | Суб-интерпретаторы | отказ обязателен (иначе abi3 невозможен) | отказ всё равно обязателен (ограничение nanobind) |
| Q7 | Формат pickle 1:1 | да | да |
| Q8 | Breaking changes API | только от nanobind (enum/None), abi3 сам по себе API не меняет | то же |
| Q9 | Производительность | дефолт + бенчмарки; abi3-оверхед возможен | дефолт + бенчмарки; abi3-оверхеда нет |
| Q10 | Big-bang/поэтапно | big-bang (зафиксировано) | big-bang (зафиксировано) |

Дополнительно к Q10 в ветке 1: процесс поддержки новых Python упрощается (нужен только присмотр за пререлизами и `abi3audit`), в ветке 2 — остаётся как сейчас (новый Python → новые колёса + релиз).

---

## 6. Точка ветвления в коде: где именно расходятся ветки

Ветки расходятся **только** в сборочных/упаковочных файлах и одном C++-файле:

| Файл | Ветка 1 | Ветка 2 |
|---|---|---|
| `src/python_bindings/CMakeLists.txt` | `find_package(... Development.SABIModule)`, `nanobind-static-abi3`, `nanobind_add_module(... STABLE_ABI ...)` | `nanobind_add_module(... NB_STATIC ...)` |
| `CMakeLists.txt` (корень) | `cmake_minimum_required(3.26)`, опция abi3 | `3.25` достаточно |
| `cmake/desbordante_helpers.cmake` | линковка `${NANOBIND_LIB}` = `nanobind-static-abi3` | `${NANOBIND_LIB}` = `nanobind-static` |
| `pyproject.toml` | `cmake>=3.26`, `wheel.py-api`, `requires-python` по решению | `cmake>=3.25`, без `wheel.py-api`, `requires-python` как сейчас |
| `src/python_bindings/py_util/get_py_type.cpp` | рантайм-типы из `builtins`/`types` | можно оставить `&Py*_Type` |
| `.github/workflows/wheel.yml` | `CIBW_BUILD: "cp312-*"`, проверка `.abi3.` | матрица как сейчас |
| `bindings-tests.yml` / `examples-tests.yml` | минимальная версия 3.12 | как сейчас |
| README/README_PYPI | «Stable ABI, Python 3.12+» | «nanobind, Python 3.10+» |

Все остальные правки из `MIGRATION_CHECKLIST.md` идентичны для обеих веток.

---

## 7. Big-bang: как это выглядит по шагам (общее для обеих веток)

1. **Подготовка сборки**: убрать pybind11 из CMake/`pyproject.toml`, ввести `${NANOBIND_LIB}`, перевести `desbordante_add_bind()` на nanobind-цель (по выбранной ветке — `-abi3` или обычную).
2. **Ядро**: `bindings.cpp` (`NB_MODULE`), `bind_main_classes.*`, `py_util/*` (включая `logging.cpp`, `get_py_type.cpp`, `statistics`).
3. **Массовая миграция `bind_*.cpp`**: группами (pickle/hash → class_/holders → properties/init → остальное) с компиляцией после каждой группы.
4. **Заголовки `bind_*.h`**: механически (`nanobind/nanobind.h`, `nb::module_&`).
5. **Сборка и упаковка по выбранной ветке** (CMake, `pyproject.toml`, CI, README).
6. **Тесты**: весь `test_bindings.py`, `test_data_stats.py`, примеры; плюс новые проверки совместимости (§8).
7. **Один релиз**, без промежуточного гибридного состояния.

Так как big-bang: **main должен оставаться зелёным как можно дольше**; работу удобно вести в отдельной ветке, но слияние — одним PR/релизом (переходного периода с двумя библиотеками в main нет).

---

## 8. «Ничего не должен ломать»: что проверить и что может отличаться

| Область | Ожидание | Как проверить |
|---|---|---|
| Публичный API (`load_data`, `execute`, `get_*`, `get_opts`) | без изменений | `test_bindings.py`, `test_data_stats.py`, examples |
| Исключения (`ConfigurationError` ⊂ `ValueError`) | без изменений | `assertRaises(desb.ConfigurationError)` + `assertRaises(ValueError)` |
| `pickle`/`copy` | формат состояния 1:1; старые pickle читаются новой сборкой | round-trip для `FD`, `RawCFD`, `CIND` и др.; кросс-версионный pickle-тест |
| `hash`/`__eq__` | без изменений | существующие тесты + новые на hash |
| Enum (`gdd.DistanceMetric`, `CmpOp`) | **может отличаться**: `int(member)` в nanobind не работает, нужно `.value` | grep по тестам/примерам (сейчас таких мест нет) + release notes |
| `None` в опциях (`_set_option(name, None)` — сброс в default) | сохраняем (аргумент помечен `.none()`/`= nb::none()`) | `test_bindings.py` (`_set_option(..., None)`) |
| Типы приводимых значений (int/float/enum) | nanobind строже (нет усечения float → int) | прогон тестов и примеров |
| Итераторы (`gspan`), DataFrame-путь, последовательности | без изменений | `test_bindings.py`, примеры FEM/DataFrame |
| Производительность | в пределах бюджета | бенчмарки `src/tests/benchmark` до/после; при регрессе — `LTO`/`NOMINSIZE` |

Если что-то из этого всё же отличается — это точечное отличие nanobind (не abi3), и его можно либо принять в release notes, либо эмулировать. Решение — «смотреть дальше» по факту тестов, как и договорились.

---

## 9. Итоговое резюме по веткам

**Ветка 1 (Stable API)** — «сделать один раз и забыть про пересборку»: одно abi3-колесо на платформу покрывает 3.12+ (или 3.10+ в split mode), новый CPython работает на уже выпущенном колесе. Цена: CMake 3.26 + `wheel.py-api` + правка `get_py_type.cpp`, floor 3.12 (или split mode), нет free-threading (кроме abi3t/split/3.15t).

**Ветка 2 (без Stable API)** — «убрать pybind11 и стать проще внутри»: nanobind, меньше и быстрее собирается, никаких ограничений limited API, можно free-threading. Но релизный процесс и матрица колёс остаются прежними: новый Python → новые колёса → новый релиз.

**Что общего:** big-bang, полное удаление pybind11, ~90–95 % правок кода, отказ от суб-интерпретаторов, ручной pickle, проверка `Pyro`, переписанный caster, тесты на отсутствие регрессий.

**Рекомендация:** если владельцы подтверждают, что 3.10/3.11 не обязательны и суб-интерпретаторы не используются, — ветка 1. Если нужен free-threading «сейчас» или не хочется трогать CMake/packaging, — ветка 2, но с заменой `get_py_type.cpp` на рантайм-типы («abi3-ready»), чтобы будущий переезд в ветку 1 не трогал C++.

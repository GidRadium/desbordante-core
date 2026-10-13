# STABLE_ABI.md — Python Stable ABI (abi3) для desbordante-core на nanobind

Дополнение к `REPORT.md`. Здесь — детали перехода на Python Stable ABI (limited API / abi3): требования, варианты упаковки, блокеры в коде и проверка результата.

---

## 1. Что даёт Stable ABI и почему это возможно только на nanobind

**Stable ABI (limited API)** — подмножество Python C API с гарантией бинарной совместимости между минорными версиями CPython. Расширение, собранное с `Py_LIMITED_API`, получает суффикс `.abi3.so` и тег колеса `cp312-abi3-<platform>`: **одно колесо на платформу** вместо колеса на каждую версию Python (сейчас CI собирает колёса для 3.10, 3.11, 3.12, 3.13, 3.14 × Linux/macOS × x86_64/arm64).

Ключевой факт: **в выпущенном pybind11 3.1.0 поддержки limited API нет** (нет опции `STABLE_ABI`, нет макроса `PYBIND11_USE_LIMITED_API`; соответствующий PR [#6198](https://github.com/pybind/pybind11/pull/6198) не влит). Поэтому «использовать Python Stable API» в этом проекте технически означает «перейти на nanobind»: nanobind поддерживает limited API с версии 1.3, а в 3.0 добавил split mode.

Как это включается в nanobind 3.1.0 (проверено по закешированному исходнику `cmake/cpm-cache/nanobind/58ad`):

* high-level: `nanobind_add_module(<target> STABLE_ABI ...)`;
* low-level: `nanobind_build_library(nanobind-static-abi3)` — режим определяется **суффиксом имени** (`-abi3`), плюс `nanobind_extension_abi3(<target>)` для суффикса `.abi3.so`;
* в linked-режиме библиотека nanobind компилируется с `Py_LIMITED_API=0x030C0000` (Python 3.12) и линкуется с `Python::SABIModule`; это свойство `PUBLIC`, т. е. пробрасывается во все цели, которые линкуются с `nanobind-static-abi3`;
* **важно:** если Python < 3.12, интерпретатор не CPython или не найден `Python::SABIModule`, nanobind **молча** собирает обычный модуль, а scikit-build-core при `wheel.py-api = "cp312"` всё равно проштампует колесо тегом abi3 → нерабочее колесо. Нужен явный контроль в CI.

---

## 2. Варианты упаковки

### Вариант A — linked stable ABI, floor Python 3.12 (рекомендуется)

```toml
# pyproject.toml
[build-system]
requires = ["scikit-build-core >=0.4.3", "nanobind>=3.1", "cmake>=3.26"]
build-backend = "scikit_build_core.build"

[project]
requires-python = ">=3.12"

[tool.scikit-build]
minimum-version = "0.4"
wheel.py-api = "cp312"
```

```cmake
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module
             OPTIONAL_COMPONENTS Development.SABIModule)
nanobind_add_module(desbordante STABLE_ABI NB_STATIC ...)
```

* **Плюсы:** простейший и наиболее обкатанный путь; нет runtime-зависимостей; одно колесо на платформу для 3.12–3.14.
* **Минусы:** теряются Python 3.10 и 3.11 (сейчас `requires-python = ">=3.10"`, классификаторы 3.10–3.14, `examples-tests.yml` использует 3.10).
* **Причина выбора:** проект уже собирает колёса для 3.12+; отказ от 3.10/3.11 сокращает матрицу CI примерно втрое и снимает необходимость поддерживать два режима сборки. Дополнительный аргумент: Python 3.10 объявлен EOL 2026-10-01, поэтому верхняя планка 3.11 уже не выглядит обязательной. Если 3.10/3.11 всё же обязательны — вариант B или C.

### Вариант B — split mode, floor Python 3.10

```toml
[project]
requires-python = ">=3.10"
dependencies = ["nanobind-backend>=1.0"]

[tool.scikit-build]
wheel.py-api = "cp310"
```

```cmake
nanobind_add_module(desbordante BACKEND_MODULE nanobind_backend ...)
```

* **Плюсы:** сохраняются 3.10/3.11; **одно колесо на платформу для всех версий 3.10+**; минимальный размер фронтенда.
* **Минусы/ограничения (из документации nanobind 3.0):** нужен отдельный backend (`nanobind-backend`, ставится как зависимость); не поддерживаются PyPy, Emscripten, musllinux (для них нет backend-колёс) и статически слинкованный C++ runtime; нельзя комбинировать с `NB_STATIC`/`NB_SHARED`; free-threading доступен только как `abi3t` (PEP 803) и требует Python ≥ 3.15 (сейчас проект декларирует обычный free-threading через `mod_gil_not_used()`).
* **Причина рассмотрения:** это единственный способ получить abi3-колесо, работающее на 3.10/3.11 без матрицы на каждую версию.

### Вариант C — гибрид: обычные колёса для 3.10/3.11 + abi3 для 3.12+

* Для сборок под 3.10/3.11 `wheel.py-api` отключается (например, через `-C wheel.py-api=` / переопределение конфигурации scikit-build-core), включается обычный режим nanobind (`nanobind-static` без `-abi3`).
* Для 3.12+ — как в варианте A.
* **Плюсы:** поддержка всех версий.
* **Минусы:** две конфигурации сборки, условная логика в CMake/CI, больше колёс; выше риск, что abi3-режим «не заметят» и протестируют не то.
* **Причина:** рассматривать только если 3.10/3.11 действительно нужны, а split mode неприемлем (например, требуется musllinux или статический C++ runtime).

### Матрица решений

| Критерий | A (linked, cp312) | B (split, cp310) | C (гибрид) |
|---|---|---|---|
| Минимальный Python | 3.12 | 3.10 | 3.10 |
| Колёс на платформу | 1 (3.12+) | 1 (3.10+) | 2 (3.10/3.11 + abi3) |
| Runtime-зависимость | нет | `nanobind-backend` | нет |
| musllinux | да | нет backend-колёс | да |
| Free-threading | невозможен в abi3 | только abi3t, Python ≥ 3.15 | в обычных колёсах — да |
| Сложность | низкая | средняя | высокая |

**Рекомендация:** A. Если поддержка 3.10/3.11 обязательна — B. C — запасной вариант.

---

## 3. Что конкретно меняется под каждый вариант

### 3.1 Общее (все варианты)

1. `find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS Development.SABIModule)`.
   **Причина:** `Python::SABIModule` (CMake ≥ 3.26) — обязательное условие для abi3 в linked-режиме.
2. `cmake_minimum_required(VERSION 3.26)` в корне (или выше).
   **Причина:** без 3.26 компонент `Development.SABIModule` не поддерживается, abi3 молча выключится.
3. `nanobind_add_module(${MODULE_NAME} STABLE_ABI NB_STATIC bind_main_classes.cpp bindings.cpp)`.
   **Причина:** включает `Py_LIMITED_API=0x030C0000` (linked) и суффикс `.abi3.so`.
4. Объектные библиотеки линкуются с `nanobind-static-abi3` (переменная `NANOBIND_LIB`, см. `REPORT.md` §4.1.2).
   **Причина:** `Py_LIMITED_API` — `PUBLIC`-свойство этой цели; без него OBJECT-библиотеки скомпилируются под полный API и abi3-модуль не соберётся (или соберётся с недопустимыми символами).
5. `[tool.scikit-build] wheel.py-api`.
   **Причина:** scikit-build-core транслирует значение в `SKBUILD_SABI_VERSION`, nanobind выбирает floor stable ABI, а колесо получает тег `abi3`.
6. Проверка в CI, что модуль действительно abi3: суффикс `.abi3.` в `desbordante.__file__`.
   **Причина:** nanobind молча отключает abi3 и может получиться невалидное колесо (см. §1).

### 3.2 Вариант A: дополнительно

* `requires-python = ">=3.12"`, классификаторы 3.12–3.14.
  **Причина:** linked abi3 не работает на 3.10/3.11; метаданные должны соответствовать.
* CI: `CIBW_BUILD: "cp312-*"` в `.github/workflows/wheel.yml` (в обоих job'ах генерации матрицы и, для надёжности, в `build-wheels`).
  **Причина:** у cibuildwheel нет abi3-идентификаторов сборки — abi3 получается тегом колеса, который ставит scikit-build-core по `wheel.py-api`. Поэтому собираем на минимальной версии (3.12), а полученное abi3-колесо покрывает 3.12+; cibuildwheel 4.x сам определяет abi3-колесо и запускает `abi3audit --strict`.
* `examples-tests.yml`: `python-version: "3.12"`.
  **Причина:** минимальная поддерживаемая версия.

### 3.3 Вариант B: дополнительно

* `dependencies = ["nanobind-backend>=1.0"]` в `[project]`.
  **Причина:** backend-модуль импортируется в рантайме; без него `import desbordante` падает с `ImportError`.
* `nanobind_add_module(... BACKEND_MODULE nanobind_backend ...)`, `wheel.py-api = "cp310"`.
  **Причина:** split mode всегда целится в stable ABI, floor по умолчанию 3.10; `NB_STATIC`/`NB_SHARED` запрещены.
* CI: `CIBW_BUILD: "cp310-*"`.
  **Причина:** одно колесо на платформу.
* Раздел README «Сборка»: зафиксировать отсутствие поддержки musllinux/PyPy.
  **Причина:** ограничение самого nanobind.

### 3.4 Вариант C: дополнительно

* Опция CMake `DESBORDANTE_BINDINGS_STABLE_ABI` (ON/OFF) и выбор `NANOBIND_LIB` в зависимости от неё.
  **Причина:** одна и та же кодовая база должна собираться и как обычный модуль, и как abi3.
* В `wheel.yml` — два набора job'ов: обычные для 3.10/3.11 и abi3 для 3.12; для первых `wheel.py-api` переопределяется в пустое значение.
  **Причина:** scikit-build-core применяет `wheel.py-api` ко всем сборкам, поэтому для не-abi3 версий его нужно явно сбрасывать.
* Тесты — прогонять оба типа колёс.
  **Причина:** иначе abi3-сборка останется непроверенной.

---

## 4. Блокеры Stable ABI в текущем коде

| # | Файл | Суть | Решение |
|---|---|---|---|
| B1 | `src/python_bindings/py_util/get_py_type.cpp` | `&PyFunction_Type` объявлен в `Include/cpython/funcobject.h` под `#ifndef Py_LIMITED_API` → недоступен при abi3. Остальные `Py*_Type` (`PyBool_Type`, `PyLong_Type` — в `object.h`; `PyList_Type` — в `listobject.h`) в CPython 3.12+ доступны, но зависеть от этого не стоит | получать типы в рантайме: `nb::module_::import_("builtins").attr("dict")` и т. п., `types.FunctionType` для функций. Это одинаково работает и в abi3, и в обычной сборке |
| B2 | `src/python_bindings/py_util/logging.cpp` | `PYBIND11_HAS_SUBINTERPRETER_SUPPORT`, `#include <pybind11/subinterpreter.h>`, `py::subinterpreter::current().id()`, реестр `unordered_map<InterpreterId, py::object>` | nanobind не поддерживает мультиинтерпретаторы вообще (в исходниках 3.1.0 нет ни одного упоминания). Упростить до одного logger'а; при `nb::gil_scoped_acquire` в `sink_it_` учитывать, что в 3.x guard может быть невалиден при shutdown — проверять `guard.is_valid()` |
| B3 | `src/python_bindings/bindings.cpp` | `PYBIND11_MODULE(..., pybind11::mod_gil_not_used())` декларирует поддержку free-threading | В nanobind это CMake-флаг `FREE_THREADED` (макрос `NB_FREE_THREADED`). **В linked-режиме abi3 и free-threading несовместимы**: `STABLE_ABI` игнорируется на free-threaded интерпретаторе. `abi3t` (PEP 803) — только split mode и Python ≥ 3.15 |
| B4 | `src/python_bindings/statistics/bind_statistics.cpp` | `PYBIND11_NAMESPACE::detail::type_caster`, `PYBIND11_TYPE_CASTER`, `static handle cast(...)` | переписать на `nanobind::detail::type_caster` (`NB_TYPE_CASTER`, `from_python`/`from_cpp` noexcept). Используемые функции (`PyFloat_FromDouble`, `PyLong_FromLongLong`, `PyUnicode_FromStringAndSize`, `PyBool_FromLong`) входят в Stable ABI, поэтому abi3 не ломают |
| B5 | `src/python_bindings/py_util/value_to_py.cpp` | `PyLong_FromString` | входит в Stable ABI (документация CPython это подтверждает) — блокером не является, достаточно `nb::steal` |
| B6 | `src/python_bindings/py_util/dataframe_reader.cpp`, `get_py_type.cpp` | прямой `#include <Python.h>` | не блокер; при `Py_LIMITED_API ≥ 3.11` `Python.h` не подтягивает `<stdio.h>` транзитивно — если где-то нужны `fprintf`/`stderr`, включить `<cstdio>` явно |

**Не являются блокерами:** `nb::ndarray` в проекте не используется; кастомные caster'ы и `nb::enum_` под limited API работают (ни один STL-caster nanobind не отключён под `Py_LIMITED_API`); `nb::dynamic_attr` не используется; трaмполинов (`NB_TRAMPOLINE`) в проекте нет. C-API NumPy тоже не используется (pandas подключается как обычный Python-объект через `itertuples`/`dtypes`), поэтому известные сложности связки numpy+abi3 (в т.ч. abi3t и NumPy < 2.5) проект не затрагивают.

---

## 5. Free-threading: отдельное решение

Сейчас модуль заявляет `pybind11::mod_gil_not_used()` — то есть считается безопасным для free-threaded Python 3.13+. Варианты:

1. **Отказаться от заявки на free-threading** (пометить как неподдерживаемое в release notes).
   **Причина:** linked abi3 (цель проекта) и free-threading несовместимы; кроме того, в коде есть небезопасные для no-GIL места (`md/object_similarity_measure.h`, `PyCustomMetric`, реестр логов), а `kEnablePythonMultithreading = false` уже фиксирует отказ от параллельных Python-вызовов.
2. **Сохранить free-threading без abi3**: `nanobind_add_module(... FREE_THREADED)` без `STABLE_ABI`.
   **Причина:** nanobind корректно обрабатывает free-threaded сборки (с версии 2.2), но тогда колесо собирается под конкретную free-threaded версию (`cp313t`/`cp314t`), а abi3-выигрыша нет.
3. **abi3t (PEP 803)**: только split mode + Python ≥ 3.15.
   **Причина:** в linked-режиме stable ABI для free-threaded не существует. Для abi3t CMake выставляет `Py_TARGET_ABI3T=0x030F0000`, колесо получает тег `cp315-abi3.abi3t`; отдельная матрица (`cp315t-*`) имеет смысл только после перехода на split mode и появления Python 3.15 в CI.

**Рекомендация:** на первом этапе — вариант 2/1: убрать `mod_gil_not_used`-семантику из кода и решить отдельно, нужны ли free-threaded колёса. Если нужны — не включать abi3 для этих сборок.

---

## 6. Проверка результата

Обязательный чек-лист после включения abi3:

1. **Суффикс модуля:** `python -c "import desbordante, pathlib; print(pathlib.Path(desbordante.__file__).name)"` → `desbordante.abi3.so` (macOS/Linux) или `desbordante.pyd`.
   **Причина:** единственная надёжная проверка, что `STABLE_ABI` не проигнорирован.
2. **Тег колеса:** `wheel unpack`/`pip debug --verbose` → `cp312-abi3-<platform>`; в `WHEEL`-метаданных `Tag: cp312-abi3-...`.
3. **Импорт на двух разных версиях Python** (например, 3.12 и 3.14) из одного и того же колеса.
   **Причина:** именно это преимущество abi3 и нужно проверить.
4. **Проверка отсутствия запрещённых символов:** `nm -D --defined-only desbordante.abi3.so` не должен ссылаться на `_Py*` / не-limited API; полезно сравнить с эталонной сборкой. В CI это делает `abi3audit --strict --report` — cibuildwheel 4.x запускает его автоматически для abi3-колёс. Заодно стоит смотреть `auditwheel show`: для manylinux `libstdc++.so.6`/`libgcc_s.so.1` не бандлятся, но у политики есть потолок версий символов GLIBCXX (manylinux_2_28 → 3.4.24), и сборка более новым libstdc++ приведёт к отказу аудита.
   **Причина:** линковка с non-limited символами формально возможна и приведёт к падению на другой версии Python; потолок GLIBCXX — отдельная, не связанная с abi3 причина падения wheel-джобы.
5. **Полный прогон тестов** из `src/python_bindings/test_bindings.py` и `test_data_stats.py` на abi3-колесе.
   **Причина:** ограничения limited API проявляются в рантайме (например, в кастомных caster'ах и Python-API-вызовах).
6. **Проверка отсутствия `PyFunction_Type`/`Py*_Type` в коде биндингов**: `grep -rn "&Py[A-Za-z]*_Type" src/python_bindings`.
   **Причина:** это главный источник ошибок сборки под limited API (см. B1).
7. **Проверка pickle/copy** после ручной замены `py::pickle` (см. `REPORT.md` §4.4.2, R3).
8. **Проверка словарей/исключений**: `except ValueError` для `ConfigurationError`, `None`-аргументы (`_set_option(name, None)`).
   **Причина:** nanobind строже к `None` и меняет модель enum.

---

## 7. Пример целевого CI (вариант A)

```yaml
# .github/workflows/wheel.yml (фрагмент)
env:
  CIBW_BUILD: "cp312-*"          # было: "*manylinux*" / "*macos*"; abi3-идентификаторов у cibuildwheel нет
  CIBW_ARCHS_LINUX: x86_64
  CIBW_MANYLINUX_X86_64_IMAGE: manylinux_2_28
  CIBW_TEST_REQUIRES: pytest
  CIBW_BUILD_VERBOSITY: 1
```

и шаг сборки остаётся `pypa/cibuildwheel`, но матрица теперь содержит один идентификатор на платформу (`cp312-manylinux_x86_64`, `cp312-macosx_arm64`, `cp312-macosx_x86_64`); тег `cp312-abi3-<platform>` колесу ставит scikit-build-core, а `abi3audit` запускается cibuildwheel автоматически.

В `bindings-tests.yml` добавить:

```yaml
- name: Assert stable ABI module
  run: |
    source .venv/bin/activate
    python - <<'PY'
    import pathlib, desbordante
    name = pathlib.Path(desbordante.__file__).name
    assert ".abi3." in name, f"not a stable ABI build: {name}"
    PY
```

---

## 8. Источники

* Nanobind. CMake API (`STABLE_ABI`, `STABLE_ABI_VERSION`, `nanobind_build_library` с суффиксами `-static`/`-abi3`/`-ft`, ограничения linked-режима Python ≥ 3.12) — https://nanobind.readthedocs.io/en/latest/api_cmake.html
* Nanobind. Packaging (`wheel.py-api = "cp312"`, split mode с `cp310`, cibuildwheel) — https://nanobind.readthedocs.io/en/latest/packaging.html
* Nanobind. FAQ («Can I make stable ABI extensions for pre-3.12 Python?», ABI-совместимость, `NB_MAKE_OPAQUE`) — https://nanobind.readthedocs.io/en/latest/faq.html
* Nanobind. Free-threaded Python (нет stable ABI в linked-режиме; abi3t — split mode, Python ≥ 3.15) — https://nanobind.readthedocs.io/en/latest/free_threaded.html
* Nanobind. Split mode — https://nanobind.readthedocs.io/en/latest/split_mode.html
* Nanobind 3.1.0, `cmake/nanobind-config.cmake` (PUBLIC `Py_LIMITED_API=0x030C0000`, обработка `SKBUILD_SABI_VERSION`, молчаливое отключение `STABLE_ABI`)
* CPython C API Stability / Stable ABI — https://docs.python.org/3/c-api/stable.html
* CPython 3.12 `Include/cpython/funcobject.h` (`PyFunction_Type` только вне limited API) — https://github.com/python/cpython/blob/3.12/Include/cpython/funcobject.h
* CPython 3.12 `Include/object.h` (`PyLong_Type`, `PyBool_Type` доступны) — https://github.com/python/cpython/blob/3.12/Include/object.h
* CPython C API: Integer/Float Objects (`PyLong_FromString`, `PyLong_FromLongLong`, `PyFloat_FromDouble` — Stable ABI) — https://docs.python.org/3/c-api/long.html, https://docs.python.org/3/c-api/float.html
* pybind11: открытый PR поддержки stable ABI — https://github.com/pybind/pybind11/pull/6198
* scikit-build-core, `wheel.py-api` (трансляция в `SKBUILD_SABI_VERSION` / `SKBUILD_SABI_COMPONENT` / `Py_TARGET_ABI3T`) — https://scikit-build-core.readthedocs.io/en/latest/reference/configs.html#confval-wheel.py-api
* cibuildwheel, FAQ по abi3 (нет abi3-идентификаторов; повторное использование колеса; авто-запуск `abi3audit`) — https://cibuildwheel.pypa.io/en/stable/faq/#abi3

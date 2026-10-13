# Отчёт: перевод desbordante-core с pybind11 на nanobind и на Python Stable ABI

**Статус документа:** аналитический отчёт, код проекта не изменялся.
**Дата анализа:** состояние ветки `nanobind-migration` (коммиты `02f17f0b`, `93d09e83`).
**Область анализа:** `src/python_bindings/**`, сборочные файлы, упаковка, CI, документация.
**Цель:** полный список файлов, которые нужно изменить, и описание изменений с обоснованием каждого.

---

## 0. Резюме

1. Проект уже сделал первый шаг: в `src/python_bindings/CMakeLists.txt` добавлен nanobind 3.1.0 (CPM), собран `nanobind-static`, а часть биндингов (`bindings.cpp`, `data/bind_data_types.*`) механически переведена на `nb::`. Однако модуль по-прежнему создаётся `pybind11_add_module`, а все остальные ~100 файлов биндингов используют pybind11. В `bind_main_classes.cpp` при этом оставлена синтаксическая ошибка `"\" doesn't exist!"};data` (строка 79), то есть текущая ветка не компилируется.
2. Полная миграция — это **механическая замена API примерно в 106 файлах** (54 `.cpp` + 56 `.h`; полностью переведены только `data/bind_data_types.{cpp,h}`, `bindings.cpp` и `bind_main_classes.cpp` находятся в гибридном состоянии) плюс **≈15 файлов со специальными случаями**: `py::pickle` (22 места), кастомный `type_caster`, `py::make_iterator`, `py::self`, `py::subinterpreter`, `py::enum_`, кастомные `__init__`-лямбды, `py::typing::Callable`, `py::arg_v`, `py::multiple_inheritance`.
3. **Stable ABI (abi3) реально доступен только в связке с nanobind**: в выпущенном pybind11 3.1.0 поддержки limited API нет, соответствующий PR (#6198) не влит. В nanobind это делается флагом `STABLE_ABI` в `nanobind_add_module()` (linked mode, требует CPython ≥ 3.12) либо split mode (`BACKEND_MODULE`, floor 3.10, но появляется runtime-зависимость `nanobind-backend`).
4. Главные содержательные блокеры stable ABI в текущем коде:
   - `py_util/get_py_type.cpp` использует объекты типов CPython (`&PyBool_Type`, `&PyLong_Type`, …, `&PyFunction_Type`). Часть из них в limited API 3.12+ доступна, но `PyFunction_Type` объявлен только в `Include/cpython/funcobject.h` под `#ifndef Py_LIMITED_API` и недоступен — типы нужно получать из `builtins`/`types` во время выполнения.
   - `py_util/logging.cpp` опирается на `PYBIND11_HAS_SUBINTERPRETER_SUPPORT` и `py::subinterpreter`. **nanobind не поддерживает мультиинтерпретаторы/embedding вообще** (в исходниках 3.1.0 нет ни одного упоминания subinterpreter) — многотенантный sink придётся упростить.
   - Текущий `PYBIND11_MODULE(desbordante, module, pybind11::mod_gil_not_used())` заявляет поддержку free-threading. В nanobind это делается CMake-флагом `FREE_THREADED`, но **в linked mode stable ABI и free-threading несовместимы** (abi3t — только split mode и Python ≥ 3.15).
5. Рекомендуемая стратегия: сначала завершить полную миграцию API (фазы 1–3 из §9) на текущей схеме сборки с `STABLE_ABI`, затем отдельно оформить упаковку (см. `STABLE_ABI.md`) и CI. Сопроводительный чек-лист — в `MIGRATION_CHECKLIST.md`.

---

## 1. Текущее состояние репозитория

| Что | Состояние |
|---|---|
| Ветка | `nanobind-migration` |
| pybind11 | 2.13.4, подключается через CPM в `src/python_bindings/CMakeLists.txt` |
| nanobind | 3.1.0, уже подключён через CPM (`cmake/cpm-cache/nanobind/58ad` — закешированный исходник) |
| Сборка модуля | `pybind11_add_module(desbordante bind_main_classes.cpp bindings.cpp)` |
| Специальные библиотеки | `Desbordante.bindlib.data` (data_types) и `Desbordante.bindlib.util` (py_util) — OBJECT-библиотеки; ~44 OBJECT-библиотеки `Desbordante.bind.<name>` через `desbordante_add_bind()` |
| Уже используют `nb::` | `src/python_bindings/bindings.cpp` (гибрид: `PYBIND11_MODULE` + `nb::register_module`), `src/python_bindings/bind_main_classes.cpp` (только один `};data`-хвост остался от эксперимента), `src/python_bindings/data/bind_data_types.{cpp,h}` |
| Не компилируется | Да: синтаксическая ошибка в `bind_main_classes.cpp:79`, а также гибридная схема требует, чтобы **все** типы, передаваемые между `py::`- и `nb::`-биндингами, были переведены вместе |
| Python-тесты | `src/python_bindings/test_bindings.py` (270 строк), `src/python_bindings/test_pybind_data_stats.py` (431 строка) |
| Упаковка | `pyproject.toml` + scikit-build-core, `[build-system].requires = ["scikit-build-core", "pybind11", "cmake>=3.25"]` |

Объём pybind11-кода (оценка по grep, `src/python_bindings`):

| Конструкция | Число вхождений |
|---|---|
| `#include <pybind11/pybind11.h>` | 99 файлов |
| `#include <pybind11/stl.h>` | 46 |
| `py::tuple` / `pybind11::tuple` | 165 + 5 |
| `py::class_` / `pybind11::class_` | 72 + 4 |
| `py::arg(` / `py::arg_v(` | 70 / 1 |
| `py::make_tuple` | 57 |
| `py::init` | 43 |
| `py::module_` / `pybind11::module_` | 35 + 59 |
| `py::object` / `pybind11::object` | 31 + 31 |
| `py::handle` / `pybind11::handle` | 30 + 5 |
| `py::pickle(` | 22 (16 файлов) |
| `py::hash(` | 19 (14 файлов) |
| `.cast<T>()` (метод-член, которого **нет** в nanobind) | 115 (21 файл) |
| `py::type::of<T>()` | 10 |
| `py::typing::Callable<...>` | 4 |
| `py::self` | 6 (2 файла) |
| `py::enum_` | 2 (1 файл) |
| `py::make_iterator` / `keep_alive` | 1 файл (`gspan`) |
| `py::init([]{...})` (кастомный конструктор) | 3 файла (`gdd`, `md` ×2) |
| `py::register_exception` | 1 |
| `py::subinterpreter` / `PYBIND11_HAS_SUBINTERPRETER_SUPPORT` | 1 файл (`py_util/logging.cpp`) |

---

## 2. Как устроена сборка биндингов сейчас

Цепочка сборки:

```
pyproject.toml (scikit-build-core)
  └─ CMakeLists.txt (корень, DESBORDANTE_BINDINGS=BUILD|INSTALL)
       └─ src/python_bindings/CMakeLists.txt
            ├─ find_package(Python 3.10 COMPONENTS Interpreter Development.Module)
            ├─ CPMAddPackage(pybind11@2.13.4)
            ├─ CPMAddPackage(nanobind@3.1.0) + nanobind_build_library(nanobind-static)
            ├─ pybind11_add_module(desbordante bind_main_classes.cpp bindings.cpp)
            ├─ Desbordante.bindlib.data  (OBJECT: data/bind_data_types.cpp)
            ├─ Desbordante.bindlib.util  (OBJECT: py_util/*.cpp)
            └─ desbordante_add_bind(<name> SRCS ... LIBS ...)   (OBJECT на каждый раздел API)
```

`desbordante_add_bind()` в `cmake/desbordante_helpers.cmake` добавляет к каждой OBJECT-библиотеке `pybind11::pybind11` (только заголовки/флаги) и линкует её в модуль `${MODULE_NAME}`. Модуль — **один** (`desbordante`), поэтому все типы видны друг другу.

Ключевые следствия для миграции:

* **Причина** менять `pybind11_add_module` на `nanobind_add_module`: имя цели должно совпадать с именем в `NB_MODULE()`; именно `nanobind_add_module` выставляет правильный суффикс (`desbordante.cpython-3xx-*.so` или `desbordante.abi3.so`), линкует `libnanobind`, включает `-Os`, скрытие символов, отключение stack protector и (при `STABLE_ABI`) `-DPy_LIMITED_API=0x030C0000`.
* **Причина** линковать OBJECT-библиотеки к nanobind-цели, а не к pybind11: OBJECT-библиотеки компилируются отдельно и должны получать include-пути nanobind и (для abi3) определение `Py_LIMITED_API`. В nanobind это `PUBLIC`-свойства библиотеки `nanobind-static-abi3`, поэтому линковка к ней пробрасывает и include-пути, и `Py_LIMITED_API`, и `NB_STATIC`.

---

## 3. Целевая схема сборки

```cmake
# src/python_bindings/CMakeLists.txt (эскиз)
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module
             OPTIONAL_COMPONENTS Development.SABIModule)     # нужно для abi3
CPMAddPackage(URI "gh:wjakob/nanobind@3.1.0" OPTIONS "NB_USE_SUBMODULE_DEPS OFF")
CPMAddPackage("gh:Tessil/robin-map@1.4.1")                  # уже есть
set(NANOBIND_LIB nanobind-static-abi3)                      # имя определяет abi3-режим
nanobind_build_library(${NANOBIND_LIB})                     # один раз, разделяется всеми целями
nanobind_add_module(${MODULE_NAME} STABLE_ABI NB_STATIC bind_main_classes.cpp bindings.cpp)
target_link_libraries(${MODULE_NAME} PRIVATE
    Desbordante::algos spdlog::spdlog_header_only Boost::headers
    Desbordante.bindlib.util Desbordante.bindlib.data)
```

и в `desbordante_add_bind()`:

```cmake
list(APPEND arg_LIBS ${NANOBIND_LIB} ${DESBORDANTE_PREFIX}.compile_feats)
```

**Причина** вынести имя nanobind-цели в переменную `NANOBIND_LIB`: суффикс `-abi3` — это и есть переключатель stable ABI в low-level API nanobind (`nanobind_build_library(nanobind-static-abi3)`); переменная позволяет одной правкой включать/выключать abi3 (см. `STABLE_ABI.md`, вариант C) и не даёт разъехаться именам целей в разных файлах.

---

## 4. Полный перечень изменений

### 4.1 Сборочные и упаковочные файлы

#### 4.1.1 `src/python_bindings/CMakeLists.txt`

| Строка/блок | Изменение | Причина |
|---|---|---|
| `find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)` | добавить `OPTIONAL_COMPONENTS Development.SABIModule` | Компонент `Python::SABIModule` нужен `nanobind_add_module(STABLE_ABI)`; на CMake < 3.26 его нет, поэтому `OPTIONAL` |
| `CPMAddPackage("gh:pybind/pybind11@2.13.4")` | удалить | pybind11 больше не нужен |
| `nanobind_build_library(nanobind-static)` | заменить на `nanobind_build_library(nanobind-static-abi3)` (либо на переменную `NANOBIND_LIB`) | Суффикс `-abi3` включает сборку самой библиотеки nanobind под limited API и `Py_LIMITED_API=0x030C0000` |
| `pybind11_add_module(${MODULE_NAME} bind_main_classes.cpp bindings.cpp)` | `nanobind_add_module(${MODULE_NAME} STABLE_ABI NB_STATIC bind_main_classes.cpp bindings.cpp)` | `NB_MODULE`-модуль обязан создаваться этой командой; она же ставит суффикс `.abi3.so` и флаги |
| `target_link_libraries(${MODULE_NAME} PRIVATE pybind11::module nanobind-static ...)` | убрать `pybind11::module` и `nanobind-static` (их заменит `nanobind_add_module`) | Дублирование линковки; `nanobind_add_module` сам линкует `nanobind-static-abi3` |
| `target_link_libraries(${NAME} PRIVATE nanobind-static)` (bindlib.data) | `nanobind-static-abi3` (или `${NANOBIND_LIB}`) | OBJECT-библиотека должна компилироваться с теми же флагами/`Py_LIMITED_API`, что и модуль |
| `target_link_libraries(${NAME} PRIVATE ... pybind11::pybind11 ...)` (bindlib.util) | убрать `pybind11::pybind11`; добавить `${NANOBIND_LIB}` | То же; nanobind-заголовки должны быть доступны |
| закомментированная строка `#target_link_libraries(... pybind11::embed)` | удалить комментарий | Мёртвый код, привязанный к pybind11 |
| `CPMAddPackage("gh:Tessil/robin-map@1.4.1")` | оставить | Нужен nanobind'у при `NB_USE_SUBMODULE_DEPS=OFF` (nanobind делает `find_dependency(tsl-robin-map)`) |
| — | добавить комментарий о том, что `${NANOBIND_LIB}` создаётся до `desbordante_add_bind` | Порядок вызовов важен для читаемости; cmake резолвит цели на этапе генерации, но явный порядок снижает риск ошибок |
| — (опционально) | `LTO`/`NOMINSIZE` в `nanobind_add_module` | Исторически `pybind11_add_module` сам включал LTO (`pybind11::lto`), nanobind — нет; при регрессе производительности на горячих биндингах добавить `LTO` (или `NOMINSIZE`, т.к. nanobind компилирует биндинги с `-Os`) |

#### 4.1.2 `cmake/desbordante_helpers.cmake`

* В `desbordante_add_bind()` заменить `list(APPEND arg_LIBS pybind11::pybind11 ...)` на `list(APPEND arg_LIBS ${NANOBIND_LIB} ...)`.
  **Причина:** nanobind-цель даёт include-пути nanobind, `-DNB_STATIC` (или `NB_SHARED`) и — в abi3-режиме — `-DPy_LIMITED_API`; без этого OBJECT-библиотеки не скомпилируются.
* Обновить комментарий-Brief функции: «libraries required for all bindings» → упомянуть nanobind.
  **Причина:** иначе документация CMake противоречит коду.
* Ничего больше не менять: `desbordante_add_lib()` уже выставляет hidden visibility и C++20, что совместимо с nanobind.
  **Причина:** nanobind требует C++17+ и сам выставляет hidden visibility; дублирование безвредно.

#### 4.1.3 Корневой `CMakeLists.txt`

* Поднять `cmake_minimum_required(VERSION 3.25)` до `3.26` (или выше) **или** не полагаться на `Development.SABIModule`.
  **Причина:** компонент `Development.SABIModule` у `FindPython` появился в CMake 3.26; без него `find_package(Python ... OPTIONAL_COMPONENTS Development.SABIModule)` на 3.25 не найдёт SABI-модуль, `nanobind_add_module(STABLE_ABI)` молча соберёт не-abi3 модуль, а scikit-build-core при `wheel.py-api=cp312` всё равно проштампует колесо тегом abi3 — получится нерабочее колесо.
* Добавить опции (имена — предложение):
  ```cmake
  option(DESBORDANTE_BINDINGS_STABLE_ABI "Build Python bindings against the limited API (abi3)" ON)
  option(DESBORDANTE_BINDINGS_FREE_THREADED "Build Python bindings for free-threaded Python" OFF)
  ```
  и прокидывать их в `src/python_bindings/CMakeLists.txt`.
  **Причина:** нужно управлять abi3/free-threading из одного места (pyproject/CI) и иметь возможность выключить abi3 для Python 3.10/3.11, если поддержка этих версий сохраняется (см. `STABLE_ABI.md`).
* В блоке `if(DESBORDANTE_BINDINGS) set(CMAKE_POSITION_INDEPENDENT_CODE ON)` — можно оставить без изменений.
  **Причина:** nanobind и так включает PIC для своей библиотеки, но PIC полезен и для объектных библиотек ядра, линкуемых в модуль.

#### 4.1.4 `pyproject.toml`

| Изменение | Причина |
|---|---|
| `requires = ["scikit-build-core", "nanobind>=3.1", "cmake>=3.26"]`, убрать `pybind11` | pybind11 больше не используется; версия nanobind фиксируется; `cmake>=3.26` нужен для `Development.SABIModule` |
| Добавить `[tool.scikit-build] wheel.py-api = "cp312"` (linked abi3) или `"cp310"` (split mode) | Значение публикуется в CMake как `SKBUILD_SABI_VERSION`; nanobind читает его и выбирает floor stable ABI; scikit-build-core по нему ставит тег `abi3` колесу |
| Добавить `[tool.scikit-build] minimum-version = "0.4"` (рекомендация upstream) | Защита от несовместимых будущих версий scikit-build-core |
| Если выбран split mode — добавить `nanobind-backend>=1.0` в `[project].dependencies` | Backend-модуль подгружается в рантайме; без зависимости `import desbordante` упадёт |
| Проверить `requires-python`: `>=3.10` остаётся только для split mode; для linked abi3 нужно `>=3.12` | Linked stable ABI требует CPython ≥ 3.12 (см. §7 и `STABLE_ABI.md`) |
| Классификаторы: убрать/оставить версии Python в соответствии с решением | Иначе метаданные колеса будут обещать неподдерживаемые версии |
| `[tool.scikit-build.cmake.define]` — при необходимости добавить `DESBORDANTE_BINDINGS_STABLE_ABI`/`..._FREE_THREADED` | Проброс новых опций CMake |

#### 4.1.5 `build.sh`

* Строка `-p, --pybind  Compile python bindings` — переименовать в `-p, --python` с сохранением `--pybind` как скрытого алиаса (или оставить как есть, только поправить описание).
  **Причина:** пользовательские скрипты/документация используют `--pybind`; ломать флаг не обязательно, но текст вводит в заблуждение. Минимальный вариант — поправить только help-текст.

#### 4.1.6 `.clang-format`

* В `IncludeCategories` заменить/дополнить правила:
  ```yaml
  - Regex: <nanobind/nanobind[.]h>|<Python[.]h>
    Priority: 1
  - Regex: '<(spdlog|boost|magic_enum|gmock|gtest|nanobind|Python).+>'
    Priority: 3
  ```
  **Причина:** сейчас главный заголовок `pybind11/pybind11.h` идёт первым (Priority 1), а `pybind11/*` — в группе сторонних (Priority 3). Без правки `nanobind/nanobind.h` попадёт в общую группу `<...>` (Priority 2), и `clang-format` будет переставлять include-блоки иначе, чем принято в проекте (в частности, ломая проверку `check-codestyle.yml`).

#### 4.1.7 Новый файл `src/python_bindings/binding_core.h` (создать)

Общий заголовок для всех TU биндингов: все caster-заголовки nanobind (`stl/vector.h`, `stl/string.h`, `stl/optional.h`, `stl/unordered_map.h`, `stl/unordered_set.h`, `stl/pair.h`, `stl/tuple.h`, `stl/variant.h`, `stl/filesystem.h`, `stl/function.h`, `stl/shared_ptr.h`, `stl/complex.h` — только реально используемые), объявления `NB_MAKE_OPAQUE(...)` для типов, которые биндуются и одновременно могли бы попасть под caster, и мелкие helper-функции (`ToPyStr(std::string const&)`).

**Причина:** nanobind — это набор частичных специализаций `nanobind::detail::type_caster<T>`; если в разных TU набор подключаемых caster'ов различается, возникает нарушение ODR (официальный FAQ nanobind прямо рекомендует один общий заголовок). В проекте ~44 независимых OBJECT-библиотеки — риск ODR максимальный. Дополнительно helper `ToPyStr` нужен потому, что `nb::str` не имеет конструктора из `std::string` (только `const char*` и `(const char*, size_t)`), а `py::str(std::string)` в коде встречается часто.

---

### 4.2 Точка входа

#### 4.2.1 `src/python_bindings/bindings.cpp`

* Убрать `#include <pybind11/pybind11.h>`, `namespace nb = nanobind` в анонимном namespace — заменить на `#include <nanobind/nanobind.h>` и `namespace nb = nanobind;` в `namespace python_bindings`.
  **Причина:** единый API; анонимный namespace вокруг alias не нужен.
* `PYBIND11_MODULE(desbordante, module, pybind11::mod_gil_not_used())` → `NB_MODULE(desbordante, module)`.
  **Причина:** `NB_MODULE` сам регистрирует модуль в nanobind и (на free-threaded сборке) выставляет нужные слоты; `mod_gil_not_used()` — pybind11-специфичный механизм, в nanobind роль играет CMake-флаг `FREE_THREADED`.
* Удалить `if (!nb::register_module(module.ptr())) throw pybind11::error_already_set();` и `auto nb_module = nb::borrow<nb::module_>(module.ptr());`.
  **Причина:** после перехода на `NB_MODULE` регистрация выполняется автоматически; `register_module` существует только для гибридного режима «pybind11 + nanobind в одном модуле».
* `BindDataTypes(nb_module)` → `BindDataTypes(module)`.
  **Причина:** все `Bind*` становятся `nb::module_&`.
* Убрать `using namespace pybind11::literals;` (и `#include <initializer_list>` при необходимости оставить — он не связан с pybind11).
  **Причина:** литералы pybind11 в этом файле не используются.
* Список вызовов `bind_func(module)` сохраняется; все сигнатуры `Bind*` меняются на `nb::module_&` (см. §4.4).
  **Причина:** после `NB_MODULE` переменная `module` имеет тип `nb::module_`.

#### 4.2.2 `src/python_bindings/bind_main_classes.h`

* `#include <pybind11/pybind11.h>` → `#include <nanobind/nanobind.h>`; `pybind11::kwargs` → `nanobind::kwargs`; `pybind11::module_&` → `nanobind::module_&`.
  **Причина:** тип аргумента `ConfigureAlgo` и сигнатура `BindMainClasses` обязаны совпадать с реализацией.

#### 4.2.3 `src/python_bindings/bind_main_classes.cpp`

* **Исправить синтаксическую ошибку** в строке 79: `... "\" doesn't exist!"};data` → `... "\" doesn't exist!"};`.
  **Причина:** ветка `nanobind-migration` в текущем виде не собирается; это артефакт экспериментального коммита.
* Убрать `#include <pybind11/pybind11.h>`, `#include <pybind11/stl.h>`; добавить `#include <nanobind/nanobind.h>`, `#include <nanobind/stl/unordered_map.h>` (для `get_opts`), `#include <nanobind/stl/string.h>` (если потребуется), `#include <nanobind/stl/string_view.h>` — по фактическому использованию.
  **Причина:** nanobind не имеет единого `stl.h`; каждый caster подключается отдельным заголовком.
* `namespace py = pybind11;` → `namespace nb = nanobind;`; все `py::` → `nb::`.
  **Причина:** механическое переименование.
* `py::register_exception<config::ConfigurationError>(main_module, "ConfigurationError", PyExc_ValueError)` → `nb::exception<config::ConfigurationError>(main_module, "ConfigurationError", PyExc_ValueError)`.
  **Причина:** в nanobind регистрация исключения — это шаблон класса `nb::exception<T>(scope, name, base)`, а не функция `register_exception`. Базовый класс `ValueError` сохраняется, поэтому `except ValueError`/`desb.ConfigurationError` в Python продолжат работать.
* `configure_algorithm::ConfigureAlgo(Algorithm&, py::kwargs const&)` → `nb::kwargs const&`; `kwargs.contains(option_name)` → `kwargs.contains(nb::str(option_name))` (или `nb::str(option_name.data())`), `kwargs[option_name.data()]` можно оставить.
  **Причина:** `nb::dict::contains` принимает либо `str_key` (C-строка), либо произвольный объект-ключ; `std::string_view` не имеет caster'а по умолчанию, поэтому нужно явно сделать `nb::str`.
* `option_value.is_none()` — оставить (есть в `nb::handle`).
  **Причина:** API совпадает.
* `"option_name"_a, "option_value"_a = py::none()` → `"option_name"_a, "option_value"_a = nb::none()`.
  **Причина:** в nanobind 3.x `nb::none` — это класс (а не функция, возвращающая `object`); присваивание `arg = nb::none()` дополнительно помечает аргумент как принимающий `None` (`cast_flags::accepts_none`) — в nanobind `None` не принимается по умолчанию.
* `pybind11::object res` в лямбде `get_opts` → `nb::object`; `OptToPy` меняет возвращаемый тип на `nb::object` (см. §4.3).
  **Причина:** согласование типов.
* `py::class_<Algorithm>(main_module, "Algorithm")`, `.def(...)` — без изменений по смыслу; вложенные лямбды с `py::handle` → `nb::handle`.
  **Причина:** синтаксис `def`/`class_` в nanobind совпадает, меняется только namespace.
* Для `_get_option_type` и `_set_option` — поведение сохраняется; `GetPyType` вернёт `nb::tuple` (см. §4.3.6).
  **Причина:** это Python-видимый контракт (`Algorithm.get_option_type`), менять его не требуется.

---

### 4.3 Каталог `py_util/` (11 `.cpp` + 11 `.h`)

Общие механические замены для всех файлов каталога (далее не повторяются):
`py::` → `nb::`, `pybind11::` → `nanobind::`, `#include <pybind11/pybind11.h>` → `#include <nanobind/nanobind.h>`, `#include <pybind11/stl.h>` → набор `<nanobind/stl/*.h>` по используемым типам.
**Причина:** у nanobind нет монолитного `stl.h`; caster'ы подключаются по одному на тип.

#### 4.3.1 `py_util/bind_primitive.h`

* `RegisterAlgorithm(pybind11::module_ module, ...)` → `RegisterAlgorithm(nb::module_ module, ...)`; `py::class_<AlgorithmType, BaseType>(module, name, py::multiple_inheritance())` → `nb::class_<AlgorithmType, BaseType>(module, name)`.
  **Причина:** у nanobind **нет** аналога `py::multiple_inheritance()` — поддерживается ровно один C++ базовый класс. Важно: `algos::Pyro` (`public DependencyConsumer, public PliBasedFDAlgorithm`) и `algos::PyroUCC` (`public DependencyConsumer, public UCCAlgorithm`) физически имеют два базовых класса; в Python виден только один (`PliBasedFDAlgorithm`/`UCCAlgorithm`). Это требует проверки в рантайме (см. §8, риск R2).
* `cls.doc() = MakeDocString<AlgorithmType>();` — можно оставить (у nanobind `class_` наследует `object`, у которого есть `doc()`), либо передать строку третьим аргументом конструктора `nb::class_<T, Base>(module, name, docstring)`.
  **Причина:** оба варианта поддерживаются; второй короче.
* `cls.def(py::init<>())` → `cls.def(nb::init<>())`.
* `py::class_<Base, Algorithm>(module, base_name).def(base_result_method_name, result_method)` → `nb::class_<Base, Algorithm>(...)`.
* `algos_module.attr("Default") = default_module;` — оставить.
  **Причина:** accessor-присваивание в nanobind работает так же.
* `module.def_submodule("algorithms")` — оставить.
  **Причина:** `nb::module_::def_submodule` существует.

#### 4.3.2 `py_util/py_to_any.{cpp,h}`

* `using ConvFunc = std::function<boost::any(std::string_view, py::handle)>` → `nb::handle`.
* `py::cast<T>(value)` → `nb::cast<T>(value)`; `catch (py::cast_error&)` → `catch (nb::cast_error&)`.
  **Причина:** свободная функция `nb::cast<T>` — единственный способ каста (метода `.cast<T>()` в nanobind нет); `nb::cast_error` — это `std::bad_cast`, поэтому `catch` работает.
* `py::isinstance<py::tuple>(obj)`, `py::cast<py::tuple>(obj)`, `py::isinstance<py::str>`, `py::isinstance<py::iterable>`, `py::cast<py::iterable>`, `py::isinstance<py::sequence>` → соответствующие `nb::`.
  **Причина:** одноимённые обёртки есть в nanobind (`nb::tuple`, `nb::str`, `nb::iterable`, `nb::sequence`).
* `py::len(arguments)`, `py::len(seq)` → `nb::len(...)`.
* `py::module_::import("pathlib")` → `nb::module_::import_("pathlib")`.
  **Причина:** `import` — зарезервированное слово C++, поэтому у nanobind метод называется `import_`.
* `py::reinterpret_borrow<py::object>(obj)` → `nb::borrow<nb::object>(obj)`.
* `std::vector<py::handle>` в `CastAndReplaceCastError` и `InputTablesToAny`/`CustomMetricsToAny` → `std::vector<nb::handle>` + `#include <nanobind/stl/vector.h>`.
  **Причина:** nanobind поддерживает caster для типов, производных от `detail::api_tag` (включая `nb::handle`), внутри `std::vector`; но заголовок `stl/vector.h` обязателен.
* `std::filesystem::path` → `#include <nanobind/stl/filesystem.h>`.
* `#include <pybind11/stl.h>`/`<pybind11/stl/filesystem.h>` → `nanobind/stl/vector.h`, `string.h`, `filesystem.h`, `unordered_map.h`, `unordered_set.h`, `optional.h`, `pair.h` — по факту.
* Итерация `for (py::handle item : seq)` → `for (nb::handle item : seq)`.
  **Причина:** у обёрток nanobind есть `begin()/end()`.
* Список исключений/ошибок: `boost::any` не меняется; `config::ConfigurationError` остаётся C++-исключением, которое транслируется зарегистрированным `nb::exception` (см. 4.2.3).

#### 4.3.3 `py_util/value_to_py.{cpp,h}`

* `pybind11::object` → `nb::object` (в т. ч. в `std::vector<pybind11::object> ValuesToPy(...)`).
* `py::none{}` → `nb::none()`; `py::bool_`, `py::int_`, `py::float_` → `nb::bool_`, `nb::int_`, `nb::float_`.
* `py::str(Type::GetValue<String>(value))` → `nb::str(s.c_str(), s.size())` (либо helper `ToPyStr` из `binding_core.h`).
  **Причина:** у `nb::str` нет конструктора из `std::string`.
* `py::reinterpret_steal<py::object>(PyLong_FromString(...))` → `nb::steal<nb::object>(PyLong_FromString(...))`.
  **Причина:** функция `PyLong_FromString` возвращает новую ссылку — `steal` семантически верен. `PyLong_FromString` входит в Stable ABI (проверено по документации CPython 3.15).
* `py::module_::import("datetime").attr("date")` → `nb::module_::import_("datetime").attr("date")`.
* `#include <pybind11/stl.h>` → `nanobind/stl/vector.h` (для `std::vector<nb::object>`) и `nanobind/stl/string.h` (если понадобится `nb::cast<std::string>`).
* `Py_RETURN_NONE` в этом файле не используется (там `py::none{}`), но в `statistics` — используется, см. §4.4.14.

#### 4.3.4 `py_util/get_py_type.{cpp,h}`

Это **один из ключевых файлов для Stable ABI.**

* `pybind11::tuple GetPyType(...)` → `nb::tuple`; `py::handle` → `nb::handle`; `py::tuple` → `nb::tuple`; `py::make_tuple` → `nb::make_tuple`.
* `py::type::of<T>()` → `nb::type<T>()`.
  **Причина:** переименование из официального porting guide; работает для типов, зарегистрированных через `nb::class_`.
* **Заменить статические объекты типов CPython** `MakeType(PyTypeObject*)`/`MakeType(PyObject*)` + `&PyBool_Type`, `&PyLong_Type`, `&PyFloat_Type`, `&PyUnicode_Type`, `&PyList_Type`, `&PyTuple_Type`, `&PySet_Type`, `&PyDict_Type`, `&PyFunction_Type` на **ленивое получение типов из `builtins`/`types`**:
  ```cpp
  nb::handle Builtin(char const* name) {
      static nb::dict const builtins = nb::module_::import_("builtins").attr("__dict__");
      return builtins[name];
  }
  // bool -> Builtin("bool"), int -> Builtin("int"), float -> Builtin("float"),
  // str -> Builtin("str"), list -> Builtin("list"), tuple -> Builtin("tuple"),
  // set -> Builtin("set"), dict -> Builtin("dict");
  // function -> nb::module_::import_("types").attr("FunctionType")
  ```
  **Причина:**
  1. в limited API часть конкретных type-object'ов скрыта. В CPython 3.12 `PyLong_Type`/`PyBool_Type` объявлены в `object.h` (доступны), `PyList_Type` — в `listobject.h` (доступен), но `PyFunction_Type` объявлен только в `Include/cpython/funcobject.h`, который подключается под `#ifndef Py_LIMITED_API` — при abi3-сборке код с `&PyFunction_Type` просто не скомпилируется;
  2. получение типов из `builtins` не зависит от версии limited API и одинаково работает в abi3 и обычной сборке.
* Комментарий в шапке файла («storing pybind11's objects themselves statically is unpredictable…») переформулировать: статически хранятся не `py::object`, а `PyTypeObject*`; после правки статическими могут быть `nb::dict`/`nb::object` (лениво инициализируемые) — важно не создавать их до инициализации интерпретатора.
  **Причина:** корректность при повторных импортах/выгрузках и ясность для сопровождающих.
* `std::unordered_map<std::type_index, std::function<py::tuple()>>` → `... std::function<nb::tuple()>`.
* `std::shared_ptr<pac::model::IDomain>` в списке — без изменений (это C++-тип).

#### 4.3.5 `py_util/opt_to_py.{cpp,h}`

* `using ConvFunction = std::function<py::object(boost::any)>` → `nb::object`.
* `py::cast(boost::any_cast<T>(value))` → `nb::cast(boost::any_cast<T>(value))`.
  **Причина:** `nb::cast` с выведенным типом возвращает `nb::object`; поведение совпадает.
* `py::cast(util::EnumToStr(...))` → `nb::cast(...)`; добавить `#include <nanobind/stl/string.h>`.
* `#include <pybind11/stl.h>` → набор `nanobind/stl/*.h` по типам (`vector.h`, `string.h`, `unordered_map.h` (нет), `memory.h` нет — `shared_ptr` требует `nanobind/stl/shared_ptr.h`).
  **Причина:** `std::shared_ptr<pac::model::IDomain>` в списке конвертеров требует caster'а `shared_ptr`.

#### 4.3.6 `py_util/table_serialization.{cpp,h}`

* `py::tuple` → `nb::tuple`, `py::make_tuple` → `nb::make_tuple`.
* `t[0].cast<std::string>()` → `nb::cast<std::string>(t[0])`; `t[1].cast<std::vector<std::string>>()` → `nb::cast<...>(t[1])`; `.cast<std::vector<unsigned int>>()` аналогично.
  **Причина:** у `nb::object`/`nb::handle` **нет** метода `.cast<T>()`; используется свободная функция `nb::cast<T>(obj)`.
* `PyObject*`-трюков здесь нет; `#include <pybind11/stl.h>` → `nanobind/stl/string.h`, `stl/vector.h`, `stl/tuple.h` (если tuple caster нужен — в этом файле создаются `nb::tuple` вручную, caster не нужен).
* Комментарий `// relational schema conversion suitable for py::hash` → `nb::hash`.

#### 4.3.7 `py_util/dataframe_reader.{cpp,h}`

* `#include <pybind11/pybind11.h>`, `<pybind11/pytypes.h>`, `<pybind11/stl.h>` → `nanobind/nanobind.h` + `nanobind/stl/vector.h` + `nanobind/stl/string.h`.
* `py::handle` → `nb::handle`, `py::object` → `nb::object`, `py::list` → `nb::list`.
* `py::reinterpret_borrow<py::object>(dataframe)` → `nb::borrow<nb::object>(dataframe)`.
* `py::iterator::sentinel()` (в `HasNextRow`) → `nb::iterator::sentinel()`.
  **Причина:** API совпадает (проверено в исходниках nanobind 3.1.0: `iterator::sentinel()` есть, `operator!=` определён).
* `py::cast<std::vector<std::string>>(*df_iter_++)` → `nb::cast<std::vector<std::string>>(*df_iter_++)`.
* `py::str(element)` → `nb::cast<std::string>(nb::str(element))` (в `GetColumnNames`).
  **Причина:** у `nb::str` нет неявного преобразования в `std::string`; нужен явный каст.
* `is_null_(el) ? ... : py::str(el)` → `nb::cast<std::string>(nb::str(el))`.
* **Проверить член `df_iter_`**: сейчас тип `py::object`, инициализируется `dataframe_.attr("itertuples")(...)`. В nanobind можно оставить `nb::object` (тогда `*iterator` не скомпилируется) либо объявить `nb::iterator` и инициализировать `nb::iter(...)`/`attr(...)` кастом. Рекомендация: `nb::iterator` + `nb::cast<nb::iterator>(...)`.
  **Причина:** у `nb::object` нет `operator*`/`operator++`, а `nb::iterator` их имеет и поддерживает сравнение с `sentinel()`.

#### 4.3.8 `py_util/create_dataframe_reader.{cpp,h}`

* `py::handle` → `nb::handle`; `py::isinstance(object, py::module::import("pandas").attr("DataFrame"))` → `nb::isinstance(object, nb::module_::import_("pandas").attr("DataFrame"))`.
* `catch (py::error_already_set& e)` → `catch (nb::python_error& e)`; `e.matches(PyExc_ImportError)`/`(PyExc_KeyError)`/`(PyExc_AttributeError)` — оставить (метод `matches` есть у `nb::python_error`).
  **Причина:** `nb::python_error` — прямой аналог `py::error_already_set`, предоставляет `matches()`.
* `py::object dtypes` → `nb::object`; `py::str{"string"}` → `nb::str("string")`; `.cast<bool>()` → `nb::cast<bool>(...)`.
* `py::str(dataframe.attr("attrs")["name"])` → `nb::cast<std::string>(nb::str(...))`.
* `throw;` внутри catch — без изменений.
* `config::ConfigurationError` — транслируется через `nb::exception` (см. 4.2.3), поведение для Python сохраняется.

#### 4.3.9 `py_util/iterable_sequence_stream.{cpp,h}`

* `py::iterable` → `nb::iterable` (поле `sequence_`), `py::object iterator_` → `nb::iterator` (для `begin()` и `sentinel()`), `py::object item` → `nb::object`.
* `py::iterator::sentinel()` → `nb::iterator::sentinel()`.
* `py::reinterpret_borrow<py::object>(*iterator_)` → `nb::borrow<nb::object>(*iterator_)`; `*iterator_++` (если используется) — совместимо с `nb::iterator::operator++(int)`.
* `py::isinstance<py::tuple>(item)`, `py::cast<py::tuple>(item)`, `py::len(tuple)`, `py::cast<model::Timestamp>(tuple[1])`, `py::isinstance<py::iterable>(events_obj)`, `py::cast<model::Event>(event)` → `nb::`-версии.
* `catch (py::cast_error const&)` → `catch (nb::cast_error const&)`.
* `py::handle events_obj` → `nb::handle`.
* Заголовок: `#include "iterable_sequence_stream.h"` — оставить; добавить `<nanobind/stl/vector.h>`, `<nanobind/stl/tuple.h>` (для `py::cast<py::tuple>` нужен caster-тип `nb::tuple` — он встроенный, отдельный заголовок не нужен), `<nanobind/stl/string.h>` при необходимости.
  **Причина:** у `nb::tuple` есть встроенный caster (api_tag), дополнительных заголовков не требуется.

#### 4.3.10 `py_util/py_custom_metrics.{cpp,h}`

* `pybind11::object metric_` → `nb::object`; `pybind11::cast<double>(metric_(...))` → `nb::cast<double>(metric_(...))`.
  **Причина:** свободная функция `nb::cast<T>`.
* `#include <pybind11/pytypes.h>` → удалить (достаточно `nanobind/nanobind.h`).
* `void BindCustomMetrics(pybind11::module_&)` → `nb::module_&`; в `.cpp`: `py::class_<util::ICustomMetric, std::shared_ptr<util::ICustomMetric>>` → `nb::class_<util::ICustomMetric>` + `#include <nanobind/stl/shared_ptr.h>`.
  **Причина:** nanobind не использует holder'ы в `class_`; для функций, принимающих/возвращающих `shared_ptr`, нужен соответствующий caster.
* **Внимание (потокобезопасность):** `nb::object` копируется/уничтожается с удержанием GIL; при free-threaded сборке это требует `nb::ft_mutex`/`nb::lock_self()` (см. §8, риск R5).

#### 4.3.11 `py_util/vector_to_tuple.h`

* `py::tuple`, `py::make_tuple` → `nb::tuple`, `nb::make_tuple`; `py::cast(...)` (если есть) → `nb::cast(...)`.
* `std::vector` итераторы — без изменений.
* Добавить `#include <nanobind/nanobind.h>`.

#### 4.3.12 `py_util/logging.{cpp,h}` — **функциональная переработка**

* Убрать `#include <pybind11/detail/common.h>`, `#include <pybind11/stl.h>`, `#ifdef PYBIND11_HAS_SUBINTERPRETER_SUPPORT`/`#include <pybind11/subinterpreter.h>`, `#ifdef PYBIND11_HAS_SUBINTERPRETER_SUPPORT return py::subinterpreter::current().id(); #else return 0; #endif`.
  **Причина:** nanobind **не поддерживает мультиинтерпретаторы**: в исходниках 3.1.0 нет ни `subinterpreter`, ни аналога `PYBIND11_HAS_SUBINTERPRETER_SUPPORT`; porting guide прямо относит multi-interpreter/embedding к удалённым возможностям. Код придётся упростить до одного logger'а на процесс.
* Заменить `std::unordered_map<InterpreterId, py::object> interpreters_loggers_` на одно поле `py::object logger_` (или `std::optional<nb::object>`), `RegisterLogger`/`UnregisterLogger`/`GetCurrentSubinterpreterLogger` — на сохранение/очистку/чтение одного объекта.
  **Причина:** без мультиинтерпретаторов ключ `InterpreterId` теряет смысл; сохранение одного logger'а эквивалентно текущему поведению для одноинтерпретаторного процесса.
* `py::object` → `nb::object`, `py::gil_scoped_acquire` → `nb::gil_scoped_acquire`, `logger.attr("isEnabledFor")(level).cast<bool>()` → `nb::cast<bool>(logger.attr("isEnabledFor")(level))`.
  **Причина:** механическая замена + отсутствие member `.cast`.
* В `sink_it_` учесть API-break nanobind 3.0: `nb::gil_scoped_acquire` на Python ≥ 3.15 может не получить thread state при завершении интерпретации; проверять `guard.is_valid()` и пропускать работу.
  **Причина:** иначе возможен abort/hang при shutdown (см. changelog nanobind 3.0.0, PEP 788).
* `py::module_ logging = py::module_::import("logging")` → `nb::module_ import_`; `py::hasattr(logging, "TRACE")` → `nb::hasattr(...)`; `logging.add_object("TRACE", py::int_(kPyTraceLevel))` → `logging.attr("TRACE") = nb::int_(kPyTraceLevel);`.
  **Причина:** у `nb::module_` **нет** метода `add_object` (проверено grep'ом по исходникам 3.1.0); присваивание через `attr()` — штатный способ добавить атрибут модуля.
* `catch (py::error_already_set const& e)` → `catch (nb::python_error const& e)`; `py::print("ERROR: ...", e.what(), py::arg("file") = ...)` → `nb::print(msg.c_str(), nb::handle(), file)` (собрать сообщение в `std::string`, файл получить как `nb::object`).
  **Причина:** у `nb::print` другая сигнатура: `print(handle value, handle end = {}, handle file = {})`; `py::arg` там не применяется.
* `py::cpp_function(CleanupAtExit)` → `nb::cpp_function(CleanupAtExit)`; `py::module_::import("atexit")` → `nb::module_::import_`.
* `BindLogging(py::module_&)` → `nb::module_&`.
* Дополнительно: `#include <cstdio>` (если используется `stderr`) — при `Py_LIMITED_API ≥ 3.11` `Python.h` перестаёт транзитивно включать `<stdio.h>`.
  **Причина:** иначе abi3-сборка может не найти `stderr`/`fprintf` (та же проблема была у самого nanobind, PR #1423).

#### 4.3.13 `py_util/logging.h`

* `#include <pybind11/pybind11.h>` → `<nanobind/nanobind.h>`; `pybind11::module_&` → `nanobind::module_&`.

---

### 4.4 Привязки алгоритмов (`*/bind_*.cpp`, `*/bind_*.h`, плюс `data/`, `bindings.cpp`)

Все 54 `.cpp` и 56 `.h` (полный список — в `MIGRATION_CHECKLIST.md`) делятся на группы.

#### 4.4.1 Механические замены (во всех файлах)

| № | Замена | Причина |
|---|---|---|
| M1 | `#include <pybind11/pybind11.h>` → `#include <nanobind/nanobind.h>` | единый заголовок nanobind |
| M2 | `py::` → `nb::`, `pybind11::` → `nanobind::` | namespace |
| M3 | `py::module_&`/`pybind11::module_&` в сигнатурах `Bind*` → `nb::module_&` | иначе не соберётся после смены `NB_MODULE` |
| M4 | `py::class_<T>(m, "T")` → `nb::class_<T>(m, "T")`; `.def(py::init<...>())` → `.def(nb::init<...>())` | переименование |
| M5 | `py::arg("x")` / `"x"_a` / `using namespace py::literals` → `nb::arg`/`nb::literals` | переименование; важно: в nanobind 3.x число `nb::arg` должно совпадать с арностью |
| M6 | `py::kw_only()` → `nb::kw_only()` | переименование |
| M7 | `py::make_tuple` → `nb::make_tuple`; `py::tuple` → `nb::tuple` | переименование |
| M8 | `py::object/handle/str/list/dict/int_/float_/bool_` → `nb::...` | переименование |
| M9 | `obj.cast<T>()` → `nb::cast<T>(obj)` | у `nb::object` **нет** метода `.cast<T>()` (115 мест в 21 файле) |
| M10 | `py::reinterpret_borrow<T>(x)` → `nb::borrow<T>(x)`; `py::reinterpret_steal<T>(x)` → `nb::steal<T>(x)` | переименование |
| M11 | `py::none()`/`py::none{}` → `nb::none()` | `nb::none` — класс; `= nb::none()` помечает аргумент как принимающий `None` |
| M12 | `py::len(x)` → `nb::len(x)`; `py::hash(x)` → `nb::hash(x)`; `py::isinstance<T>(x)` → `nb::isinstance<T>(x)`; `py::hasattr` → `nb::hasattr`; `py::print` → `nb::print` (сигнатура другая!) | переименование/уточнение |
| M13 | `py::module_::import("m")`/`py::module::import` → `nb::module_::import_("m")` | `import` — ключевое слово C++ |
| M14 | `def_property_readonly` → `def_prop_ro`; `def_property` → `def_prop_rw`; `def_readonly` → `def_ro`; `def_readwrite` → `def_rw` | переименование из porting guide |
| M15 | `py::return_value_policy::X` → `nb::rv_policy::X` | переименование; в nanobind 3.x политики — compile-time теги, «вычисляемые» политики запрещены |
| M16 | `py::error_already_set` → `nb::python_error`; `e.matches(...)` сохраняется | переименование |
| M17 | `py::value_error` → `nb::value_error`; `pybind11::index_error` → `nb::index_error`; `py::cast_error` → `nb::cast_error` | встроенные исключения nanobind |
| M18 | `py::self` → `nb::self` + `#include <nanobind/operators.h>` | переименование заголовка |
| M19 | `#include <pybind11/stl.h>` → индивидуальные `<nanobind/stl/....h>` | у nanobind нет общего `stl.h` |
| M20 | `#include <pybind11/operators.h>` → `<nanobind/operators.h>`; `<pybind11/functional.h>` → `<nanobind/stl/function.h>`; `<pybind11/complex.h>` → удалить (в коде нет `std::complex`); `<pybind11/stl_bind.h>` → удалить (нет `bind_vector`/`bind_map`); `<pybind11/cast.h>`, `<pybind11/pytypes.h>`, `<pybind11/typing.h>`, `<pybind11/detail/common.h>` → удалить/заменить на `<nanobind/nanobind.h>` (`nb::typed`/`nb::sig` объявлены в `nb_cast.h`, подключаемом через `nanobind.h`) | фактическое использование |
| M21 | `py::class_<T, std::shared_ptr<T>>` → `nb::class_<T>` + `#include <nanobind/stl/shared_ptr.h>` в TU, где `shared_ptr` встречается в сигнатурах | nanobind не использует holder'ы; shared_ptr-конверсии обеспечивает отдельный caster |
| M22 | `py::pickle(getstate, setstate)` → пара `.def("__getstate__", ...)` / `.def("__setstate__", ...)` | в nanobind нет `py::pickle`; канонический пример — в `docs/classes.rst` (Pickling): `__setstate__` конструирует объект на месте через placement new |
| M23 | `py::init([](args){ return T(...); })` → `.def("__init__", [](T* self, args){ new (self) T(...); })` | nanobind поддерживает кастомные конструкторы только через in-place construction (placement new), возврат значения из лямбды не поддерживается |
| M24 | `py::typing::Callable<Sig>` → `nb::typed<nb::callable, Sig>` (или просто `nb::callable`) | `py::typing::*` отсутствует; nanobind выражает сигнатуры через `nb::typed`/`nb::sig` |
| M25 | `py::arg_v("name", value, "doc")` → `nb::arg("name") = value` | типаж `nb::arg_v_t` есть, но публичного конструктора `(name, value, doc)` нет; докстринг аргумента в nanobind не поддерживается (при необходимости — `.sig(...)`) |
| M26 | `#include <pybind11/...>` в `.h`-файлах (54 файла) → `<nanobind/nanobind.h>` и `nb::module_&` в объявлении `Bind*` | иначе заголовок несовместим с реализацией |

#### 4.4.2 Группа `py::pickle` + `py::hash` (16 файлов)

`ac/bind_ac.cpp`, `ar/bind_ar.cpp`, `cfd/bind_cfd.cpp`, `cind/bind_cind.cpp`, `cind/bind_cind_verification.cpp`, `dc/bind_fastadc.cpp`, `dd/bind_split.cpp`, `fd/bind_afd.cpp`, `fd/bind_fd.cpp`, `ind/bind_ind.cpp`, `md/bind_md.cpp`, `nar/bind_nar.cpp`, `od/bind_od.cpp`, `pac/bind_pac.cpp`, `sfd/bind_sfd.cpp`, `ucc/bind_ucc.cpp`.

Для каждого:

* `py::hash(py::make_tuple(...))` → `nb::hash(nb::make_tuple(...))`; `py::hash(py::int_(...))` → `nb::hash(nb::int_(...))`; `py::hash(py::str(...))` → `nb::hash(ToPyStr(...))`.
* `py::pickle(get, set)` переписать по шаблону:
  ```cpp
  .def("__getstate__", [](FD const& fd) { /* как раньше, вернуть nb::tuple */ })
  .def("__setstate__", [](FD& fd, nb::tuple t) {
      if (t.size() != 3) throw std::runtime_error("Invalid state for FD pickle!");
      ...
      new (&fd) FD(lhs, rhs, std::move(schema));   // in-place, как в docs nanobind
  })
  ```
  **Причина:** `py::pickle` в nanobind отсутствует; Python-протокол pickle сам вызывает `__getstate__`/`__setstate__`; для nanobind-объектов `__setstate__` обязан сконструировать значение на месте (память уже выделена под T).
* Для `__setstate__` с типом, у которого есть присваивание, допустимо `fd = FD(...)`, но placement new — универсальный вариант (совпадает с документацией nanobind).
* `.cast<...>()` внутри `__setstate__` → `nb::cast<...>(...)`.
* Заголовки: `nanobind/stl/tuple.h` (для `nb::tuple` как параметра — он встроен, но при возврате `std::tuple` из `MakeFdNameTuple` нужен `stl/tuple.h`), `stl/vector.h`, `stl/string.h`, `stl/optional.h` — по факту использования.

#### 4.4.3 Файлы с `def_property_readonly`/`def_readonly`/`def_readwrite`

`ac/bind_ac.cpp`, `ar/bind_ar.cpp`, `cfd/bind_cfd.cpp`, `cfd/bind_cfd_verification.cpp`, `cind/bind_cind_verification.cpp`, `dd/bind_dd_verification.cpp`, `fd/bind_fd.cpp`, `fd/bind_fd_verification.cpp`, `gdd/bind_gdd_verification.cpp`, `gspan/bind_gspan.cpp`, `md/bind_md.cpp`, `md/bind_md_verification.cpp`, `mfd/bind_mfd_verification.cpp`, `nar/bind_nar.cpp`, `nd/bind_nd.cpp`, `nd/bind_nd_verification.cpp`, `od/bind_od.cpp`, `pac/bind_pac.cpp`, `pac/bind_pac_verification.cpp`, `sd/bind_sd_verification.cpp`, `ucc/bind_ucc.cpp`.

* Замены M14. Дополнительно: `def_prop_ro`/`def_prop_rw` в nanobind по умолчанию используют `rv_policy::reference_internal` — так же, как pybind11, поэтому семантика сохраняется.
  **Причина:** важно не потерять владение при возврате ссылок на члены.

#### 4.4.4 `gdd/bind_gdd_verification.cpp` — enum + кастомные конструкторы

* `py::enum_<DistanceMetric>(...)` → `nb::enum_<DistanceMetric>(...)`; `py::enum_<CmpOp>(...)` → `nb::enum_<CmpOp>(...)`. `.value(...)`, `.export_values()`, `.def("__eq__", ...)` сохраняются.
  **Внимание:** с nanobind 2.0 `nb::enum_` создаёт настоящий `enum.Enum`/`IntEnum`; `int(member)` больше не работает — в Python-коде (тесты, примеры, пользовательские скрипты) нужно использовать `.value`. В текущих тестах/примерах таких вызовов нет (проверено grep'ом), но это поведенческое изменение нужно отразить в release notes.
* `using py::literals::operator""_a;` → `using nb::literals::operator""_a;`.
* `py::init([](args){ return T{...}; })` (≈15 мест) → `.def("__init__", [](T* self, args){ new (self) T{...}; }, ...)`.
  **Причина:** M23; nanobind-лямбда-конструктор получает указатель на неинициализированную память.
* `std::variant` в `GddToken::field` (используются `std::holds_alternative`/`std::get`, а сигнатуры принимают `TokenField`) требует `#include <nanobind/stl/variant.h>`.
  **Причина:** `pybind11/stl.h` покрывал `std::variant`; в nanobind это отдельный заголовок.
* `#include <pybind11/stl/filesystem.h>` → `<nanobind/stl/filesystem.h>`.
* `.def_readwrite` → `.def_rw` и т. п. (M14).

#### 4.4.5 `gspan/bind_gspan.cpp` — итератор и `keep_alive`

* `pybind11::make_iterator(begin, end)` → `nb::make_iterator<nb::rv_policy::reference>(gspan_module, "DFSCodeIterator", begin, end)` + `#include <nanobind/make_iterator.h>`.
  **Причина:** у nanobind `make_iterator` требует первыми двумя аргументами scope и **имя** создаваемого типа-итератора (тип создаётся и «устанавливается» в модуле), а политика возврата задаётся шаблонным параметром.
* `pybind11::keep_alive<0, 1>()` → `nb::keep_alive<0, 1>()`; индексация та же (0 — возвращаемое значение, 1 — self).
  **Причина:** одинаковая семантика в nanobind; нужна, чтобы объект-владелец не был уничтожен, пока жив итератор.
* `pybind11::index_error` → `nb::index_error`.
* `def_readonly` → `def_ro`, `def_property_readonly` → `def_prop_ro` (M14).
* `std::unordered_set<int>` в `graphs_ids` требует `#include <nanobind/stl/unordered_set.h>`.
  **Причина:** у nanobind нет общего `stl.h`.

#### 4.4.6 `statistics/bind_statistics.cpp` — кастомный `type_caster` (критично)

Сейчас файл специализирует `PYBIND11_NAMESPACE::detail::type_caster<algos::Statistic>` с `PYBIND11_TYPE_CASTER` и `static handle cast(...)`. Переписать под интерфейс nanobind:

```cpp
namespace nanobind::detail {
template <>
struct type_caster<algos::Statistic> {
    static constexpr auto Name = const_name("float | int | str");

    bool from_python(handle, uint32_t, cleanup_list*) noexcept { return false; }

    static handle from_cpp(algos::Statistic const& stat, rv_policy, cleanup_list*) noexcept {
        if (!stat.HasValue()) return none().release();
        switch (stat.GetType()->GetTypeId()) {
            case model::TypeId::kDouble:
                return PyFloat_FromDouble(model::Type::GetValue<model::Double>(stat.GetData()));
            case model::TypeId::kInt:
                return PyLong_FromLongLong(model::Type::GetValue<model::Int>(stat.GetData()));
            case model::TypeId::kString: { auto const& s = ...; return PyUnicode_FromStringAndSize(s.data(), s.size()); }
            case model::TypeId::kBool: { bool v = ...; return PyBool_FromLong(v); }
            default: PyErr_SetString(PyExc_TypeError, "unsupported Statistic type"); return handle();
        }
    }
};
}  // namespace nanobind::detail
```

* **Причина:** `PYBIND11_TYPE_CASTER`/`type_caster::cast` — API pybind11; у nanobind макрос называется `NB_TYPE_CASTER`, а точки входа — `from_python(handle, uint32_t flags, cleanup_list*) noexcept` и `from_cpp(..., rv_policy, cleanup_list*) noexcept` (3.0 расширил `flags` до `uint32_t`). Функции-кастеры не должны бросать C++-исключения: при ошибке в `from_cpp` надо выставить Python-ошибку и вернуть пустой `handle`.
* `Py_RETURN_NONE` в `from_cpp` → `none().release()` (в `from_cpp` возвращается `handle`).
* `PyFloat_FromDouble`, `PyLong_FromLongLong`, `PyUnicode_FromStringAndSize`, `PyBool_FromLong` — **входят в Stable ABI** (проверено по документации CPython 3.15), так что abi3-сборке не мешают.
* `BindStatistics(pybind11::module_&)` → `nb::module_&`; `py::arg` → `nb::arg`; `std::unordered_set<...>`/`std::vector<...>` в возвращаемых типах → соответствующие `nanobind/stl/*.h`.
  **Причина:** механические замены; список возвращаемых контейнеров в файле большой (`get_vocab`, `get_words`, `get_top_k_*` и т. д.).
* **Проверить**, что `nb::cast<algos::Statistic>` не используется на входе (там `from_python` возвращает false) — иначе будет `TypeError`.
  **Причина:** кастер односторонний (только C++ → Python), как и раньше.

#### 4.4.7 `md/bind_md.cpp` + `md/object_similarity_measure.h` — `py::typing::Callable`, `arg_v`, кастомные конструкторы

`md/bind_md.cpp`:
* `py::typing::Callable<std::vector<ColumnClassifierValueId>(...) >` → `nb::typed<nb::callable, std::vector<ColumnClassifierValueId>(...)>` (или `nb::callable`, если сигнатура не важна).
  **Причина:** `py::typing::*` отсутствует; nanobind выражает `Callable[[...], R]` через `nb::typed<nb::callable, R(Args...)>`; на рантайме это `collections.abc.Callable`, проверка — `PyCallable_Check`.
* `py::arg_v("pick_lhs_indices", ccv_id_pickers::SimilaritiesPicker(PickAll), "...")` → `nb::arg("pick_lhs_indices") = ccv_id_pickers::SimilaritiesPicker(PickAll)`.
  **Причина:** M25.
* `py::init<ColumnIdentifier, ColumnIdentifier, model::md::DecisionBoundary, ...>` → `nb::init<...>`; `py::init([](...){...})` (2 места) → `__init__` + placement new.
* `py::class_<MeasureType, ColumnMatch, std::shared_ptr<MeasureType>>` → `nb::class_<MeasureType, ColumnMatch>` + `nanobind/stl/shared_ptr.h`.
* `py::pickle` → `__getstate__`/`__setstate__`; `py::hash` → `nb::hash`.
* `py::type::of<...>` (если есть) → `nb::type<...>()`; `.cast<T>()` → `nb::cast<T>(...)`.

`md/object_similarity_measure.h`:
* Все `pybind11::object` (поля `comparison_function_`, `StrTransform::operator()`, `PyLhsCCVIDsPicker`) → `nb::object`; `pybind11::cast<double>` → `nb::cast<double>`.
* `#include <pybind11/stl.h>` → `<nanobind/stl/vector.h>` (и `<nanobind/stl/string.h>`).
* Сохранить/уточнить комментарий-TODO про no-GIL и subinterpreter: при free-threaded сборке вызовы Python-функций из воркеров требуют удержания GIL-эквивалента (`nb::gil_scoped_acquire`), сейчас `kEnablePythonMultithreading = false`.
  **Причина:** nanobind не даёт автоматической потокобезопасности; без явного guard возможны гонки.

#### 4.4.8 `pac/bind_domains.{cpp,h}` + `pac/bind_pac.cpp`

* `pybind11::class_<IDomain, std::shared_ptr<IDomain>>` → `nb::class_<IDomain>`; `Ball`/`Parallelepiped`/`PyDomain` — так же, с одним базовым классом `IDomain`.
  **Причина:** M21; `IDomain` — абстрактный полиморфный класс, holder не нужен.
* `PyDomain` в `bind_domains.h` хранит `pybind11::object dist_from_domain_` → `nb::object`; `pybind11::cast<double>(...)` → `nb::cast<double>(...)`; `void BindDomains(pybind11::module_&)` → `nb::module_&`.
* `using namespace py::literals;` → `nb::literals`.
* `py::pickle`/`py::hash`/`py::self` в `bind_pac.cpp` → `__getstate__`/`__setstate__`, `nb::hash`, `nb::self` + `<nanobind/operators.h>`.
* `#include <pybind11/detail/common.h>` в `bind_pac.cpp` удалить; `<pybind11/cast.h>` → удалить.
* `#include <pybind11/functional.h>` → `<nanobind/stl/function.h>`; `<pybind11/complex.h>` → удалить (не используется).
  **Причина:** M20.

#### 4.4.9 `dc/bind_dc_verification.cpp` и `dd/bind_dd_verification.cpp`

* `dc/bind_dc_verification.cpp`: удалить `#include <pybind11/complex.h>` и `#include <pybind11/stl_bind.h>` (в файле нет ни `std::complex`, ни `bind_vector`/`bind_map`), `#include <pybind11/stl.h>` → `<nanobind/stl/*.h>` по фактическим типам; `py::module_&` → `nb::module_&`.
  **Причина:** мёртвые include'ы после миграции; у nanobind нет `stl_bind.h`.
* `dd/bind_dd_verification.cpp`: `py::class_<Metric, std::shared_ptr<Metric>>` → `nb::class_<Metric>` + `stl/shared_ptr.h`; `py::init<std::function<double(int,int)>>` → `nb::init<...>` + `stl/function.h`; `py::hash(...)` → `nb::hash(...)`; `py::len(...)` → `nb::len(...)`; удалить `#include <pybind11/complex.h>`.
* `dd/bind_split.cpp`: `py::pickle` → `__getstate__`/`__setstate__`; `py::hash(py::str(...))` → `nb::hash(ToPyStr(...))`; `py::self` → `nb::self` + `operators.h`.
* `dd/create_dd.cpp`/`create_dd.h` — **изменений не требуют**: файлы не используют Python API (чистые C++-хелперы), они лишь участвуют в списке источников `desbordante_add_bind(dd ...)`.
  **Причина:** нет ни одного вхождения `py::`/`pybind11`.

#### 4.4.10 `data/bind_data_types.{cpp,h}` (уже переведены — проверить и дополнить)

* Убедиться, что `#include <nanobind/stl/vector.h>` достаточно; используется `nb::make_tuple`, `nb::cast`, `nb::pickle` уже переписан на `__getstate__`/`__setstate__`.
* **Исправить потенциальную утечку/двойное конструирование в `__setstate__`:** текущий код делает `new (&cc) ColumnCombination(...)` без вызова деструктора старого значения. Для `ColumnCombination` (значимый тип без ресурсов) это безопасно, но общий шаблон для остальных 21 pickle-класса должен быть либо `cc = ColumnCombination(...)` (если есть присваивание), либо `cc.~ColumnCombination(); new (&cc) ColumnCombination(...);`.
  **Причина:** placement new поверх живого объекта без разрушения — UB для типов с нетривиальным деструктором (`FD`, `RawCFD`, `CIND`, … содержат `std::string`/`shared_ptr`).
* Убрать пробел-опечатку/форматирование (файл уже отформатирован).
* `data/bind_data_types.h`: `nanobind::module_&` — ок.

#### 4.4.11 `cfd/bind_cfd.cpp` и `cfd/bind_cfd_verification.cpp`

* `py::self == py::self` → `nb::self == nb::self` + `#include <nanobind/operators.h>`.
* `py::pickle` → `__getstate__`/`__setstate__` (2 места), `py::hash` → `nb::hash`, `.cast<...>()` → `nb::cast<...>(...)`, `py::value_error` → `nb::value_error`.
* `py::init<int, std::optional<std::string>>` → `nb::init<...>` + `#include <nanobind/stl/optional.h>`.
* `bind_cfd_verification.cpp`: `def_property_readonly` → `def_prop_ro`; `py::module_&` → `nb::module_&`.

#### 4.4.12 `nar/bind_nar.cpp`, `ind/bind_ind.cpp`, `od/bind_od.cpp`, `sfd/bind_sfd.cpp`, `ucc/bind_ucc.cpp`, `fd/bind_fd.cpp`, `fd/bind_afd.cpp`, `ac/bind_ac.cpp`, `ar/bind_ar.cpp`, `dc/bind_fastadc.cpp`, `cind/*`

Для всех — M1–M26 плюс:

* `py::pickle` → `__getstate__`/`__setstate__` (файлы из §4.4.2).
* `py::hash` → `nb::hash`.
* `py::return_value_policy::copy`/`move`/`reference_internal` → `nb::rv_policy::copy`/`move`/`reference_internal` (`dc/bind_fastadc.cpp`, `ar/bind_ar.cpp`, `sfd/bind_sfd.cpp`, `ac/bind_ac.cpp`).
* `py::class_<..., std::shared_ptr<...>>` → `nb::class_<...>` + `stl/shared_ptr.h` (`nar`, `fd/afd`, `dc/fastadc`, `sfd`, `ucc`, `md`).
* `py::dict`/`py::int_` в `nar/bind_nar.cpp` → `nb::dict`/`nb::int_`.
* `py::cast<...>` (свободный) и `.cast<...>()` (метод) — см. M9.
* `fd/bind_fd.cpp`: `BindPrimitive<..., Pyro>` — см. риск R2 (`multiple_inheritance`).
* `ucc/bind_ucc.cpp`: `BindPrimitive<HPIValid, HyUCC, PyroUCC>` — см. риск R2.

#### 4.4.13 Остальные механические файлы

Полный список (только M-замены + смена сигнатур `Bind*`):
`afd_metric/bind_afd_metric_calculation.{cpp,h}`, `ar/bind_ar_verification.{cpp,h}`, `bindings.cpp` (см. 4.2.1), `cind/bind_cind_verification.{cpp,h}`, `dynamic/bind_dynamic_fd_verification.{cpp,h}`, `fd/bind_fd_verification.{cpp,h}`, `fem/bind_fem.{cpp,h}`, `gfd/bind_gfd.{cpp,h}`, `gfd/bind_gfd_verification.{cpp,h}`, `ind/bind_ind_verification.{cpp,h}`, `md/bind_md_verification.{cpp,h}`, `mfd/bind_mfd_verification.{cpp,h}`, `nd/bind_nd.{cpp,h}`, `nd/bind_nd_verification.{cpp,h}`, `od/bind_od_verification.{cpp,h}`, `pac/bind_pac_verification.{cpp,h}`, `pfd/bind_pfd_verification.{cpp,h}`, `sd/bind_sd_verification.{cpp,h}`, `statistics/bind_statistics.h`, `ucc/bind_ucc_verification.{cpp,h}`, `data/bind_data_types.{cpp,h}` (см. 4.4.10), `dd/bind_dd_verification.{cpp,h}`, `dd/bind_split.{cpp,h}`, `dd/create_dd.{cpp,h}` (без изменений), `nar/bind_nar.{cpp,h}`, `sfd/bind_sfd.{cpp,h}`, `ucc/bind_ucc.{cpp,h}`, `md/object_similarity_measure.h` (см. 4.4.7), `py_util/bind_primitive.h` (см. 4.3.1), все `py_util/*.{cpp,h}` (см. 4.3).

#### 4.4.14 `statistics/bind_statistics.h`

* `#include <pybind11/pybind11.h>` → `<nanobind/nanobind.h>`; `pybind11::module_&` → `nanobind::module_&`.

---

### 4.5 Python-тесты

#### 4.5.1 `src/python_bindings/test_bindings.py`

* Код менять почти не нужно: файл не использует pybind11-специфику (проверено: нет `pickle`, `copy`, `int(enum)`, `.cast`).
* Добавить проверки, чувствительные к миграции:
  1. `desb.ConfigurationError` по-прежнему наследуется от `ValueError` (после `nb::exception<...>(..., PyExc_ValueError)`);
  2. `pickle`/`copy` round-trip для 2–3 классов (`FD`, `RawCFD`, `CIND`) — самая рискованная часть ручной замены `py::pickle`;
  3. `get_opts()` возвращает те же Python-значения;
  4. `gspan`-итератор (`for edge in EdgeList`) жив после удаления исходного объекта (проверка `keep_alive`).
  **Причина:** эти четыре места — единственные, где миграция может изменить наблюдаемое поведение.
* Обновить сообщения/пути, если тест переименован (см. 4.5.2).

#### 4.5.2 `src/python_bindings/test_pybind_data_stats.py` → `test_data_stats.py`

* Переименовать файл и обновить `.github/workflows/bindings-tests.yml` (строки 99–110).
  **Причина:** имя содержит «pybind», после миграции оно вводит в заблуждение; git сохранит историю при `git mv`.

---

### 4.6 CI

#### 4.6.1 `.github/workflows/wheel.yml`

* Ограничить матрицу минимальной поддерживаемой версией: в env блока `generate-linux-wheels-matrix`/`generate-macos-wheels-matrix`/`build-wheels` задать `CIBW_BUILD: "cp312-*"`.
  **Причина:** у cibuildwheel **нет** abi3-идентификаторов сборки (`--print-build-identifiers` печатает `cp312-manylinux_x86_64` и т. п.), поэтому «выбрать abi3» через `CIBW_BUILD` нельзя. Правильная схема: собрать колесо на минимальной версии (3.12); scikit-build-core по `wheel.py-api=cp312` проштампует его тегом `abi3`, и это колесо покрывает 3.12–3.14. Сборка на 3.13/3.14 в матрице не нужна, а на 3.10/3.11 при `wheel.py-api=cp312` даст либо не-abi3 бинарник с abi3-тегом, либо ошибку конфигурации nanobind («You must set tool.scikit-build.wheel.py-api to 'cp312' or later…»). cibuildwheel 4.x дополнительно сам распознаёт abi3-колесо и запускает `abi3audit --strict`.
* Оставить `CIBW_TEST_COMMAND` (он копирует датасеты и запускает `test_bindings.py`) — он валидирует именно установленное колесо.
  **Причина:** smoke-тест abi3-колеса на разных Python-версиях — единственная проверка, что limited API действительно соблюдён.
* При выборе split mode: `CIBW_BUILD: "cp310-*"` и `pip install nanobind-backend` в тестовом окружении (через `[project].dependencies`, поэтому отдельная настройка не нужна).
  **Причина:** в split mode одно колесо на платформу.
* (Побочно) В workflow сейчас матрица генерируется `cibuildwheel==4.1.0`, а сборка идёт `pypa/cibuildwheel@v3.2.1`. Стоит выровнять версии, иначе идентификаторы `--print-build-identifiers` и фактическая сборка могут разойтись.
  **Причина:** рассинхронизация версий — источник трудноуловимых сбоев матрицы; к нанобинду отношения не имеет, но всплывёт при смене `CIBW_BUILD`.
* Колесо для Python 3.10/3.11 (если поддержка сохраняется) собирать отдельным job'ом с `wheel.py-api` пустым/`cp3XX` — см. `STABLE_ABI.md`, вариант C.

#### 4.6.2 `.github/workflows/bindings-tests.yml`

* Обновить имя тестового файла (`test_pybind_data_stats.py` → `test_data_stats.py`).
* Поднять `python-version`/venv до минимально поддерживаемой (3.12 при linked abi3; 3.10 при split mode).
  **Причина:** тестировать нужно минимальную поддерживаемую версию; для abi3-колеса дополнительно полезно прогонять импорт на двух разных версиях Python.
* Добавить шаг проверки суффикса собранного модуля: `python -c "import desbordante, pathlib; assert '.abi3.' in pathlib.Path(desbordante.__file__).name"`.
  **Причина:** гарантия, что `STABLE_ABI` не был молча проигнорирован (nanobind молча отключает abi3 на Python < 3.12 / PyPy / без `Python::SABIModule`).

#### 4.6.3 `.github/workflows/examples-tests.yml`

* `python-version: "3.10"` → минимально поддерживаемая (3.12 при варианте A).
  **Причина:** `pip install .` на 3.10 с `requires-python >=3.12` заведомо упадёт.

#### 4.6.4 `.github/workflows/check-codestyle.yml`

* Изменений в самих шагах не требуется; проверка clang-format подхватит новый `.clang-format` (см. 4.1.6).
  **Причина:** стиль проверяется автоматически.
* Если добавляется `binding_core.h`, убедиться, что он попадает под `find`-паттерн clang-format в workflow.
  **Причина:** иначе новый файл не будет проверяться на форматирование.

#### 4.6.5 `.github/composite-actions/install-dependencies/action.yml`

* Изменений не требуется (ставит boost/cmake/ninja/python3).
  **Причина:** nanobind скачивается CPM/scikit-build-core, отдельных системных зависимостей нет.

---

### 4.7 Документация

* `README.md`, строка 92: «using pybind11» → «using nanobind and the Python Stable ABI (abi3)».
* `README_PYPI.md`, строка 73: аналогично.
  **Причина:** утверждение о pybind11 станет ложным; PyPI-описание читают пользователи.
* `README.md`, строка 283 и далее (раздел сборки): описание флага `--pybind` и требования к Python (3.12+/3.10+) — синхронизировать с `pyproject.toml`.
  **Причина:** иначе документация обещает сборку на версиях, которые не поддерживаются.
* `examples/README.md` и `examples/**` — изменений не требуют (pybind11 не упоминается, специфичных для pybind11 приёмов нет — проверено grep'ом).
  **Причина:** примеры работают только с публичным API `desbordante`.
* `docs/papers/**` — не трогать (исторические PDF).
  **Причина:** архивные материалы.

---

## 5. Сводная таблица «pybind11 → nanobind» (для механической замены)

| pybind11 | nanobind | Причина/примечание |
|---|---|---|
| `PYBIND11_MODULE(name, m)` | `NB_MODULE(name, m)` | точка входа |
| `pybind11::mod_gil_not_used()` | CMake `FREE_THREADED` / `NB_FREE_THREADED` | нет аналога-функции |
| `py::module` / `py::module_` | `nb::module_` | одно написание |
| `m.def_submodule("x")` | `m.def_submodule("x")` | без изменений |
| `m.add_object(name, obj)` | `m.attr(name) = obj` | у `nb::module_` нет `add_object` |
| `py::module_::import` | `nb::module_::import_` | ключевое слово |
| `py::class_<T, Base>` | `nb::class_<T, Base>` | только один базовый класс |
| `py::class_<T, std::shared_ptr<T>>` | `nb::class_<T>` + `stl/shared_ptr.h` | holder'ы удалены |
| `py::multiple_inheritance()` | — | удалено; см. R2 |
| `py::init<...>` | `nb::init<...>` | — |
| `py::init(lambda)` | `.def("__init__", [](T* p, ...){ new (p) T(...); })` | in-place конструктор |
| `py::pickle(g, s)` | `__getstate__` + `__setstate__` | хелпера нет |
| `py::enum_<T>` | `nb::enum_<T>` | `int(member)` перестаёт работать |
| `py::dynamic_attr()` | `nb::dynamic_attr()` | — |
| `py::arg`, `py::arg_v`, `_a` | `nb::arg`, `nb::arg(...)=value`, `_a` | `arg_v` без докстроки |
| `py::kw_only()` | `nb::kw_only()` | — |
| `py::pos_only()` | — | удалено |
| `py::return_value_policy::X` | `nb::rv_policy::X` | compile-time теги |
| `py::keep_alive<N,P>()` | `nb::keep_alive<N,P>()` | те же индексы |
| `py::call_guard<T>()` | `nb::call_guard<T>()` | — |
| `py::gil_scoped_acquire/release` | `nb::gil_scoped_acquire/release` | в 3.x проверять `is_valid()` |
| `py::cast<T>(x)` | `nb::cast<T>(x)` | — |
| `x.cast<T>()` | `nb::cast<T>(x)` | метода нет |
| `py::reinterpret_borrow<T>` | `nb::borrow<T>` | — |
| `py::reinterpret_steal<T>` | `nb::steal<T>` | — |
| `py::error_already_set` | `nb::python_error` | `matches()` есть |
| `py::cast_error` | `nb::cast_error` (= `std::bad_cast`) | — |
| `py::register_exception<T>(m, n, base)` | `nb::exception<T>(m, n, base)` | — |
| `py::value_error`/`py::index_error` | `nb::value_error`/`nb::index_error` | — |
| `py::type::of<T>()` | `nb::type<T>()` | только для bound-типов |
| `py::type` | `nb::type_object` | — |
| `py::object/handle/str/bytes/tuple/list/dict/set/kwargs/iterable/sequence/iterator` | `nb::...` | `nb::iterator::sentinel()` есть |
| `py::none()` | `nb::none()` | в 3.x это класс |
| `py::len`, `py::hash`, `py::print`, `py::hasattr`, `py::isinstance<T>` | `nb::...` | у `print` другая сигнатура |
| `py::self` | `nb::self` | `nanobind/operators.h` |
| `py::make_iterator(b, e)` | `nb::make_iterator<policy>(scope, name, b, e)` | тип создаётся в модуле |
| `py::typing::Callable<Sig>` | `nb::typed<nb::callable, Sig>` | — |
| `py::detail::type_caster` + `PYBIND11_TYPE_CASTER` | `nanobind::detail::type_caster` + `NB_TYPE_CASTER` | новые сигнатуры `from_python`/`from_cpp` |
| `#include <pybind11/stl.h>` | `<nanobind/stl/*.h>` по типам | общего `stl.h` нет |
| `pybind11/subinterpreter.h` | — | мультиинтерпретаторы не поддерживаются |
| `py::eval`/`py::exec` | `nb::eval`/`nb::exec` | — |

---

## 6. Stable ABI: что именно ломается в текущем коде

Подробный разбор вариантов упаковки — в `STABLE_ABI.md`; здесь только список файлов-блокеров.

| Файл | Проблема | Что делать |
|---|---|---|
| `py_util/get_py_type.cpp` | `&PyFunction_Type` недоступен при `Py_LIMITED_API` (объявлен в `Include/cpython/funcobject.h` под `#ifndef Py_LIMITED_API`); остальные `Py*_Type` в 3.12+ доступны, но зависят от версии | получать типы из `builtins`/`types` во время выполнения (см. 4.3.4) |
| `py_util/logging.cpp` | `PYBIND11_HAS_SUBINTERPRETER_SUPPORT`, `py::subinterpreter::current()` | nanobind не поддерживает мультиинтерпретаторы — упростить sink до одного logger'а; проверять `nb::gil_scoped_acquire::is_valid()` |
| `bindings.cpp` | `pybind11::mod_gil_not_used()` (free-threading) | заменить на CMake `FREE_THREADED`; в linked abi3 free-threading невозможен, нужен split mode + Python ≥ 3.15 (abi3t) |
| `statistics/bind_statistics.cpp` | `PYBIND11_NAMESPACE`/`PYBIND11_TYPE_CASTER` | переписать на `nanobind::detail::type_caster`; используемые `Py*`-функции входят в Stable ABI |
| `py_util/value_to_py.cpp` | `PyLong_FromString` — входит в Stable ABI (проверено), `py::reinterpret_steal` | заменить на `nb::steal`; блокером не является |
| `py_util/dataframe_reader.cpp` | прямой `#include <Python.h>` | оставить; при `Py_LIMITED_API ≥ 3.11` не полагаться на транзитивный `<stdio.h>`, при необходимости включить явно |
| Все `py_util/*`, `*/bind_*.cpp` | `#include <pybind11/...>` | заменить на nanobind-заголовки; abi3 требует, чтобы в коде не было обращений к не-limited API CPython |

Дополнительно: `nanobind_add_module(STABLE_ABI)` **молча игнорирует** abi3, если Python < 3.12, интерпретатор не CPython или отсутствует `Python::SABIModule`. Поэтому в CI нужен явный тест суффикса `.abi3.` (см. 4.6.2), иначе можно выпустить обычный модуль с abi3-тегом колеса.

---

## 7. Риски и регрессии

| ID | Риск | Причина/последствие | Митигация |
|---|---|---|---|
| R1 | Массовая механическая правка ~106 файлов | 115 методов `.cast<T>()`, 22 `py::pickle`, 43 `py::init` — высокий шанс опечаток | делать по группам, компилировать после каждой; чек-лист в `MIGRATION_CHECKLIST.md` |
| R2 | `Pyro`/`PyroUCC` имеют два C++ базовых класса (`DependencyConsumer` + алгоритм) | у nanobind нет `py::multiple_inheritance()`; поддерживается один базовый класс, апкаст из невидимого базового класса невозможен | проверить рантайм-тестом создание/использование `Pyro` и `PyroUCC`; при проблемах — не наследовать Python-класс от `FdAlgorithm`/`UccAlgorithm` (объявлять отдельно) либо вернуть тонкую обёртку |
| R3 | `py::pickle` → `__setstate__` | placement new поверх живого объекта без деструктора — UB для типов с `std::string`/`shared_ptr` | использовать `self = T(...)` где возможно, иначе `self.~T(); new (&self) T(...);`; тест pickle round-trip |
| R4 | Отказ от мультиинтерпретаторов | пользователи, запускающие desbordante в subinterpreter'ах с per-interpreter GIL, потеряют изоляцию логирования | зафиксировать в release notes; sink оставить потокобезопасным (мьютекс уже есть) |
| R5 | `nb::object` в C++-классах, используемых из воркеров (`md/object_similarity_measure.h`, `PyCustomMetric`) | в free-threaded сборке копирование/уничтожение `nb::object` требует удержания GIL-эквивалента | `nb::gil_scoped_acquire` вокруг вызовов Python; `kEnablePythonMultithreading=false` сохранить |
| R6 | `nb::enum_` меняет модель enum (`int(member)` больше не работает) | ломает пользовательский код, который передавал enum как int | отразить в release notes; gdd-примеры/тесты проверить |
| R7 | Ужесточение nanobind к `None`/`__index__` (2.14+) | `_set_option(name, None)`, `metric="euclidean"`, приведение `np.float32`/`"123"` к int могут начать падать иначе | прогнать `test_bindings.py`/`test_data_stats.py`/examples; при необходимости добавить `.none()`/`__index__`-совместимые значения |
| R8 | ODR при смешивании caster'ов в 44 OBJECT-библиотеках | nanobind использует частичные специализации `type_caster<T>`; разные наборы включений в TU → UB | единый `binding_core.h` (см. 4.1.7) + `NB_MAKE_OPAQUE` где нужно |
| R9 | Производительность | linked abi3 медленнее обычной сборки; nanobind по умолчанию `-Os` и без LTO (в отличие от `pybind11_add_module`, который включал LTO) | бенчмарки из `src/tests/benchmark`; при регрессе — `LTO`/`NOMINSIZE` |
| R10 | Смена версии Python в CI/упаковке | `requires-python`/классификаторы/примеры/README должны быть согласованы | единый PR с обновлением `pyproject.toml`, workflow'ов и README |
| R11 | Молчаливое отключение abi3 | nanobind игнорирует `STABLE_ABI` на Python < 3.12/не-CPython/без `Python::SABIModule` | проверка суффикса `.abi3.` в CI и/или `FATAL_ERROR`, если `wheel.py-api ≥ cp312`, а `Python::SABIModule` не найден |

---

## 8. Порядок работ

**Фаза 0. Стабилизация ветки**
1. Исправить `bind_main_classes.cpp:79` (`};data`).
2. Решить, продолжать ли гибридную схему (она допустима как промежуточная) или сразу переходить на `NB_MODULE`.
   **Причина:** гибрид требует `nb::register_module` до любого `nb::`-вызова и запрещает передавать типы между `py::`- и `nb::`-биндингами — для 100+ файлов это лишняя сложность, поэтому рекомендуется сразу полный переход.

**Фаза 1. Сборка (готовит почву)**
3. `cmake/desbordante_helpers.cmake`: `pybind11::pybind11` → `${NANOBIND_LIB}`.
4. `src/python_bindings/CMakeLists.txt`: убрать pybind11, добавить `SABIModule`, `NANOBIND_LIB`, `nanobind_add_module`.
5. Корневой `CMakeLists.txt`: CMake ≥ 3.26, опции abi3/free-threading.

**Фаза 2. Точка входа и py_util (ядро)**
6. `bindings.cpp` → `NB_MODULE`.
7. `bind_main_classes.*`, `bind_primitive.h`, все `py_util/*` (включая нетривиальные `logging.cpp`, `get_py_type.cpp`, `statistics`).
8. Собрать — на этом шаге компилируются только эти файлы; остальные `Bind*` ещё имеют старую сигнатуру, поэтому модуль временно не должен компилироваться до фазы 3, либо сигнатуры меняются сразу во всех `.h` (механически, без правки `.cpp` — тогда `.cpp` перестанут компилироваться).

**Фаза 3. Массовая миграция `bind_*.cpp`**
9. По группам (pickle/hash → class_/holders → property/init → остальное), компилируя после каждой группы.
10. Заголовки `bind_*.h` — механически (одна и та же замена).

**Фаза 4. Упаковка и CI**
11. `pyproject.toml`: `nanobind`, `cmake>=3.26`, `wheel.py-api`, решение по `requires-python`.
12. `wheel.yml`, `bindings-tests.yml`, `examples-tests.yml`, `.clang-format`.
13. Тесты: pickle/copy/exception/iterator; переименование `test_pybind_data_stats.py`.

**Фаза 5. Документация и релиз**
14. `README.md`, `README_PYPI.md`.
15. Release notes: отказ от мультиинтерпретаторов, изменение enum (`int(member)`), требования к Python.

---

## 9. Источники

* Nanobind. Porting guide — https://nanobind.readthedocs.io/en/latest/porting.html
* Nanobind. CMake API Reference — https://nanobind.readthedocs.io/en/latest/api_cmake.html
* Nanobind. Packaging — https://nanobind.readthedocs.io/en/latest/packaging.html
* Nanobind. Changelog (3.0.0 — split mode, API breaks; 3.1.0) — https://nanobind.readthedocs.io/en/latest/changelog.html
* Nanobind. FAQ (stable ABI до 3.12, split mode, ABI-совместимость, ODR/NB_MAKE_OPAQUE) — https://nanobind.readthedocs.io/en/latest/faq.html
* Nanobind. Free-threaded Python — https://nanobind.readthedocs.io/en/latest/free_threaded.html
* Nanobind. Exchanging information (caster'ы) — https://nanobind.readthedocs.io/en/latest/exchanging.html
* Nanobind 3.1.0, `cmake/nanobind-config.cmake` и `include/nanobind/*.h` (проверка по закешированному исходнику `cmake/cpm-cache/nanobind/58ad`)
* CPython C API: Floating-Point Objects (`PyFloat_FromDouble` — Stable ABI) — https://docs.python.org/3/c-api/float.html
* CPython C API: Integer Objects (`PyLong_FromLongLong`, `PyLong_FromString` — Stable ABI) — https://docs.python.org/3/c-api/long.html
* CPython 3.12 `Include/object.h` (`PyLong_Type`, `PyBool_Type` вне `Py_LIMITED_API`) — https://github.com/python/cpython/blob/3.12/Include/object.h
* CPython 3.12 `Include/cpython/funcobject.h` (`PyFunction_Type` под `#ifndef Py_LIMITED_API`) — https://github.com/python/cpython/blob/3.12/Include/cpython/funcobject.h
* pybind11 stable ABI: открытый PR #6198 — https://github.com/pybind/pybind11/pull/6198

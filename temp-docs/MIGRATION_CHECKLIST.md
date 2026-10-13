# MIGRATION_CHECKLIST.md — построчный чек-лист миграции

Дополнение к `REPORT.md` (обоснования изменений — там). Здесь — полный перечень файлов с отметками. Порядок выполнения — сверху вниз: сначала сборка, затем ядро `py_util`, затем массовые биндинги, затем упаковка/CI/доки.

## Легенда замен

| Код | Замена |
|---|---|
| **R1** | `#include <pybind11/pybind11.h>` → `#include <nanobind/nanobind.h>`; `#include <pybind11/stl.h>` → нужные `<nanobind/stl/*.h>` |
| **R2** | `py::` → `nb::`, `pybind11::` → `nanobind::`, `namespace py = pybind11` → `namespace nb = nanobind` |
| **R3** | `Bind*(py::module_&)` / `pybind11::module_&` → `nb::module_&` (и в `.h`, и в `.cpp`) |
| **R4** | `py::class_<T>` → `nb::class_<T>`; `py::init<...>` → `nb::init<...>` |
| **R5** | `py::class_<T, std::shared_ptr<T>>` → `nb::class_<T>` + `#include <nanobind/stl/shared_ptr.h>` |
| **R6** | `py::arg`/`py::arg_v`/`_a`/`py::kw_only` → `nb::arg` (или `nb::arg("x") = value`) / `nb::literals` / `nb::kw_only` |
| **R7** | `py::make_tuple` → `nb::make_tuple`; `py::tuple` → `nb::tuple` |
| **R8** | `x.cast<T>()` → `nb::cast<T>(x)` |
| **R9** | `py::reinterpret_borrow<T>` → `nb::borrow<T>`; `py::reinterpret_steal<T>` → `nb::steal<T>` |
| **R10** | `py::none()`/`py::none{}` → `nb::none()` |
| **R11** | `py::module_::import`/`py::module::import` → `nb::module_::import_` |
| **R12** | `def_property_readonly` → `def_prop_ro`; `def_property` → `def_prop_rw`; `def_readonly` → `def_ro`; `def_readwrite` → `def_rw` |
| **R13** | `py::return_value_policy::X` → `nb::rv_policy::X` |
| **R14** | `py::pickle(g, s)` → `.def("__getstate__", g)` + `.def("__setstate__", s)` с in-place конструктором |
| **R15** | `py::init(lambda)` → `.def("__init__", [](T* self, ...){ new (self) T(...); })` |
| **R16** | `py::self` → `nb::self` + `#include <nanobind/operators.h>` |
| **R17** | `py::error_already_set` → `nb::python_error`; `py::cast_error` → `nb::cast_error`; `py::value_error` → `nb::value_error`; `pybind11::index_error` → `nb::index_error` |
| **R18** | `py::typing::Callable<S>` → `nb::typed<nb::callable, S>` |
| **R19** | `py::enum_` → `nb::enum_` |
| **R20** | `py::make_iterator(b,e)` → `nb::make_iterator<policy>(scope, name, b, e)` + `<nanobind/make_iterator.h>`; `py::keep_alive` → `nb::keep_alive` |
| **R21** | удалить мёртвые include (`complex.h`, `stl_bind.h`, `cast.h`, `pytypes.h`, `detail/common.h`, `subinterpreter.h`, `typing.h`) |
| **R22** | `py::len` → `nb::len`; `py::hash` → `nb::hash`; `py::isinstance<T>` → `nb::isinstance<T>`; `py::hasattr` → `nb::hasattr`; `py::print` → `nb::print` (другая сигнатура) |

---

## 1. Сборка и упаковка

- [ ] `CMakeLists.txt` — `cmake_minimum_required` ≥ 3.26; опции `DESBORDANTE_BINDINGS_STABLE_ABI` / `DESBORDANTE_BINDINGS_FREE_THREADED`.
- [ ] `src/python_bindings/CMakeLists.txt` — убрать `CPMAddPackage(pybind11)`; `find_package(Python ... OPTIONAL_COMPONENTS Development.SABIModule)`; `set(NANOBIND_LIB nanobind-static-abi3)`; `nanobind_build_library(${NANOBIND_LIB})`; `nanobind_add_module(${MODULE_NAME} STABLE_ABI NB_STATIC bind_main_classes.cpp bindings.cpp)`; убрать `pybind11::module`, заменить `nanobind-static` на `${NANOBIND_LIB}`; удалить закомментированный `pybind11::embed`.
- [ ] `cmake/desbordante_helpers.cmake` — в `desbordante_add_bind()` `pybind11::pybind11` → `${NANOBIND_LIB}`; обновить комментарий.
- [ ] `pyproject.toml` — `requires`: убрать `pybind11`, добавить `nanobind>=3.1`, `cmake>=3.26`; `[tool.scikit-build] wheel.py-api`; при split mode — `nanobind-backend` в dependencies; синхронизировать `requires-python` и классификаторы.
- [ ] `build.sh` — help-текст флага `--pybind` (по желанию — алиас `--python`).
- [ ] `.clang-format` — `IncludeCategories`: `<nanobind/nanobind.h>` в Priority 1, `nanobind` в группу Priority 3.
- [ ] **Создать** `src/python_bindings/binding_core.h` — общие caster-include'ы, `NB_MAKE_OPAQUE`, helper `ToPyStr`.

## 2. Точка входа

- [ ] `src/python_bindings/bindings.cpp` — `NB_MODULE(desbordante, module)`; удалить `register_module`/`borrow`; `BindDataTypes(module)`; удалить `pybind11::literals`; R1, R2.
- [ ] `src/python_bindings/bind_main_classes.h` — R1, R3, `pybind11::kwargs` → `nanobind::kwargs`.
- [ ] `src/python_bindings/bind_main_classes.cpp` — **исправить `};data` (строка 79)**; R1–R3, R6, R7, R8, R10; `py::register_exception<config::ConfigurationError>(m, "ConfigurationError", PyExc_ValueError)` → `nb::exception<...>(...)`; `kwargs.contains(option_name)` → `kwargs.contains(nb::str(option_name))`; `get_opts` возвращает `nb::object`; R11 (при необходимости), R21.

## 3. `py_util/` — ядро

- [ ] `py_util/bind_primitive.h` — R1–R4, R12; **убрать `py::multiple_inheritance()`** (см. риск R2); `attr("Default") = default_module` оставить; `def_submodule` оставить.
- [ ] `py_util/py_to_any.h` — R1, R2 (`pybind11::handle` → `nb::handle`).
- [ ] `py_util/py_to_any.cpp` — R1, R2, R8, R9, R11, R21; `std::vector<nb::handle>`; `nb::isinstance<nb::tuple/str/iterable/sequence>`; `nb::len`; `nanobind/stl/{vector,string,filesystem,unordered_map,unordered_set,optional,pair}.h`.
- [ ] `py_util/value_to_py.h` — R1, R2 (`pybind11::object` → `nb::object`).
- [ ] `py_util/value_to_py.cpp` — R1, R2, R9, R10, R11; `nb::str` из `std::string` строить как `nb::str(s.c_str(), s.size())`; `std::vector<nb::object>`; `nanobind/stl/vector.h`.
- [ ] `py_util/get_py_type.h` — R1, R2 (`pybind11::tuple` → `nb::tuple`).
- [ ] `py_util/get_py_type.cpp` — R1, R2, R7, R8; `py::type::of<T>()` → `nb::type<T>()`; **`&PyBool_Type`, `&PyLong_Type`, `&PyFloat_Type`, `&PyUnicode_Type`, `&PyList_Type`, `&PyTuple_Type`, `&PySet_Type`, `&PyDict_Type`, `&PyFunction_Type` → рантайм-получение из `builtins`/`types`** (блокер abi3 B1).
- [ ] `py_util/opt_to_py.h` — R1, R2.
- [ ] `py_util/opt_to_py.cpp` — R1, R2, R8; `nanobind/stl/{vector,string,shared_ptr}.h`; `py::cast(boost::any_cast<T>(...))` → `nb::cast(...)`.
- [ ] `py_util/table_serialization.h` — R1, R2, R7.
- [ ] `py_util/table_serialization.cpp` — R1, R2, R7, R8; `nanobind/stl/{string,vector}.h`.
- [ ] `py_util/dataframe_reader.h` — R1, R2, R3 (если требуется), тип `df_iter_` → `nb::iterator`.
- [ ] `py_util/dataframe_reader.cpp` — R1, R2, R8, R9; `py::iterator::sentinel()` → `nb::iterator::sentinel()`; `nb::str` → `std::string` через `nb::cast<std::string>(...)`; `nanobind/stl/{vector,string}.h`.
- [ ] `py_util/create_dataframe_reader.h` — R1, R2.
- [ ] `py_util/create_dataframe_reader.cpp` — R1, R2, R8, R11, R17; `nanobind/stl/string.h`.
- [ ] `py_util/iterable_sequence_stream.h` — R1, R2; `py::iterable` → `nb::iterable`, `py::object iterator_` → `nb::iterator`.
- [ ] `py_util/iterable_sequence_stream.cpp` — R1, R2, R7, R8, R9, R10, R17; `nb::iterator::sentinel()`; `nanobind/stl/vector.h`.
- [ ] `py_util/py_custom_metrics.h` — R1, R2, R5, R8, R21; `nb::object` для хранимых callable.
- [ ] `py_util/py_custom_metrics.cpp` — R1, R2, R4, R5; `nanobind/stl/shared_ptr.h`.
- [ ] `py_util/vector_to_tuple.h` — R1, R2, R7.
- [ ] `py_util/logging.h` — R1, R3.
- [ ] `py_util/logging.cpp` — R1, R2, R8, R10, R11, R17, R21, R22; **удалить subinterpreter-ветку и реестр интерпретаторов** (блокер abi3 B2); `add_object` → `attr(...) = ...`; `nb::gil_scoped_acquire::is_valid()`; `#include <cstdio>` при необходимости.

## 4. Специальные биндинги

- [ ] `ac/bind_ac.cpp` — R1–R4, R6, R7, R10, R12, R13, R14, R16, R22.
- [ ] `ar/bind_ar.cpp` — R1–R4, R6, R7, R12, R13 (`rv_policy::move`), R14, R16, R22.
- [ ] `cfd/bind_cfd.cpp` — R1–R4, R6, R7, R12, R14, R16, R17, R22; `nanobind/stl/optional.h`.
- [ ] `cfd/bind_cfd_verification.cpp` — R1–R3, R12.
- [ ] `cind/bind_cind.cpp` — R1–R4, R6, R7, R12, R14, R22; `nanobind/stl/{vector,string}.h`.
- [ ] `cind/bind_cind_verification.cpp` — R1–R3, R12, R14.
- [ ] `dd/bind_dd_verification.cpp` — R1–R4, R5, R6, R7, R12, R21, R22; `nanobind/stl/{function,shared_ptr,string}.h`.
- [ ] `dd/bind_split.cpp` — R1–R4, R6, R7, R12, R14, R16, R22.
- [ ] `dc/bind_fastadc.cpp` — R1–R4, R5, R6, R7, R13 (`rv_policy::copy`), R14, R22.
- [ ] `dc/bind_dc_verification.cpp` — R1–R3, R21 (удалить `complex.h`, `stl_bind.h`).
- [ ] `fd/bind_fd.cpp` — R1–R4, R5, R6, R7, R8, R12, R14, R22; **проверить `Pyro` (риск R2)**.
- [ ] `fd/bind_afd.cpp` — R1–R4, R5, R6, R7, R8, R14.
- [ ] `gdd/bind_gdd_verification.cpp` — R1–R4, R6, R7, R12, R15 (≈15 кастомных конструкторов), R19, R22; `nanobind/stl/{variant,filesystem,string,vector}.h`.
- [ ] `gspan/bind_gspan.cpp` — R1–R4, R12, R17, R20; `nanobind/stl/unordered_set.h`.
- [ ] `ind/bind_ind.cpp` — R1–R4, R6, R7, R12, R14, R22.
- [ ] `md/bind_md.cpp` — R1–R5, R6, R7, R8, R12, R14, R15, R18, R21, R22.
- [ ] `md/bind_md_verification.cpp` — R1–R5, R8, R12, R15.
- [ ] `md/object_similarity_measure.h` — R1, R2, R8; `nanobind/stl/vector.h`; сохранить TODO про no-GIL/subinterpreter.
- [ ] `nar/bind_nar.cpp` — R1–R5, R6, R7, R8, R12, R14, R22.
- [ ] `nd/bind_nd.cpp` — R1–R4, R6, R8, R12, R22.
- [ ] `od/bind_od.cpp` — R1–R4, R6, R7, R12, R14, R22.
- [ ] `pac/bind_domains.h` — R1, R2, R8; `pybind11::object` → `nb::object`.
- [ ] `pac/bind_domains.cpp` — R1–R5, R6, R12.
- [ ] `pac/bind_pac.cpp` — R1–R4, R6, R7, R12, R14, R16, R21, R22.
- [ ] `sfd/bind_sfd.cpp` — R1–R5, R6, R7, R8, R12, R13 (`rv_policy::copy`), R14, R22.
- [ ] `statistics/bind_statistics.cpp` — R1, R2, R3, R6, R22; **переписать `type_caster<algos::Statistic>` на `nanobind::detail::type_caster`/`NB_TYPE_CASTER`** (`from_python`/`from_cpp` noexcept); `Py_RETURN_NONE` → `none().release()`; `nanobind/stl/{vector,unordered_set,string}.h`.
- [ ] `statistics/bind_statistics.h` — R1, R3.
- [ ] `ucc/bind_ucc.cpp` — R1–R5, R6, R7, R8, R12, R14, R22; **проверить `PyroUCC` (риск R2)**.
- [ ] `data/bind_data_types.cpp` — уже на nanobind; проверить `__setstate__` (разрушение старого значения перед placement new) и актуальность комментариев.
- [ ] `data/bind_data_types.h` — уже на nanobind; R3.

## 5. Механические биндинги `.cpp` (только R1–R3 + встречающиеся R4/R6/R7/R8/R10/R12/R13/R17/R21/R22)

- [ ] `afd_metric/bind_afd_metric_calculation.cpp`
- [ ] `ar/bind_ar_verification.cpp`
- [ ] `dynamic/bind_dynamic_fd_verification.cpp`
- [ ] `fd/bind_fd_verification.cpp`
- [ ] `fem/bind_fem.cpp`
- [ ] `gfd/bind_gfd.cpp`
- [ ] `gfd/bind_gfd_verification.cpp`
- [ ] `ind/bind_ind_verification.cpp`
- [ ] `mfd/bind_mfd_verification.cpp`
- [ ] `nd/bind_nd_verification.cpp`
- [ ] `od/bind_od_verification.cpp`
- [ ] `pac/bind_pac_verification.cpp`
- [ ] `pfd/bind_pfd_verification.cpp`
- [ ] `sd/bind_sd_verification.cpp`
- [ ] `ucc/bind_ucc_verification.cpp`
- [ ] `dd/create_dd.cpp` — **без изменений** (нет Python API).

## 6. Механические заголовки `.h` (R1 + R3)

- [ ] `ac/bind_ac.h`, `afd_metric/bind_afd_metric_calculation.h`, `ar/bind_ar.h`, `ar/bind_ar_verification.h`
- [ ] `cfd/bind_cfd.h`, `cfd/bind_cfd_verification.h`, `cind/bind_cind.h`, `cind/bind_cind_verification.h`
- [ ] `dc/bind_dc_verification.h`, `dc/bind_fastadc.h`, `dd/bind_dd_verification.h`, `dd/bind_split.h`
- [ ] `dynamic/bind_dynamic_fd_verification.h`, `fd/bind_afd.h`, `fd/bind_fd.h`, `fd/bind_fd_verification.h`
- [ ] `fem/bind_fem.h`, `gdd/bind_gdd_verification.h`, `gfd/bind_gfd.h`, `gfd/bind_gfd_verification.h`
- [ ] `gspan/bind_gspan.h`, `ind/bind_ind.h`, `ind/bind_ind_verification.h`, `md/bind_md.h`, `md/bind_md_verification.h`
- [ ] `mfd/bind_mfd_verification.h`, `nar/bind_nar.h`, `nd/bind_nd.h`, `nd/bind_nd_verification.h`
- [ ] `od/bind_od.h`, `od/bind_od_verification.h`, `pac/bind_pac.h`, `pac/bind_pac_verification.h`
- [ ] `pfd/bind_pfd_verification.h`, `sd/bind_sd_verification.h`, `sfd/bind_sfd.h`, `ucc/bind_ucc.h`, `ucc/bind_ucc_verification.h`
- [ ] `py_util/create_dataframe_reader.h`, `dataframe_reader.h`, `get_py_type.h`, `iterable_sequence_stream.h`, `logging.h`, `opt_to_py.h`, `py_custom_metrics.h`, `py_to_any.h`, `table_serialization.h`, `value_to_py.h`, `vector_to_tuple.h`
- [ ] `dd/create_dd.h` — **без изменений**.

## 7. Python-тесты

- [ ] `src/python_bindings/test_bindings.py` — добавить проверки: `pickle`/`copy` round-trip (`FD`, `RawCFD`, `CIND`), `ConfigurationError` ⊂ `ValueError`, итератор gspan после удаления владельца, `get_opts()`.
- [ ] `src/python_bindings/test_pybind_data_stats.py` → `test_data_stats.py` (`git mv`), обновить вызов в CI.

## 8. CI

- [ ] `.github/workflows/wheel.yml` — `CIBW_BUILD` на минимальную версию (`cp312-*`, вариант A) или split (`cp310-*`, вариант B); при варианте C — два набора job'ов; выровнять версию cibuildwheel между генерацией матрицы и сборкой (сейчас 4.1.0 против v3.2.1).
- [ ] `.github/workflows/bindings-tests.yml` — новое имя `test_data_stats.py`; минимальная версия Python; шаг проверки `.abi3.` в имени модуля.
- [ ] `.github/workflows/examples-tests.yml` — `python-version` = минимальная поддерживаемая.
- [ ] `.github/workflows/check-codestyle.yml` — убедиться, что `binding_core.h` попадает в проверку форматирования.

## 9. Документация

- [ ] `README.md` — строки 92 и 283+ (pybind11 → nanobind/Stable ABI, версии Python, флаг сборки).
- [ ] `README_PYPI.md` — строка 73.
- [ ] Release notes (вне репозитория/CHANGELOG, если есть) — отказ от мультиинтерпретаторов, изменение семантики `nb::enum_` (`int(member)`), требования к Python.

---

## 10. Команды для самопроверки после миграции

```bash
# не осталось pybind11 в биндингах
grep -rn "pybind11\|PYBIND11" src/python_bindings/ || echo "clean"

# не осталось member-.cast<T>()
grep -rnE '\.cast<' src/python_bindings/ || echo "clean"

# не осталось запрещённых type-object'ов CPython (важно для abi3)
grep -rn "&Py[A-Za-z]*_Type" src/python_bindings/ || echo "clean"

# модуль действительно abi3 (после сборки)
python -c "import pathlib, desbordante; print(pathlib.Path(desbordante.__file__).name)"
```

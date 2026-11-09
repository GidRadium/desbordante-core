# Release notes

## Unreleased: pybind11 → nanobind migration

The Python bindings now use nanobind. The public module layout, class names,
pattern protocols and `_get_option_type` names remain unchanged; `_get_option_type`
is checked against the T01 pybind11 baseline inventory for every algorithm option.
T06 also filled the option types that the pybind11 baseline could not report; their
old names are kept in the inventory as `previously_unsupported_option_types`.

Python-visible behavior differences:

* **Complete option type map.** `_get_option_type` now resolves every option,
  including the eight types that raised `RuntimeError` in the pybind11 baseline:
  `bin_operation`, the AFD-metric and CIND enums map to `str`; `sequence` maps
  to `list` (paths are still accepted as input); `metrics_map` maps to
  `dict[str, Metric]`. `get_opts` now skips all input-stream options by type
  (`table`, `tables`, `left_table`, `right_table`, `sequence`) and can return
  `column_matches` and `metrics_map` values.
* **Shared-pointer wrapper identity.** nanobind has no holder template argument:
  a live Python wrapper is cached for its C++ address, so a round-trip such as
  `get_opts()` returns the same object and `is`/`id()` work while it is alive.
  After the last wrapper is released, a later conversion may create a new
  wrapper for the same C++ object (an `id()` value may even be reused), so
  object identity is not part of the API contract; compare data instead.
* **bool is strict.** Boolean options and boolean arguments accept only `True`
  and `False`. pybind11 also accepted `1`/`0` (and `None` as `False`) in several
  places; nanobind rejects them with `ConfigurationError` (`TypeError` for
  constructor arguments). This applies to `load_data`, `execute`, `_set_option`,
  the `table=(path, separator, has_header)` tuple, and `desbordante.md`
  constructor flags such as `symmetrical` and `equality_is_max`.
* **`None` for `_set_option` still resets an option.** Both
  `_set_option(name, None)` and `_set_option(name)` restore the default value
  (the `option_value` argument defaults to `nb::none()`). Passing `None` as a
  bool option through `load_data`/`execute` is *not* a reset and is rejected.
* **Optional and callable arguments.** `std::optional<T>` parameters accept
  `None` without an extra annotation, e.g. `desbordante.cfd.Item(0, None)` and
  the tuple form of `desbordante.cfd.CFD`. MD column-match
  `column_functions` accepts `None`, a callable, or a pair of callables.
  `pick_lhs_indices` remains a callable-only argument and rejects `None`.
* **Enums are real `enum.Enum` classes.** `desbordante.gdd.DistanceMetric` and
  `desbordante.gdd.CmpOp` keep their member names, integer values, `repr`, and
  `export_values` aliases. `isinstance(member, enum.Enum)` is now true, while
  `int(member)` no longer works: use `member.value`. Members are not equal to
  raw integers (same as the pybind11 baseline).
* **Signatures and docstrings.** nanobind renders overloads lazily.
  `help(Class)` / `Class.__init__.__doc__` contain the detailed overloads,
  keyword-only markers and defaults, including `pick_lhs_indices = pick_all`.
  `inspect.signature(Class)` returns the generic `(*args, **kwargs)`, and
  `inspect.signature(Class.__init__)` raises `ValueError` because the
  constructor is a builtin; inspect the docstring for the real signature.

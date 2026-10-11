#pragma once

#include <nanobind/nanobind.h>

namespace python_bindings {
void BindDataTypes(nanobind::module_& main_module);
}  // namespace python_bindings

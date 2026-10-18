#pragma once

#include "python_bindings/binding_core.h"

#include <typeindex>

namespace python_bindings {
nb::tuple GetPyType(std::type_index type_index);
}  // namespace python_bindings

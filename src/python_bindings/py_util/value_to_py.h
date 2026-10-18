#pragma once

#include "python_bindings/binding_core.h"

#include <cstddef>
#include <vector>

#include "core/model/types/type.h"

namespace python_bindings {
nb::object ValueToPy(model::Type const* type, std::byte const* value);
std::vector<nb::object> ValuesToPy(std::vector<model::Type const*> const& types,
                                   std::vector<std::byte const*> const& values);
}  // namespace python_bindings

#pragma once

#include "python_bindings/binding_core.h"

#include <boost/any.hpp>

namespace python_bindings {
[[nodiscard]] boost::any PyToAny(std::string_view option_name, std::type_index index,
                                 nb::handle obj);
}  // namespace python_bindings

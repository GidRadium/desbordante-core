#pragma once

#include "python_bindings/binding_core.h"

#include <typeindex>

#include <boost/any.hpp>

namespace python_bindings {
nb::object OptToPy(std::type_index type, boost::any val);
}  // namespace python_bindings

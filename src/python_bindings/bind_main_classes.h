#pragma once

#include "python_bindings/binding_core.h"

#include "core/algorithms/algorithm.h"

namespace configure_algorithm {
void ConfigureAlgo(algos::Algorithm& algorithm, nb::dict const& kwargs);
}

namespace python_bindings {
void BindMainClasses(nb::module_& main_module);
}  // namespace python_bindings

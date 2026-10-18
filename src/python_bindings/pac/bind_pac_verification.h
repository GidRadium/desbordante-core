#pragma once

#include "python_bindings/binding_core.h"

namespace python_bindings {
void BindPACVerification(nb::module_& main_module);
void BindDomainPACVerification(nb::module_& pac_verification_module);
}  // namespace python_bindings

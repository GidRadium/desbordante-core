#include "python_bindings/gfd/bind_gfd_verification.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/gfd/gfd_validator/egfd_validator.h"
#include "core/algorithms/gfd/gfd_validator/gfd_validator.h"
#include "core/algorithms/gfd/gfd_validator/naivegfd_validator.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindGfdVerification(nb::module_& main_module) {
    using namespace algos;

    auto gfd_module = main_module.def_submodule("gfd_verification");

    nb::class_<model::Gfd>(gfd_module, "Gfd").def("__str__", &model::Gfd::ToString);

    BindPrimitive<GfdValidator, EGfdValidator, NaiveGfdValidator>(
            gfd_module, &GfdHandler::GfdList, "GfdAlgorithm", "get_gfds",
            {"GfdValid", "EGfdValid", "NaiveGfdValid"});
}
}  // namespace python_bindings

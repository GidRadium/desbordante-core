#include "python_bindings/binding_core.h"

#include "core/algorithms/sd/sd_verifier/sd_verifier.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}

namespace python_bindings {
void BindSDVerification(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::sd_verifier;

    auto sd_verification_module = main_module.def_submodule("sd_verification");

    nb::class_<SDDeletion>(sd_verification_module, "SDDeletion")
            .def_ro("row_idx", &SDDeletion::row_idx);

    nb::class_<SDInsertion>(sd_verification_module, "SDInsertion")
            .def_ro("left_row_idx", &SDInsertion::left_row_idx)
            .def_ro("right_row_idx", &SDInsertion::right_row_idx)
            .def_ro("val_left", &SDInsertion::val_left)
            .def_ro("val_right", &SDInsertion::val_right)
            .def_ro("min_insertions", &SDInsertion::min_insertions)
            .def_ro("max_insertions", &SDInsertion::max_insertions);

    BindPrimitiveNoBase<SDVerifier>(sd_verification_module, "SDVerifier")
            .def("get_ops", &SDVerifier::GetOPS)
            .def("get_violations", &SDVerifier::GetViolations)
            .def("get_error", &SDVerifier::GetError)
            .def("get_confidence", &SDVerifier::GetConfidence)
            .def("holds", &SDVerifier::Holds);

    main_module.attr("sd_verification") = sd_verification_module;
}
}  // namespace python_bindings

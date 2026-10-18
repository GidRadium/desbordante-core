#include "python_bindings/fd/bind_fd_verification.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/fd/fd_verifier/fd_verifier.h"
#include "core/algorithms/fd/fd_verifier/highlight.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindFdVerification(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::fd_verifier;

    auto fd_verification_module = main_module.def_submodule("fd_verification");
    nb::class_<Highlight>(fd_verification_module, "Highlight")
            .def_prop_ro("cluster", &Highlight::GetCluster)
            .def_prop_ro("num_distinct_rhs_values", &Highlight::GetNumDistinctRhsValues)
            .def_prop_ro("most_frequent_rhs_value_proportion",
                         &Highlight::GetMostFrequentRhsValueProportion);
    BindPrimitiveNoBase<FDVerifier>(fd_verification_module, "FDVerifier")
            .def("fd_holds", &FDVerifier::FDHolds)
            .def("get_error", &FDVerifier::GetError)
            .def("get_num_error_clusters", &FDVerifier::GetNumErrorClusters)
            .def("get_num_error_rows", &FDVerifier::GetNumErrorRows)
            .def("get_highlights", &FDVerifier::GetHighlights);

    main_module.attr("afd_verification") = fd_verification_module;
}
}  // namespace python_bindings

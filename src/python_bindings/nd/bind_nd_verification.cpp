#include "python_bindings/nd/bind_nd_verification.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/nd/nd_verifier/nd_verifier.h"
#include "core/algorithms/nd/nd_verifier/util/highlight.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindNdVerification(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::nd_verifier;
    using namespace algos::nd_verifier::util;

    auto nd_verification_module = main_module.def_submodule("nd_verification");
    nb::class_<Highlight>(nd_verification_module, "Highlight")
            .def("__str__", &Highlight::ToValuesString)
            .def("to_short_string", &Highlight::ToIndicesString)
            .def("to_long_string", &Highlight::ToValuesString)
            .def("get_occurrences_indices", &Highlight::GetOccurrencesIndices)
            .def_prop_ro("occurrences_number", &Highlight::GetOccurrencesNumber)
            .def_prop_ro("lhs_value", &Highlight::GetLhsValue)
            .def("get_rhs_values", &Highlight::GetRhsValues)
            .def_prop_ro("distinct_rhs_values_number", &Highlight::GetDistinctRhsValuesNumber)
            .def("get_most_frequent_rhs_value_indices", &Highlight::GetMostFrequentRhsValueIndices)
            .def("get_most_frequent_rhs_values", &Highlight::GetMostFrequentRhsValues);

    BindPrimitiveNoBase<NDVerifier>(nd_verification_module, "NDVerifier")
            .def_prop_ro("nd_holds", &NDVerifier::NDHolds)
            .def_prop_ro("highlights", &NDVerifier::GetHighlights)
            .def_prop_ro("global_min_weight", &NDVerifier::GetGlobalMinWeight)
            .def_prop_ro("real_weight", &NDVerifier::GetRealWeight)
            .def("get_lhs_frequencies", &NDVerifier::GetLhsFrequencies)
            .def("get_rhs_frequencies", &NDVerifier::GetRhsFrequencies);
}
}  // namespace python_bindings

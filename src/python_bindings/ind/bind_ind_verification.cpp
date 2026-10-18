#include "python_bindings/ind/bind_ind_verification.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/ind/ind.h"
#include "core/algorithms/ind/ind_verifier/ind_verifier.h"
#include "python_bindings/bind_main_classes.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindIndVerification(nb::module_& main_module) {
    using namespace algos;

    auto ind_verification_module = main_module.def_submodule("ind_verification");

    BindPrimitiveNoBase<INDVerifier>(ind_verification_module, "INDVerifier")
            .def("validate_ind",
                 [](INDVerifier& verifier, model::IND& ind) {
                     nb::dict kwargs;
                     kwargs["lhs_indices"] = nb::cast(ind.GetLhs().GetColumnIndices());
                     kwargs["rhs_indices"] = nb::cast(ind.GetRhs().GetColumnIndices());
                     configure_algorithm::ConfigureAlgo(verifier, kwargs);
                     verifier.Execute();
                 })
            .def("ind_holds", &INDVerifier::Holds)
            .def("get_error", &INDVerifier::GetError)
            .def("get_violating_rows_count", &INDVerifier::GetViolatingRowsCount)
            .def("get_violating_clusters", &INDVerifier::GetViolatingClusters)
            .def("get_violating_clusters_count", &INDVerifier::GetViolatingClustersCount);

    main_module.attr("aind_verification") = ind_verification_module;
}
}  // namespace python_bindings

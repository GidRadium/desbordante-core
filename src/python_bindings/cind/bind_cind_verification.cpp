#include "bind_cind_verification.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/cind/cind_verifier/cind_verifier.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace nb = nanobind;

namespace python_bindings {

void BindCindVerification(nb::module_& main_module) {
    using algos::cind::CINDVerifier;

    nb::module_ m = main_module.def_submodule("cind_verification");

    using ViolatingCluster = CINDVerifier::ViolatingCluster;

    nb::class_<ViolatingCluster>(m, "ViolatingCluster")
            .def_ro("basket_rows", &ViolatingCluster::basket_rows)
            .def_ro("violating_rows", &ViolatingCluster::violating_rows)
            .def("__getstate__",
                 [](ViolatingCluster const& vc) {
                     return nb::make_tuple(vc.basket_rows, vc.violating_rows);
                 })
            .def("__setstate__", [](ViolatingCluster& vc, nb::tuple st) {
                if (st.size() != 2) {
                    throw std::runtime_error("Invalid state for ViolatingCluster pickle!");
                }
                new (&vc)
                        ViolatingCluster{.basket_rows = nb::cast<CINDVerifier::Cluster>(st[0]),
                                         .violating_rows = nb::cast<CINDVerifier::Cluster>(st[1])};
            });

    BindPrimitiveNoBase<CINDVerifier>(m, "CINDVerifier")
            .def("holds", &CINDVerifier::Holds)
            .def("get_real_validity", &CINDVerifier::GetRealValidity)
            .def("get_real_completeness", &CINDVerifier::GetRealCompleteness)
            .def("get_violating_rows_count", &CINDVerifier::GetViolatingRowsCount)
            .def("get_violating_clusters_count", &CINDVerifier::GetViolatingClustersCount)
            .def("get_violating_clusters", &CINDVerifier::GetViolatingClusters)
            .def("get_supporting_baskets", &CINDVerifier::GetSupportingBaskets)
            .def("get_included_supporting_baskets", &CINDVerifier::GetIncludedSupportingBaskets)
            .def("get_included_baskets_total", &CINDVerifier::GetIncludedBasketsTotal);
}

}  // namespace python_bindings

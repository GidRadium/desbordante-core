#include "python_bindings/ar/bind_ar.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/ar/apriori/apriori.h"
#include "core/algorithms/ar/ar.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindAr(nb::module_& main_module) {
    using namespace algos;
    using model::ArIDs;
    using model::ARStrings;

    auto ar_module = main_module.def_submodule("ar");
    nb::class_<ARStrings>(ar_module, "ARStrings")
            .def("__str__", &ARStrings::ToString)
            .def_ro("left", &ARStrings::left)
            .def_ro("right", &ARStrings::right)
            .def_ro("confidence", &ARStrings::confidence)
            .def_ro("support", &ARStrings::support)
            .def("__getstate__",
                 [](ARStrings const& ars) {
                     std::vector<std::string> left_vec(ars.left.begin(), ars.left.end());
                     std::vector<std::string> right_vec(ars.right.begin(), ars.right.end());
                     return nb::make_tuple(std::move(left_vec), std::move(right_vec),
                                           ars.confidence, ars.support);
                 })
            .def("__setstate__", [](ARStrings& ars, nb::tuple t) {
                if (t.size() != 4) {
                    throw std::runtime_error("Invalid state for ARStrings pickle!");
                }
                auto left_vec = nb::cast<std::vector<std::string>>(t[0]);
                auto right_vec = nb::cast<std::vector<std::string>>(t[1]);
                double conf = nb::cast<double>(t[2]);
                double supp = nb::cast<double>(t[3]);
                new (&ars) ARStrings(std::move(left_vec), std::move(right_vec), conf, supp);
            });

    nb::class_<ArIDs>(ar_module, "ArIDs")
            .def_ro("left", &ArIDs::left)
            .def_ro("right", &ArIDs::right)
            .def_ro("confidence", &ArIDs::confidence)
            .def_ro("support", &ArIDs::support)
            .def("__getstate__",
                 [](ArIDs const& arids) {
                     return nb::make_tuple(arids.left, arids.right, arids.confidence,
                                           arids.support);
                 })
            .def("__setstate__", [](ArIDs& arids, nb::tuple t) {
                if (t.size() != 4) {
                    throw std::runtime_error("Invalid state for ArIDs pickle!");
                }
                auto left = nb::cast<std::vector<unsigned>>(t[0]);
                auto right = nb::cast<std::vector<unsigned>>(t[1]);
                double conf = nb::cast<double>(t[2]);
                double supp = nb::cast<double>(t[3]);
                new (&arids) ArIDs(std::move(left), std::move(right), conf, supp);
            });

    nb::class_<ARAlgorithm, Algorithm>(ar_module, "ArAlgorithm")
            .def("get_ars", &ARAlgorithm::GetArStringsList, nb::rv_policy::move)
            .def("get_itemnames", &ARAlgorithm::GetItemNamesVector)
            .def("get_ar_ids", &ARAlgorithm::GetArIDsList);

    auto algos_module = ar_module.def_submodule("algorithms");
    auto default_algorithm =
            detail::RegisterAlgorithm<Apriori, ARAlgorithm>(algos_module, "Apriori");
    algos_module.attr("Default") = default_algorithm;

    // Perhaps in the future there will be a need for:
    // default_algorithm.def("get_frequent_list", &Apriori::GetFrequentList);
}
}  // namespace python_bindings

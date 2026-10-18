#include "python_bindings/fd/bind_afd.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/fd/afd.h"
#include "core/algorithms/fd/afd_algorithm.h"
#include "core/algorithms/fd/tane/pfdtane.h"
#include "core/algorithms/fd/tane/tane.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/table_serialization.h"

namespace python_bindings {
void BindAfd(nb::module_& main_module) {
    using namespace algos;

    auto afd_module = main_module.def_submodule("afd");
    nb::class_<AFD, FD>(afd_module, "AFD")
            .def("get_threshold", &AFD::GetThreshold)
            .def("__getstate__",
                 [](AFD const& afd) {
                     nb::tuple schema_state =
                             table_serialization::SerializeRelationalSchema(afd.GetSchema().get());
                     nb::tuple lhs_state = table_serialization::SerializeVertical(afd.GetLhs());
                     nb::tuple rhs_state = table_serialization::SerializeColumn(afd.GetRhs());
                     return nb::make_tuple(std::move(schema_state), std::move(lhs_state),
                                           std::move(rhs_state), afd.GetThreshold());
                 })
            .def("__setstate__", [](AFD& afd, nb::tuple t) {
                if (t.size() != 4) throw std::runtime_error("Invalid state for AFD pickle!");
                std::shared_ptr<RelationalSchema const> schema =
                        table_serialization::DeserializeRelationalSchema(nb::cast<nb::tuple>(t[0]));
                Vertical lhs = table_serialization::DeserializeVertical(nb::cast<nb::tuple>(t[1]),
                                                                        schema.get());
                Column rhs = table_serialization::DeserializeColumn(nb::cast<nb::tuple>(t[2]),
                                                                    schema.get());
                long double threshold = nb::cast<long double>(t[3]);
                new (&afd) AFD(lhs, rhs, threshold, std::move(schema));
            });

    static constexpr auto kTaneName = "Tane";
    static constexpr auto kPFDTaneName = "PFDTane";
    static constexpr auto kPyroName = "Pyro";
    auto afd_algos_module =
            BindPrimitive<Tane, PFDTane>(afd_module, &AFDAlgorithm::SortedAfdList, "AfdAlgorithm",
                                         "get_fds", {kTaneName, kPFDTaneName});

    // Pyro is registered as an FdAlgorithm in bind_fd.cpp: it discovers approximate FDs via a
    // g1 threshold but returns plain FD objects (no threshold accessor), unlike Tane/PFDTane,
    // which return AFD objects. Re-registering the Pyro class here would abort at import time
    // (nanobind does not allow the same C++ type to be bound twice), so it is aliased instead:
    // this reuses the single Python class already created in bind_fd.cpp and only exposes it
    // under an additional path, so that it is discoverable alongside the other AFD-discovery
    // algorithms.
    afd_algos_module.attr(kPyroName) = main_module.attr("fd").attr("algorithms").attr(kPyroName);

    auto define_submodule = [&afd_algos_module, &main_module](char const* name,
                                                              std::vector<char const*> algorithms) {
        auto algos_module = main_module.def_submodule(name).def_submodule("algorithms");
        for (auto algo_name : algorithms) {
            algos_module.attr(algo_name) = afd_algos_module.attr(algo_name);
        }
        algos_module.attr("Default") = algos_module.attr(algorithms.front());
    };

    define_submodule("pfd", {kPFDTaneName});
}
}  // namespace python_bindings

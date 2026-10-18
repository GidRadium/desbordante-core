#include "python_bindings/fem/bind_fem.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/fem/afem/afem.h"
#include "core/algorithms/fem/maxfem/maxfem.h"
#include "core/algorithms/fem/tke/tke.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace python_bindings {

namespace {
namespace nb = nanobind;
}

void BindFem(nb::module_& main_module) {
    auto fem_module = main_module.def_submodule("fem");

    nb::class_<algos::FEMAlgorithm, algos::Algorithm>(fem_module, "FEMAlgorithm");

    detail::RegisterAlgorithm<algos::maxfem::MaxFEM, algos::FEMAlgorithm>(fem_module, "MaxFEM")
            .def("get_max_frequent_episodes", &algos::maxfem::MaxFEM::GetMaxFrequentEpisodes);

    detail::RegisterAlgorithm<algos::afem::AFEM, algos::FEMAlgorithm>(fem_module, "AFEM")
            .def("get_frequent_episodes", &algos::afem::AFEM::GetFrequentEpisodes);

    detail::RegisterAlgorithm<algos::tke::TKE, algos::FEMAlgorithm>(fem_module, "TKE")
            .def("get_top_k_frequent_episodes", &algos::tke::TKE::GetTopKFrequentEpisodes);
}

}  // namespace python_bindings

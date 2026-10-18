#include "python_bindings/mfd/bind_mfd_verification.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/metric/highlight.h"
#include "core/algorithms/metric/metric_verifier.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindMfdVerification(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::metric;

    auto mfd_module = main_module.def_submodule("mfd_verification");
    nb::class_<Highlight>(mfd_module, "Highlight")
            .def("to_tuple", &Highlight::ToTuple)
            .def_ro("data_index", &Highlight::data_index)
            .def_ro("furthest_data_index", &Highlight::furthest_data_index)
            .def_ro("max_distance", &Highlight::max_distance);
    BindPrimitiveNoBase<MetricVerifier>(mfd_module, "MetricVerifier")
            .def("get_highlights", &MetricVerifier::GetHighlights)
            .def("mfd_holds", &MetricVerifier::GetResult);
}
}  // namespace python_bindings

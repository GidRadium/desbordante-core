#include "python_bindings/afd_metric/bind_afd_metric_calculation.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/fd/afd_metric/afd_metric_calculator.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
using namespace nb::literals;

void BindAfdMetricCalculation(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::afd_metric_calculator;

    auto afd_metric_module = main_module.def_submodule("afd_metric_calculation");
    BindPrimitiveNoBase<AFDMetricCalculator>(afd_metric_module, "AFDMetricCalculator")
            .def("get_result", &AFDMetricCalculator::GetResult);
}
}  // namespace python_bindings

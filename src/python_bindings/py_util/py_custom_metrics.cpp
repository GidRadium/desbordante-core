#include "python_bindings/py_util/py_custom_metrics.h"

#include "python_bindings/binding_core.h"

#include <memory>

#include "core/util/custom_metric/custom_metric.h"
#include "core/util/custom_metric/custom_vector_metric.h"

namespace nb = nanobind;

namespace python_bindings {
void BindCustomMetrics(nb::module_& main_module) {
    auto metrics_module = main_module.def_submodule("metrics");

    // These classes are used only in get_opts
    nb::class_<util::ICustomMetric>(metrics_module, "CustomMetric");
    nb::class_<util::ICustomVectorMetric>(metrics_module, "CustomVectorMetrics");
}
}  // namespace python_bindings

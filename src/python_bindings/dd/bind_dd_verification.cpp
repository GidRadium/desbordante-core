#include "python_bindings/dd/bind_dd_verification.h"

#include "python_bindings/binding_core.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "core/algorithms/dd/dd.h"
#include "core/algorithms/dd/dd_verifier/FuncMetric.h"
#include "core/algorithms/dd/dd_verifier/Metric.h"
#include "core/algorithms/dd/dd_verifier/dd_verifier.h"
#include "core/algorithms/dd/dd_verifier/highlight.h"
#include "python_bindings/dd/create_dd.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace python_bindings {
namespace nb = nanobind;

void BindDDVerification(nb::module_& main_module) {
    using namespace algos;
    auto dd_verification_module = main_module.def_submodule("dd_verification");
    nb::class_<model::DFStringConstraint>(dd_verification_module, "DF")
            .def(nb::new_(&util::dd::CreateDf))
            .def("__str__", &model::DFStringConstraint::ToString)
            .def_prop_ro("attribute_name",
                         [](model::DFStringConstraint const& DF) { return DF.column_name; })
            .def_prop_ro(
                    "lower_bound",
                    [](model::DFStringConstraint const& DF) { return DF.constraint.lower_bound; })
            .def_prop_ro(
                    "upper_bound",
                    [](model::DFStringConstraint const& DF) { return DF.constraint.upper_bound; })
            .def(nb::self == nb::self)
            .def(nb::self != nb::self)
            .def("__hash__",
                 [](model::DFStringConstraint const& dd) {
                     return nb::hash(ToPyStr(dd.ToString()));
                 })
            .def("to_json", &model::DFStringConstraint::ToJSON);
    nb::class_<Highlight>(dd_verification_module, "Highlight")
            .def_prop_ro("attribute_index", &Highlight::GetAttributeIndex)
            .def_prop_ro("distance", &Highlight::GetDistance)
            .def_prop_ro("pair_rows", &Highlight::GetPairRows);
    nb::class_<Metric>(dd_verification_module, "Metric");
    nb::class_<FuncMetric<int>, Metric>(dd_verification_module, "MetricInt")
            .def(nb::init<std::function<double(int, int)>>());
    nb::class_<FuncMetric<std::string>, Metric>(dd_verification_module, "MetricStr")
            .def(nb::init<std::function<double(std::string, std::string)>>());
    nb::class_<FuncMetric<double>, Metric>(dd_verification_module, "MetricDouble")
            .def(nb::init<std::function<double(double, double)>>());
    dd_verification_module.def(
            "make_metric_ptr", [](nb::tuple metric_tuple) -> std::shared_ptr<Metric> {
                if (nb::len(metric_tuple) != 2) {
                    throw std::runtime_error("Input must be a tuple of (function, type_string)");
                }

                std::string type_tag = nb::cast<std::string>(metric_tuple[1]);

                if (type_tag == "int") {
                    return std::make_shared<FuncMetric<int>>(
                            nb::cast<std::function<double(int, int)>>(metric_tuple[0]));
                } else if (type_tag == "double") {
                    return std::make_shared<FuncMetric<double>>(
                            nb::cast<std::function<double(double, double)>>(metric_tuple[0]));
                } else if (type_tag == "string") {
                    return std::make_shared<FuncMetric<std::string>>(
                            nb::cast<std::function<double(std::string, std::string)>>(
                                    metric_tuple[0]));
                } else {
                    throw std::runtime_error(
                            "Unsupported type tag. Use 'int', 'double', or 'string'.");
                }
            });
    BindPrimitiveNoBase<dd::DDVerifier>(dd_verification_module, "DDVerifier")
            .def("dd_holds", &dd::DDVerifier::DDHolds)
            .def("get_error", &dd::DDVerifier::GetError)
            .def("get_num_error_pairs", &dd::DDVerifier::GetNumErrorRhs)
            .def("get_highlights", &dd::DDVerifier::GetHighlights);
}
}  // namespace python_bindings

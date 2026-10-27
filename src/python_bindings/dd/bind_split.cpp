#include "python_bindings/dd/bind_split.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/dd/dd.h"
#include "core/algorithms/dd/split/split.h"
#include "python_bindings/dd/create_dd.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindSplit(nb::module_& main_module) {
    using namespace algos;
    using model::DDString;
    using model::DFStringConstraint;

    auto dd_module = main_module.def_submodule("dd");
    nb::class_<model::DDString>(dd_module, "DD")
            .def(nb::new_(&util::dd::CreateDd))
            .def("__str__", &model::DDString::ToString)
            .def("__repr__", &model::DDString::ToString)
            .def("__getstate__",
                 [](model::DDString const& dd_str) {
                     std::vector<nb::tuple> left_vec;
                     for (model::DFStringConstraint const& c : dd_str.left) {
                         left_vec.push_back(nb::make_tuple(c.column_name, c.constraint.lower_bound,
                                                           c.constraint.upper_bound));
                     }
                     std::vector<nb::tuple> right_vec;
                     for (model::DFStringConstraint const& c : dd_str.right) {
                         right_vec.push_back(nb::make_tuple(c.column_name, c.constraint.lower_bound,
                                                            c.constraint.upper_bound));
                     }
                     return nb::make_tuple(std::move(left_vec), std::move(right_vec));
                 })
            .def("__setstate__",
                 [](model::DDString& dd_str, nb::tuple t) {
                     if (t.size() != 2) {
                         throw std::runtime_error("Invalid state for DDString pickle!");
                     }
                     auto left_vec = nb::cast<std::vector<nb::tuple>>(t[0]);
                     auto right_vec = nb::cast<std::vector<nb::tuple>>(t[1]);
                     auto make_constraints = [](std::vector<nb::tuple> const& state) {
                         std::list<model::DFStringConstraint> constraints;
                         for (auto const& item : state) {
                             if (item.size() != 3) {
                                 throw std::runtime_error("Invalid DDString constraint pickle!");
                             }
                             std::string col_name = nb::cast<std::string>(item[0]);
                             double lower_bound = nb::cast<double>(item[1]);
                             double upper_bound = nb::cast<double>(item[2]);
                             constraints.push_back(model::DFStringConstraint(
                                     std::move(col_name), lower_bound, upper_bound));
                         }
                         return constraints;
                     };
                     std::list<model::DFStringConstraint> left_list = make_constraints(left_vec);
                     std::list<model::DFStringConstraint> right_list = make_constraints(right_vec);
                     new (&dd_str) model::DDString{std::move(left_list), std::move(right_list)};
                 })
            .def(nb::self == nb::self)
            .def(nb::self != nb::self)
            .def("__hash__", [](DDString const& dd) { return nb::hash(ToPyStr(dd.ToString())); })
            .def("to_json", &model::DDString::ToJSON);
    BindPrimitiveNoBase<dd::Split>(dd_module, "Split").def("get_dds", &dd::Split::GetDDStringList);
}
}  // namespace python_bindings

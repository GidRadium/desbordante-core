// bind_cind.cpp
#include "bind_cind.h"

#include "python_bindings/binding_core.h"

#include <vector>

#include "core/algorithms/cind/cind.h"
#include "core/algorithms/cind/cind_algorithm.h"
#include "core/algorithms/cind/condition.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace nb = nanobind;

namespace {
template <typename ElementType>
nb::tuple VectorToTuple(std::vector<ElementType> vec) {
    std::size_t const size = vec.size();
    nb::tuple_builder builder(size);
    for (std::size_t i = 0; i < size; ++i) {
        builder.put(std::move(vec[i]));
    }
    return builder.commit();
}
}  // namespace

namespace python_bindings {
void BindCind(nb::module_& main_module) {
    using namespace model;
    using namespace algos::cind;

    auto cind_module = main_module.def_submodule("cind");

    nb::class_<Condition>(cind_module, "Condition")
            .def("__str__", &Condition::ToString)
            .def("__eq__",
                 [](Condition const& cond1, Condition const& cond2) { return cond1 == cond2; })
            .def("__hash__",
                 [](Condition const& cond) {
                     return nb::hash(nb::int_(std::hash<Condition>{}(cond)));
                 })
            .def("data",
                 [](Condition const& cond) { return VectorToTuple(cond.condition_attrs_values); })
            .def("validity", [](Condition const& cond) { return cond.validity; })
            .def("completeness", [](Condition const& cond) { return cond.completeness; })
            .def("__getstate__",
                 [](Condition const& cond) {
                     return nb::make_tuple(cond.condition_attrs_values, cond.validity,
                                           cond.completeness);
                 })
            .def("__setstate__", [](Condition& cond, nb::tuple st) {
                if (st.size() != 3) {
                    throw std::runtime_error("Invalid state for Condition pickle!");
                }
                new (&cond) Condition(nb::cast<std::vector<std::string>>(st[0]),
                                      nb::cast<double>(st[1]), nb::cast<double>(st[2]));
            });

    nb::class_<CIND>(cind_module, "CIND")
            .def("__str__", &CIND::ToString)
            .def("__eq__", [](CIND const& cind1, CIND const& cind2) { return cind1 == cind2; })
            .def("__hash__", [](CIND const& cind) { return nb::hash(nb::int_(cind.Hash())); })
            .def("conditions_number", &CIND::ConditionsNumber)
            .def("get_conditions",
                 [](CIND const& cind) {
                     nb::list result;
                     for (auto const& cond : cind.conditions) {
                         result.append(cond);
                     }
                     return result;
                 })
            .def("get_condition_attributes",
                 [](CIND const& cind) { return VectorToTuple(cind.conditional_attributes); })
            .def("get_ind_string", [](CIND const& cind) { return cind.ind.ToLongString(); });

    BindPrimitiveNoBase<CindAlgorithm>(cind_module, "Cinderella")
            .def("get_cinds", &CindAlgorithm::CINDList);
}
}  // namespace python_bindings

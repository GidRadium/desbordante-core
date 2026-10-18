#include "python_bindings/cfd/bind_cfd.h"

#include "python_bindings/binding_core.h"

#include <optional>
#include <string>
#include <utility>

#include "core/algorithms/cfd/fd_first/fd_first_algorithm.h"
#include "core/algorithms/cfd/model/raw_cfd.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace {

namespace nb = nanobind;
using algos::cfd::RawCFD;

RawCFD CreateRawCFDFromTuples(nb::list lhs_tuples, nb::tuple rhs_tuple) {
    RawCFD::RawItems lhs;
    lhs.reserve(lhs_tuples.size());
    for (auto item : lhs_tuples) {
        nb::tuple tup = nb::cast<nb::tuple>(item);
        if (tup.size() != 2) throw nb::value_error("LHS items must be (attribute, value) tuples");
        int attr = nb::cast<int>(tup[0]);
        std::optional<std::string> value;
        if (!tup[1].is_none()) value = nb::cast<std::string>(tup[1]);
        lhs.emplace_back(RawCFD::RawItem{attr, value});
    }

    if (rhs_tuple.size() != 2) throw nb::value_error("RHS must be an (attribute, value) tuple");

    int rhs_attr = nb::cast<int>(rhs_tuple[0]);
    std::optional<std::string> rhs_value;
    if (!rhs_tuple[1].is_none()) rhs_value = nb::cast<std::string>(rhs_tuple[1]);

    return RawCFD(std::move(lhs), RawCFD::RawItem{rhs_attr, rhs_value});
}

nb::tuple MakeCfdTuple(algos::cfd::RawCFD const& cfd) {
    RawCFD::RawItems lhs = cfd.GetLhs();
    RawCFD::RawItem rhs = cfd.GetRhs();
    return nb::make_tuple(python_bindings::VectorToTuple(lhs), nb::cast(rhs));
}

}  // namespace

namespace python_bindings {
void BindCfd(nb::module_& main_module) {
    using namespace algos::cfd;

    auto cfd_module = main_module.def_submodule("cfd");

    nb::class_<RawCFD::RawItem>(cfd_module, "Item")
            .def(nb::init<int, std::optional<std::string>>(), nb::arg("attribute"),
                 nb::arg("value"))
            .def_prop_ro("attribute", &RawCFD::RawItem::GetAttribute)
            .def_prop_ro("value", &RawCFD::RawItem::GetValue)
            .def("__str__", &RawCFD::RawItem::ToString)
            .def("__repr__", &RawCFD::RawItem::ToString)
            .def(nb::self == nb::self)
            .def("__hash__",
                 [](RawCFD::RawItem const& item) {
                     return nb::hash(nb::make_tuple(item.GetAttribute(), item.GetValue()));
                 })
            .def("__getstate__",
                 [](RawCFD::RawItem const& item) {
                     return nb::make_tuple(item.attribute, item.value);
                 })
            .def("__setstate__", [](RawCFD::RawItem& item, nb::tuple t) {
                if (t.size() != 2) {
                    throw std::runtime_error("Invalid state for RawItem pickle!");
                }
                new (&item) RawCFD::RawItem{.attribute = nb::cast<AttributeIndex>(t[0]),
                                            .value = nb::cast<std::optional<std::string>>(t[1])};
            });

    nb::class_<RawCFD>(cfd_module, "CFD")
            .def(nb::init<>())
            .def(nb::init<RawCFD::RawItems, RawCFD::RawItem>(), nb::arg("lhs"), nb::arg("rhs"))
            .def(nb::new_(&CreateRawCFDFromTuples), nb::arg("lhs"), nb::arg("rhs"))
            .def("__str__", &RawCFD::ToString)
            .def_prop_ro("lhs", &RawCFD::GetLhs)
            .def_prop_ro("rhs", &RawCFD::GetRhs)
            .def(nb::self == nb::self)
            .def("__hash__", [](RawCFD const& cfd) { return nb::hash(MakeCfdTuple(cfd)); })
            .def("__getstate__",
                 [](RawCFD const& cfd) { return nb::make_tuple(cfd.GetLhs(), cfd.GetRhs()); })
            .def("__setstate__", [](RawCFD& cfd, nb::tuple t) {
                if (t.size() != 2) {
                    throw std::runtime_error("Invalid state for CFD pickle!");
                }
                auto lhs = nb::cast<RawCFD::RawItems>(t[0]);
                auto rhs = nb::cast<RawCFD::RawItem>(t[1]);
                new (&cfd) RawCFD(std::move(lhs), std::move(rhs));
            });

    BindPrimitive<FDFirstAlgorithm>(cfd_module, &CFDDiscovery::GetCfds, "CfdAlgorithm", "get_cfds",
                                    {"FDFirst"});
}
}  // namespace python_bindings

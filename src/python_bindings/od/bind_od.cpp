#include "python_bindings/od/bind_od.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/od/fastod/fastod.h"
#include "core/algorithms/od/fastod/model/canonical_od.h"
#include "core/algorithms/od/order/list_od.h"
#include "core/algorithms/od/order/order.h"
#include "core/config/indices/type.h"
#include "core/util/bitset_utils.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {

void BindOd(nb::module_& main_module) {
    using namespace algos::fastod;
    using namespace algos::order;

    auto od_module = main_module.def_submodule("od");

    nb::class_<AscCanonicalOD>(od_module, "AscCanonicalOD")
            .def("__str__", &AscCanonicalOD::ToString)
            .def("__eq__",
                 [](AscCanonicalOD const& od1, AscCanonicalOD const& od2) {
                     return od1.ToString() == od2.ToString();
                 })
            .def("__hash__",
                 [](AscCanonicalOD const& od) {
                     return nb::hash(nb::str(od.ToString().c_str(), od.ToString().size()));
                 })
            .def("__getstate__",
                 [](AscCanonicalOD const& od) {
                     std::vector<int> context_state = od.GetContext().GetColumnIndices();
                     int left = static_cast<int>(od.GetAttributePair().left);
                     int right = static_cast<int>(od.GetAttributePair().right);
                     return nb::make_tuple(std::move(context_state), left, right);
                 })
            .def("__setstate__", [](AscCanonicalOD& od, nb::tuple t) {
                if (t.size() != 3) {
                    throw std::runtime_error("Invalid state for AscCanonicalOD pickle!");
                }
                std::vector<int> context_state = nb::cast<std::vector<int>>(t[0]);
                int left = nb::cast<int>(t[1]);
                int right = nb::cast<int>(t[2]);
                AttributeSet context = AttributeSet::FromVector(context_state);
                new (&od) AscCanonicalOD(std::move(context), left, right);
            });

    nb::class_<DescCanonicalOD>(od_module, "DescCanonicalOD")
            .def("__str__", &DescCanonicalOD::ToString)
            .def("__eq__",
                 [](DescCanonicalOD const& od1, DescCanonicalOD const& od2) {
                     return od1.ToString() == od2.ToString();
                 })
            .def("__hash__",
                 [](DescCanonicalOD const& od) {
                     return nb::hash(nb::str(od.ToString().c_str(), od.ToString().size()));
                 })
            .def("__getstate__",
                 [](DescCanonicalOD const& od) {
                     std::vector<int> context_state = od.GetContext().GetColumnIndices();
                     int left = static_cast<int>(od.GetAttributePair().left);
                     int right = static_cast<int>(od.GetAttributePair().right);
                     return nb::make_tuple(std::move(context_state), left, right);
                 })
            .def("__setstate__", [](DescCanonicalOD& od, nb::tuple t) {
                if (t.size() != 3) {
                    throw std::runtime_error("Invalid state for DescCanonicalOD pickle!");
                }
                std::vector<int> context_state = nb::cast<std::vector<int>>(t[0]);
                int left = nb::cast<int>(t[1]);
                int right = nb::cast<int>(t[2]);
                AttributeSet context = AttributeSet::FromVector(context_state);
                new (&od) DescCanonicalOD(std::move(context), left, right);
            });

    nb::class_<SimpleCanonicalOD>(od_module, "SimpleCanonicalOD")
            .def("__str__", &SimpleCanonicalOD::ToString)
            .def("__eq__",
                 [](SimpleCanonicalOD const& od1, SimpleCanonicalOD const& od2) {
                     return od1.ToString() == od2.ToString();
                 })
            .def("__hash__",
                 [](SimpleCanonicalOD const& od) {
                     return nb::hash(nb::str(od.ToString().c_str(), od.ToString().size()));
                 })
            .def("__getstate__",
                 [](SimpleCanonicalOD const& od) {
                     std::vector<int> context_state = od.GetContext().GetColumnIndices();
                     int right = static_cast<int>(od.GetRight());
                     return nb::make_tuple(std::move(context_state), right);
                 })
            .def("__setstate__", [](SimpleCanonicalOD& od, nb::tuple t) {
                if (t.size() != 2) {
                    throw std::runtime_error("Invalid state for SimpleCanonicalOD pickle!");
                }
                std::vector<int> context_state = nb::cast<std::vector<int>>(t[0]);
                int right = nb::cast<int>(t[1]);
                AttributeSet context = AttributeSet::FromVector(context_state);
                new (&od) SimpleCanonicalOD(std::move(context), right);
            });

    nb::class_<ListOD>(od_module, "ListOD")
            .def_ro("lhs", &ListOD::lhs)
            .def_ro("rhs", &ListOD::rhs)
            .def("__getstate__", [](ListOD const& od) { return nb::make_tuple(od.lhs, od.rhs); })
            .def("__setstate__", [](ListOD& od, nb::tuple t) {
                if (t.size() != 2) {
                    throw std::runtime_error("Invalid state for ListOD pickle!");
                }
                new (&od) ListOD{.lhs = nb::cast<std::vector<model::ColumnIndex>>(t[0]),
                                 .rhs = nb::cast<std::vector<model::ColumnIndex>>(t[1])};
            });

    auto fastod_algos_module =
            BindPrimitiveNoBase<algos::Fastod>(od_module, "Fastod")
                    .def("get_asc_ods", &algos::Fastod::GetAscendingDependencies)
                    .def("get_desc_ods", &algos::Fastod::GetDescendingDependencies)
                    .def("get_simple_ods", &algos::Fastod::GetSimpleDependencies);
    auto order_algos_module =
            BindPrimitiveNoBase<Order>(od_module, "Order").def("get_list_ods", [](Order& algo) {
                OrderDependencies const& map_res = algo.GetValidODs();
                std::vector<ListOD> res;
                for (auto const& [lhs, rhs_list] : map_res) {
                    for (AttributeList const& rhs : rhs_list) {
                        res.emplace_back(lhs, rhs);
                    }
                }
                return res;
            });

    main_module.attr("od_module") = od_module;
}

}  // namespace python_bindings

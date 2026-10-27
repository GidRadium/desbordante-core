#include "python_bindings/dc/bind_fastadc.h"

#include "python_bindings/binding_core.h"

#include <magic_enum/magic_enum.hpp>

#include "core/algorithms/dc/FastADC/fastadc.h"
#include "core/util/enum_to_str.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/table_serialization.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace nb = nanobind;
using namespace algos::fastadc;

namespace fastadc_serialization {
using DC = algos::fastadc::DenialConstraint;

nb::tuple SerializeColumnOperand(ColumnOperand const& operand) {
    return nb::make_tuple(operand.GetColumn()->GetIndex(), util::EnumToStr(operand.GetTuple()));
}

ColumnOperand DeserializeColumnOperand(nb::tuple t,
                                       std::shared_ptr<RelationalSchema const> schema) {
    if (t.size() != 2) throw std::runtime_error("Invalid state for ColumnOperand pickle!");
    auto col_index = nb::cast<size_t>(t[0]);
    auto tuple_str = nb::cast<std::string>(t[1]);
    auto tuple_type = util::EnumFromStr<ColumnOperandTuple>(tuple_str);
    if (!tuple_type) throw std::runtime_error("Invalid tuple type: " + tuple_str);
    return ColumnOperand(schema->GetColumn(col_index), *tuple_type);
}

nb::tuple SerializePredicate(PredicatePtr predicate) {
    return nb::make_tuple(static_cast<int>(predicate->GetOperator().GetType()),
                          SerializeColumnOperand(predicate->GetLeftOperand()),
                          SerializeColumnOperand(predicate->GetRightOperand()));
}

PredicatePtr DeserializePredicate(nb::tuple t, std::shared_ptr<RelationalSchema const> schema) {
    if (t.size() != 3) {
        throw std::runtime_error("Invalid state for DC predicate pickle!");
    }
    auto op_type = magic_enum::enum_cast<OperatorType>(nb::cast<int>(t[0]));
    if (!op_type) {
        throw std::runtime_error("Invalid operator type in DC pickle!");
    }
    ColumnOperand lhs = DeserializeColumnOperand(nb::cast<nb::tuple>(t[1]), schema);
    ColumnOperand rhs = DeserializeColumnOperand(nb::cast<nb::tuple>(t[2]), std::move(schema));
    return new Predicate(Operator(*op_type), lhs, rhs);
}

nb::tuple SerializeDc(DC const& dc) {
    nb::tuple schema_state = table_serialization::SerializeRelationalSchema(dc.GetSchema().get());

    nb::list provider_state;
    auto const& objs = dc.GetPredicateSet().provider->GetObjects();
    for (PredicatePtr const& p : objs) {
        provider_state.append(fastadc_serialization::SerializePredicate(p));
    }

    nb::list bit_indices;
    auto const& bs = dc.GetPredicateSet().GetBitset();
    util::ForEachIndex(bs, [&bit_indices](size_t i) { bit_indices.append(i); });

    return nb::make_tuple(std::move(schema_state), std::move(provider_state),
                          std::move(bit_indices));
}

DC DeserializeDc(nb::tuple t) {
    if (t.size() != 3) throw std::runtime_error("Invalid state for DC pickle!");

    auto schema = table_serialization::DeserializeRelationalSchema(nb::cast<nb::tuple>(t[0]));

    auto provider = std::make_shared<PredicateIndexProvider>();
    for (auto h : nb::cast<nb::list>(t[1])) {
        PredicatePtr p =
                fastadc_serialization::DeserializePredicate(nb::cast<nb::tuple>(h), schema);
        provider->GetIndex(p);
    }

    boost::dynamic_bitset<> bitset(provider->Size());
    for (auto py_idx : nb::cast<nb::list>(t[2])) {
        size_t idx = nb::cast<size_t>(py_idx);
        if (idx >= bitset.size()) {
            throw std::runtime_error("Invalid predicate index in DC pickle!");
        }
        bitset.set(idx);
    }

    return DC(bitset, std::move(provider), std::move(schema));
}

nb::tuple SerializePredicateSetCanonical(PredicateSet const& set) {
    std::vector<PredicatePtr> predicate_ptrs;
    for (PredicatePtr predicate : set) {
        predicate_ptrs.push_back(predicate);
    }
    std::ranges::sort(predicate_ptrs, [](auto const& p1, auto const& p2) {
        return p1->ToString() < p2->ToString();
    });
    return python_bindings::VectorToTuple(predicate_ptrs, [](auto elem) {
        return fastadc_serialization::SerializePredicate(elem);
    });
}

nb::tuple ConvertDcToImmutableTuple(DC const& dc) {
    nb::tuple dc_predicates_tuple = SerializePredicateSetCanonical(dc.GetPredicateSet());
    nb::tuple schema_tuple =
            table_serialization::ConvertSchemaToImmutableTuple(dc.GetSchema().get());

    return nb::make_tuple(std::move(dc_predicates_tuple), std::move(schema_tuple));
}
}  // namespace fastadc_serialization

namespace python_bindings {
using DC = algos::fastadc::DenialConstraint;

void BindFastADC(nb::module_& main_module) {
    using namespace algos;

    auto dc_module = main_module.def_submodule("dc");
    nb::class_<DC>(dc_module, "DC")
            .def("__str__", &DC::ToString)
            .def("__repr__", &DC::ToString)
            .def("__eq__",
                 [](DC const& dc1, DC const& dc2) {
                     if (&dc1 == &dc2) {
                         return true;
                     }
                     nb::tuple tuple1 = fastadc_serialization::ConvertDcToImmutableTuple(dc1);
                     nb::tuple tuple2 = fastadc_serialization::ConvertDcToImmutableTuple(dc2);

                     return tuple1.equal(tuple2);
                 })
            .def("__hash__",
                 [](DC const& dc) {
                     nb::tuple state_tuple = fastadc_serialization::ConvertDcToImmutableTuple(dc);

                     return nb::hash(state_tuple);
                 })
            .def("__getstate__",
                 [](DC const& dc) { return fastadc_serialization::SerializeDc(dc); })
            .def("__setstate__", [](DC& dc, nb::tuple t) {
                new (&dc) DC(fastadc_serialization::DeserializeDc(t));
            });

    BindPrimitiveNoBase<dc::FastADC>(dc_module, "FastADC")
            .def("get_dcs", &dc::FastADC::GetDCs, nb::rv_policy::copy);
}
}  // namespace python_bindings

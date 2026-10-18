#include "python_bindings/ucc/bind_ucc.h"

#include "python_bindings/binding_core.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/algorithms/ucc/hpivalid/hpivalid.h"
#include "core/algorithms/ucc/hyucc/hyucc.h"
#include "core/algorithms/ucc/pyroucc/pyroucc.h"
#include "core/algorithms/ucc/ucc.h"
#include "core/config/indices/type.h"
#include "core/model/table/column.h"
#include "core/util/bitset_utils.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/table_serialization.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace {
namespace nb = nanobind;
using model::UCC;

nb::tuple SerializeUcc(UCC const& ucc) {
    nb::tuple schema_state = table_serialization::SerializeRelationalSchema(ucc.GetSchema().get());
    nb::tuple vertical_state = table_serialization::SerializeVertical(ucc);
    return nb::make_tuple(std::move(schema_state), std::move(vertical_state));
}

UCC DeserializeUcc(nb::tuple t) {
    if (t.size() != 2) {
        throw std::runtime_error("Invalid state for UCC pickle!");
    }
    nb::tuple schema_state = nb::cast<nb::tuple>(t[0]);
    std::shared_ptr<RelationalSchema const> schema_ptr =
            table_serialization::DeserializeRelationalSchema(schema_state);
    nb::tuple vertical_state = nb::cast<nb::tuple>(t[1]);
    Vertical vert = table_serialization::DeserializeVertical(vertical_state, schema_ptr.get());
    return UCC(std::move(schema_ptr), std::move(vert));
}

nb::tuple MakeUCCNameTuple(UCC const& ucc) {
    std::vector<Column const*> columns = ucc.GetColumns();
    return python_bindings::VectorToTuple(columns, [](auto elem) { return elem->GetName(); });
}
}  // namespace

namespace python_bindings {
void BindUcc(nb::module_& main_module) {
    using namespace algos;

    auto ucc_module = main_module.def_submodule("ucc");
    nb::class_<UCC>(ucc_module, "UCC")
            .def("__str__", &UCC::ToIndicesString)
            .def("to_short_string", &UCC::ToIndicesString)
            .def("to_long_string", &UCC::ToString)
            .def_prop_ro("indices", &UCC::GetColumnIndicesAsVector)
            .def("__eq__", [](UCC const& a, UCC const& b) { return a == b; })
            .def("__hash__", [](UCC const& ucc) { return nb::hash(MakeUCCNameTuple(ucc)); })
            .def("__getstate__", [](UCC const& ucc) { return SerializeUcc(ucc); })
            .def("__setstate__", [](UCC& ucc, nb::tuple t) { new (&ucc) UCC(DeserializeUcc(t)); });
    BindPrimitive<HPIValid, HyUCC, PyroUCC>(
            ucc_module, nb::overload_cast<>(&UCCAlgorithm::UCCList, nb::const_), "UccAlgorithm",
            "get_uccs", {"HPIValid", "HyUCC", "PyroUCC"});
}
}  // namespace python_bindings

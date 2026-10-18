#include "python_bindings/py_util/table_serialization.h"

#include "python_bindings/binding_core.h"

#include <utility>

#include "python_bindings/py_util/vector_to_tuple.h"

namespace nb = nanobind;

namespace table_serialization {

nb::tuple SerializeRelationalSchema(RelationalSchema const* schema) {
    std::vector<std::unique_ptr<Column>> const& columns = schema->GetColumns();
    std::vector<std::string> column_names;
    column_names.reserve(columns.size());
    for (auto const& col_ptr : columns) {
        column_names.emplace_back(col_ptr->GetName());
    }
    return nb::make_tuple(schema->GetName(), std::move(column_names));
}

std::shared_ptr<RelationalSchema const> DeserializeRelationalSchema(nb::tuple t) {
    if (t.size() != 2) {
        throw std::runtime_error("Invalid state for RelationalSchema pickle!");
    }
    auto schema_name = nb::cast<std::string>(t[0]);
    auto column_names = nb::cast<std::vector<std::string>>(t[1]);
    return std::make_shared<RelationalSchema>(std::move(schema_name), std::move(column_names));
}

// relational schema conversion suitable for nb::hash
nb::tuple ConvertSchemaToImmutableTuple(RelationalSchema const* schema) {
    std::vector<std::unique_ptr<Column>> const& columns = schema->GetColumns();
    nb::tuple col_tuple = python_bindings::VectorToTuple(columns, [](auto const& elem) {
        return nb::make_tuple(elem->GetName(), elem->GetIndex());
    });
    return nb::make_tuple(schema->GetName(), std::move(col_tuple));
}

nb::tuple SerializeVertical(Vertical const& v) {
    std::vector<unsigned int> idx_vec = v.GetColumnIndicesAsVector();
    return nb::make_tuple(std::move(idx_vec));
}

Vertical DeserializeVertical(nb::tuple t, RelationalSchema const* schema) {
    if (t.size() != 1) {
        throw std::runtime_error("Invalid state for Vertical pickle!");
    }
    auto idx_vec = nb::cast<std::vector<unsigned int>>(t[0]);
    boost::dynamic_bitset<> bitset = util::IndicesToBitset(idx_vec, schema->GetNumColumns());
    return Vertical(schema, std::move(bitset));
}

nb::tuple SerializeColumn(Column const& c) {
    return nb::make_tuple(c.GetName(), c.GetIndex());
}

Column DeserializeColumn(nb::tuple t, RelationalSchema const* schema) {
    if (t.size() != 2) {
        throw std::runtime_error("Invalid state for Column pickle!");
    }
    auto col_name = nb::cast<std::string>(t[0]);
    auto col_index = nb::cast<model::ColumnIndex>(t[1]);
    Column col(schema, std::move(col_name), col_index);
    return col;
}
}  // namespace table_serialization

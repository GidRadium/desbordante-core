#pragma once

#include "python_bindings/binding_core.h"

#include <memory>

#include "core/model/table/column.h"
#include "core/model/table/column_combination.h"
#include "core/model/table/relational_schema.h"
#include "core/model/table/vertical.h"

namespace nb = nanobind;

namespace table_serialization {

nb::tuple SerializeRelationalSchema(RelationalSchema const* schema);

std::shared_ptr<RelationalSchema const> DeserializeRelationalSchema(nb::tuple t);

nb::tuple ConvertSchemaToImmutableTuple(RelationalSchema const* schema);

nb::tuple SerializeVertical(Vertical const& v);

Vertical DeserializeVertical(nb::tuple t, RelationalSchema const* schema);

nb::tuple SerializeColumn(Column const& c);

Column DeserializeColumn(nb::tuple t, RelationalSchema const* schema);
}  // namespace table_serialization

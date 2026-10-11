#include "python_bindings/data/bind_data_types.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>

#include "core/config/tabular_data/input_table_type.h"
#include "core/model/table/column_combination.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace python_bindings {

void BindDataTypes(nb::module_& main_module) {
    auto data_module = main_module.def_submodule("data_types");

    data_module.doc() = R"doc(
        Contains the types of data supported by Desbordante.

        Currently only used as tags for Algorithm.get_option_type
    )doc";

    nb::class_<config::InputTable>(data_module, "Table");

    using namespace model;

    nb::class_<ColumnCombination>(data_module, "ColumnCombination")
        .def("__str__", &ColumnCombination::ToString)
        .def(
            "to_index_tuple",
            [](ColumnCombination const& cc) {
                return nb::make_tuple(
                    cc.GetTableIndex(),
                    nb::cast(cc.GetColumnIndices())
                );
            }
        )
        .def_prop_ro("table_index", &ColumnCombination::GetTableIndex)
        .def_prop_ro("column_indices", &ColumnCombination::GetColumnIndices)
        .def(
            "__getstate__",
            [](ColumnCombination const& cc) {
                return nb::make_tuple(
                    cc.GetTableIndex(),
                    cc.GetColumnIndices()
                );
            }
        )
        .def(
            "__setstate__",
            [](ColumnCombination& cc, nb::tuple state) {
                if (state.size() != 2) {
                    throw std::runtime_error(
                        "Invalid state for ColumnCombination pickle!"
                    );
                }

                auto table_index =
                    nb::cast<model::ColumnIndex>(state[0]);

                auto col_indices =
                    nb::cast<std::vector<model::ColumnIndex>>(state[1]);

                new (&cc) ColumnCombination(table_index, col_indices);
            }
        );
}

}  // namespace python_bindings

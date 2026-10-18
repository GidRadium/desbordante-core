#include "python_bindings/ind/bind_ind.h"

#include "python_bindings/binding_core.h"

#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/algorithms/ind/faida/faida.h"
#include "core/algorithms/ind/ind.h"
#include "core/algorithms/ind/ind_algorithm.h"
#include "core/algorithms/ind/mind/mind.h"
#include "core/algorithms/ind/spider/spider.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/table_serialization.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace nb = nanobind;

namespace {
constexpr double kRoundingValue = 1e9;

}  // namespace

namespace ind_serialization {
using namespace model;
using IND = model::IND;

nb::tuple SerializeInd(IND const& ind) {
    nb::object lhs_state = nb::cast(ind.GetLhs());
    nb::object rhs_state = nb::cast(ind.GetRhs());

    std::vector<std::unique_ptr<RelationalSchema>> const& schemas_vec = *(ind.GetSchemas());
    std::vector<nb::tuple> schemas_state;
    for (auto const& schema : schemas_vec) {
        std::vector<std::string> col_names;
        for (std::unique_ptr<Column> const& col_ptr : schema->GetColumns()) {
            col_names.push_back(col_ptr->GetName());
        }
        schemas_state.push_back(nb::make_tuple(schema->GetName(), std::move(col_names)));
    }
    return nb::make_tuple(std::move(lhs_state), std::move(rhs_state), std::move(schemas_state),
                          ind.GetError());
}

nb::tuple ConvertIndToImmutableTuple(IND const& ind) {
    auto lhs_schema_index = ind.GetLhs().GetTableIndex();
    auto rhs_schema_index = ind.GetRhs().GetTableIndex();

    auto const& schemas = *(ind.GetSchemas());
    auto const& lhs_schema = schemas[lhs_schema_index];
    auto const& rhs_schema = schemas[rhs_schema_index];

    nb::tuple lhs_schema_tuple =
            table_serialization::ConvertSchemaToImmutableTuple(lhs_schema.get());
    nb::tuple rhs_schema_tuple =
            table_serialization::ConvertSchemaToImmutableTuple(rhs_schema.get());

    std::vector<unsigned int> lhs_indices = ind.GetLhs().GetColumnIndices();

    nb::tuple lhs_tuple = python_bindings::VectorToTuple(lhs_indices);

    std::vector<unsigned int> rhs_indices = ind.GetRhs().GetColumnIndices();
    nb::tuple rhs_tuple = python_bindings::VectorToTuple(rhs_indices);

    auto round_double_val = [](double d) {
        return std::round(d * kRoundingValue) / kRoundingValue;
    };

    return nb::make_tuple(round_double_val(ind.GetError()), std::move(lhs_schema_tuple),
                          std::move(lhs_tuple), std::move(rhs_schema_tuple), std::move(rhs_tuple));
}

IND DeserializeInd(nb::tuple t) {
    if (t.size() != 4) {
        throw std::runtime_error("Invalid state for IND pickle!");
    }
    ColumnCombination lhs_cc = nb::cast<ColumnCombination>(t[0]);
    ColumnCombination rhs_cc = nb::cast<ColumnCombination>(t[1]);
    auto lhs_ptr = std::make_shared<ColumnCombination>(lhs_cc);
    auto rhs_ptr = std::make_shared<ColumnCombination>(rhs_cc);

    auto schemas_state = nb::cast<std::vector<nb::tuple>>(t[2]);
    std::vector<std::unique_ptr<RelationalSchema>> schemas;
    for (nb::tuple const& s_state : schemas_state) {
        std::string s_name = nb::cast<std::string>(s_state[0]);
        std::vector<std::string> s_col_names = nb::cast<std::vector<std::string>>(s_state[1]);
        auto schema = std::make_unique<RelationalSchema>(std::move(s_name), std::move(s_col_names));
        schemas.push_back(std::move(schema));
    }
    auto schemas_ptr =
            std::make_shared<std::vector<std::unique_ptr<RelationalSchema>>>(std::move(schemas));
    double error = nb::cast<double>(t[3]);
    return IND(std::move(lhs_ptr), std::move(rhs_ptr), std::move(schemas_ptr), error);
}
}  // namespace ind_serialization

namespace python_bindings {
void BindInd(nb::module_& main_module) {
    using namespace model;
    using namespace algos;

    auto ind_module = main_module.def_submodule("ind");

    nb::class_<IND>(ind_module, "IND")
            .def("__str__", &IND::ToLongString)
            .def("to_short_string", &IND::ToShortString)
            .def("to_long_string", &IND::ToLongString)
            .def("get_lhs", &IND::GetLhs)
            .def("get_rhs", &IND::GetRhs)
            .def("get_error", &IND::GetError)
            .def("__eq__",
                 [](IND const& ind1, IND const& ind2) {
                     if (&ind1 == &ind2) {
                         return true;
                     }
                     nb::tuple ind1_state_tuple =
                             ind_serialization::ConvertIndToImmutableTuple(ind1);
                     nb::tuple ind2_state_tuple =
                             ind_serialization::ConvertIndToImmutableTuple(ind2);
                     return ind1_state_tuple.equal(ind2_state_tuple);
                 })
            .def("__hash__",
                 [](IND const& ind) {
                     nb::tuple ind_state_tuple = ind_serialization::ConvertIndToImmutableTuple(ind);
                     return nb::hash(ind_state_tuple);
                 })
            .def("__getstate__",
                 [](IND const& ind) { return ind_serialization::SerializeInd(ind); })
            .def("__setstate__", [](IND& ind, nb::tuple t) {
                new (&ind) IND(ind_serialization::DeserializeInd(t));
            });

    static constexpr auto kSpiderName = "Spider";
    static constexpr auto kMindName = "Mind";
    static constexpr auto kFaidaName = "Faida";

    auto ind_algos_module =
            BindPrimitive<Spider, Faida, Mind>(ind_module, &INDAlgorithm::INDList, "IndAlgorithm",
                                               "get_inds", {kSpiderName, kFaidaName, kMindName});
    auto define_submodule = [&ind_algos_module, &main_module](char const* name,
                                                              std::vector<char const*> algorithms) {
        auto algos_module = main_module.def_submodule(name).def_submodule("algorithms");
        for (auto algo_name : algorithms) {
            algos_module.attr(algo_name) = ind_algos_module.attr(algo_name);
        }
        algos_module.attr("Default") = algos_module.attr(algorithms.front());
    };

    define_submodule("aind", {kSpiderName, kMindName});
}
}  // namespace python_bindings

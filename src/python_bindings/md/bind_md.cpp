#include "python_bindings/md/bind_md.h"

#include "python_bindings/binding_core.h"

#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/algorithms/md/hymd/hymd.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/date_difference.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/equality.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/jaccard.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/lcs.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/levenshtein.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/monge_elkan.h"
#include "core/algorithms/md/hymd/preprocessing/column_matches/number_difference.h"
#include "core/algorithms/md/md.h"
#include "python_bindings/md/object_similarity_measure.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/table_serialization.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace {
namespace nb = nanobind;
using model::MD;
using namespace algos;
using namespace algos::hymd;
using namespace algos::hymd::preprocessing;
using namespace algos::hymd::preprocessing::column_matches;
using namespace nb::literals;
using namespace python_bindings;

std::vector<ColumnClassifierValueId> PickAll(std::vector<Similarity> const& similarities) {
    return ccv_id_pickers::IndexUniform<Similarity>{0}(similarities);
}

template <typename MeasureType>
auto BindColumnMatch(auto&& name, auto&& measures_module) {
    auto cls = nb::class_<MeasureType, ColumnMatch>(measures_module, name);
    return cls;
}

template <typename ColumnFunctions>
ColumnFunctions ToColumnFunctions(nb::object const& obj) {
    return obj.is_none() ? ColumnFunctions{} : nb::cast<ColumnFunctions>(obj);
}

template <typename MeasureType, typename... Args>
void BindColumnMatchWithConstructor(Args&&... args) {
    using ColumnFunctions = MeasureType::TransformFunctionsOption;
    BindColumnMatch<MeasureType>(std::forward<Args>(args)...)
            .def(
                    "__init__",
                    [](MeasureType* self, ColumnIdentifier left_column,
                       ColumnIdentifier right_column, model::md::DecisionBoundary min_sim,
                       std::size_t bound_number_limit, nb::object column_functions) {
                        new (self)
                                MeasureType(std::move(left_column), std::move(right_column),
                                            min_sim, bound_number_limit,
                                            ToColumnFunctions<ColumnFunctions>(column_functions));
                    },
                    "left_column"_a, "right_column"_a, "minimum_similarity"_a = 0.7, nb::kw_only(),
                    "bound_number_limit"_a = 0, "column_functions"_a = nb::none())
            .def(
                    "__init__",
                    [](MeasureType* self, ColumnIdentifier left_column,
                       ColumnIdentifier right_column, model::md::DecisionBoundary min_sim,
                       nb::typed<nb::callable, std::vector<ColumnClassifierValueId>(
                                                       std::vector<Similarity> const&)>
                               similarities_picker,
                       nb::object column_functions) {
                        new (self) MeasureType(
                                std::move(left_column), std::move(right_column), min_sim,
                                PyLhsCCVIDsPicker(std::move(similarities_picker)),
                                ToColumnFunctions<ColumnFunctions>(column_functions));
                    },
                    "left_column"_a, "right_column"_a, "minimum_similarity"_a = 0.7, nb::kw_only(),
                    nb::arg("pick_lhs_indices") = ccv_id_pickers::SimilaritiesPicker(PickAll),
                    "column_functions"_a = nb::none());
}

nb::tuple SerializeColumnMatch(MD const& md_obj) {
    std::shared_ptr<std::vector<model::md::ColumnMatch> const> matches_ptr =
            md_obj.GetColumnMatches();
    if (!matches_ptr) {
        return nb::tuple{};
    }
    return python_bindings::VectorToTuple(*matches_ptr, [](auto const& elem) {
        return nb::make_tuple(elem.left_col_index, elem.right_col_index, elem.name);
    });
}

nb::tuple SerializeLhs(MD const& md_obj) {
    return python_bindings::VectorToTuple(md_obj.GetLhs(), [](auto const& elem) {
        std::size_t match_idx = elem.GetColumnMatchIndex();
        model::md::DecisionBoundary db = elem.GetDecisionBoundary();
        std::optional<model::md::DecisionBoundary> maybe_max = elem.GetMaxDisprovedBound();
        return nb::make_tuple(match_idx, db, maybe_max);
    });
}

nb::tuple SerializeRhs(MD const& md_obj) {
    auto [rhs_idx, rhs_bound] = md_obj.GetRhs();
    return nb::make_tuple(rhs_idx, rhs_bound);
}

nb::tuple SerializeMD(MD const& md_obj) {
    nb::tuple left_schema_state =
            table_serialization::SerializeRelationalSchema(md_obj.GetLeftSchema().get());
    nb::tuple right_schema_state =
            table_serialization::SerializeRelationalSchema(md_obj.GetRightSchema().get());

    nb::tuple match_tuple = SerializeColumnMatch(md_obj);

    nb::tuple lhs_tuple = SerializeLhs(md_obj);

    nb::tuple rhs_tuple = SerializeRhs(md_obj);

    return nb::make_tuple(std::move(left_schema_state), std::move(right_schema_state),
                          std::move(match_tuple), std::move(lhs_tuple), std::move(rhs_tuple));
}

MD DeserializeMd(nb::tuple t) {
    if (t.size() != 5) {
        throw std::runtime_error("Invalid state for MD pickle!");
    }
    std::shared_ptr<RelationalSchema const> left_schema =
            table_serialization::DeserializeRelationalSchema(nb::cast<nb::tuple>(t[0]));
    std::shared_ptr<RelationalSchema const> right_schema =
            table_serialization::DeserializeRelationalSchema(nb::cast<nb::tuple>(t[1]));
    auto match_tuple = nb::cast<nb::tuple>(t[2]);
    auto matches_ptr = std::make_shared<std::vector<model::md::ColumnMatch>>();
    matches_ptr->reserve(match_tuple.size());
    for (auto item : match_tuple) {
        auto tpl = nb::cast<nb::tuple>(item);
        if (tpl.size() != 3) {
            throw std::runtime_error("Invalid state for MD pickle!");
        }
        auto l_idx = nb::cast<std::size_t>(tpl[0]);
        auto r_idx = nb::cast<std::size_t>(tpl[1]);
        auto name = nb::cast<std::string>(tpl[2]);
        matches_ptr->emplace_back(l_idx, r_idx, std::move(name));
    }
    auto lhs_tuple = nb::cast<nb::tuple>(t[3]);
    std::vector<model::md::LhsColumnSimilarityClassifier> lhs_vec;
    lhs_vec.reserve(lhs_tuple.size());
    for (auto item : lhs_tuple) {
        auto tpl = nb::cast<nb::tuple>(item);
        if (tpl.size() != 3) {
            throw std::runtime_error("Invalid state for MD pickle!");
        }
        auto match_idx = nb::cast<std::size_t>(tpl[0]);
        auto dec_bound = nb::cast<double>(tpl[1]);
        std::optional<model::md::DecisionBoundary> restored_maybe_max =
                nb::cast<std::optional<model::md::DecisionBoundary>>(tpl[2]);
        lhs_vec.emplace_back(restored_maybe_max, match_idx, dec_bound);
    }
    auto rhs_tpl = nb::cast<nb::tuple>(t[4]);
    if (rhs_tpl.size() != 2) {
        throw std::runtime_error("Invalid state for MD pickle!");
    }
    auto rhs_idx = nb::cast<std::size_t>(rhs_tpl[0]);
    auto rhs_dec = nb::cast<double>(rhs_tpl[1]);
    model::md::ColumnSimilarityClassifier rhs_classifier(rhs_idx, rhs_dec);
    model::MD md_restored(std::move(left_schema), std::move(right_schema), std::move(matches_ptr),
                          std::move(lhs_vec), std::move(rhs_classifier));
    return md_restored;
}

nb::tuple ConvertMdToImmutableTuple(MD const& md_obj) {
    nb::tuple left_schema_tuple =
            table_serialization::ConvertSchemaToImmutableTuple(md_obj.GetLeftSchema().get());
    nb::tuple right_schema_tuple =
            table_serialization::ConvertSchemaToImmutableTuple(md_obj.GetRightSchema().get());

    nb::tuple match_tuple = SerializeColumnMatch(md_obj);
    nb::tuple lhs_tuple = SerializeLhs(md_obj);
    nb::tuple rhs_tuple = SerializeRhs(md_obj);

    return nb::make_tuple(std::move(left_schema_tuple), std::move(right_schema_tuple),
                          std::move(match_tuple), std::move(lhs_tuple), std::move(rhs_tuple));
}
}  // namespace

namespace python_bindings {
void BindMd(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::hymd;
    using namespace algos::hymd::preprocessing::column_matches;
    using namespace nb::literals;
    using namespace model;

    auto md_module = main_module.def_submodule("md");
    nb::class_<ColumnDescription>(md_module, "ColumnDescription")
            .def_ro("column_name", &ColumnDescription::column_name)
            .def_ro("column_index", &ColumnDescription::column_index);

    nb::class_<ColumnMatchDescription>(md_module, "ColumnMatchDescription")
            .def_ro("left_column_description", &ColumnMatchDescription::left_column_description)
            .def_ro("right_column_description", &ColumnMatchDescription::right_column_description)
            .def_ro("column_match_name", &ColumnMatchDescription::column_match_name);

    nb::class_<LhsSimilarityClassifierDesctription>(md_module,
                                                    "LhsSimilarityClassifierDesctription")
            .def_ro("column_match_description",
                    &LhsSimilarityClassifierDesctription::column_match_description)
            .def_ro("decision_boundary", &LhsSimilarityClassifierDesctription::decision_boundary)
            .def_ro("max_invalid_bound", &LhsSimilarityClassifierDesctription::max_invalid_bound);

    nb::class_<RhsSimilarityClassifierDesctription>(md_module,
                                                    "RhsSimilarityClassifierDesctription")
            .def_ro("column_match_description",
                    &RhsSimilarityClassifierDesctription::column_match_description)
            .def_ro("decision_boundary", &RhsSimilarityClassifierDesctription::decision_boundary);
    nb::class_<MDDescription>(md_module, "MDDescription")
            .def_ro("left_table_name", &MDDescription::left_table_name)
            .def_ro("right_table_name", &MDDescription::right_table_name)
            .def_ro("lhs", &MDDescription::lhs)
            .def_ro("rhs", &MDDescription::rhs);

    nb::class_<MD>(md_module, "MD")
            .def_prop_ro("lhs_bounds", &MD::GetLhsDecisionBounds)
            .def_prop_ro("rhs", &MD::GetRhs)
            .def("to_long_string", &MD::ToStringFull)
            .def("to_short_string", &MD::ToStringShort)
            .def("to_string_active", &MD::ToStringActiveLhsOnly)
            .def("__str__", &MD::ToStringActiveLhsOnly)
            .def_prop_ro("single_table", &MD::SingleTable)
            .def("get_description", &MD::GetDescription)
            .def("__eq__",
                 [](MD const& md1, MD const& md2) {
                     if (&md1 == &md2) {
                         return true;
                     }
                     nb::tuple md1_state_tuple = ConvertMdToImmutableTuple(md1);
                     nb::tuple md2_state_tuple = ConvertMdToImmutableTuple(md2);
                     return md1_state_tuple.equal(md2_state_tuple);
                 })
            .def("__hash__",
                 [](MD const& md_obj) {
                     nb::tuple state_tuple = ConvertMdToImmutableTuple(md_obj);
                     return nb::hash(state_tuple);
                 })
            .def("__getstate__", [](MD const& md_obj) { return SerializeMD(md_obj); })
            .def("__setstate__",
                 [](MD& md_obj, nb::tuple t) { new (&md_obj) MD(DeserializeMd(t)); });

    auto column_matches_module = md_module.def_submodule("column_matches");
    nb::class_<ColumnMatch>(column_matches_module, "ColumnMatch");
    BindColumnMatchWithConstructor<Levenshtein>("Levenshtein", column_matches_module);
    BindColumnMatchWithConstructor<MongeElkan>("MongeElkan", column_matches_module);
    BindColumnMatchWithConstructor<Jaccard>("Jaccard", column_matches_module);
    BindColumnMatchWithConstructor<LVNormDateDifference>("LVNormDateDistance",
                                                         column_matches_module);
    BindColumnMatchWithConstructor<LVNormNumberDistance>("LVNormNumberDistance",
                                                         column_matches_module);
    BindColumnMatchWithConstructor<Lcs>("Lcs", column_matches_module);
    BindColumnMatch<Equality>("Equality", column_matches_module)
            .def(nb::init<ColumnIdentifier, ColumnIdentifier>(), "left_column"_a, "right_column"_a);

    BindColumnMatch<Custom>("Custom", column_matches_module)
            .def(
                    "__init__",
                    [](Custom* self,
                       nb::typed<nb::callable, preprocessing::Similarity(nb::object, nb::object)>
                               comparer,
                       ColumnIdentifier left_column, ColumnIdentifier right_column,
                       nb::object column_functions, bool symmetrical, bool equality_is_max,
                       model::md::DecisionBoundary min_sim, std::string measure_name,
                       std::size_t size_limit) {
                        new (self) Custom(
                                std::move(comparer), std::move(left_column),
                                std::move(right_column),
                                ToColumnFunctions<ObjMeasureTransformFuncs>(column_functions),
                                symmetrical, equality_is_max, min_sim, std::move(measure_name),
                                size_limit);
                    },
                    "comparer"_a, "left_column"_a, "right_column"_a, nb::kw_only(),
                    "column_functions"_a = nb::none(), "symmetrical"_a = false,
                    "equality_is_max"_a = false, "min_sim"_a = 0.7,
                    "measure_name"_a = "custom_measure", "size_limit"_a = 0)
            .def(
                    "__init__",
                    [](Custom* self, nb::object comparer, ColumnIdentifier left_column,
                       ColumnIdentifier right_column, bool classic_measure,
                       nb::object column_functions, model::md::DecisionBoundary min_sim,
                       std::string measure_name, std::size_t size_limit) {
                        new (self) Custom(
                                std::move(comparer), std::move(left_column),
                                std::move(right_column), classic_measure,
                                ToColumnFunctions<ObjMeasureTransformFuncs>(column_functions),
                                min_sim, std::move(measure_name), size_limit);
                    },
                    "comparer"_a, "left_column"_a, "right_column"_a, "classic_measure"_a,
                    nb::kw_only(), "column_functions"_a = nb::none(), "min_sim"_a = 0.7,
                    "measure_name"_a = "custom_measure", "size_limit"_a = 0)
            .def(
                    "__init__",
                    [](Custom* self,
                       nb::typed<nb::callable, preprocessing::Similarity(nb::object, nb::object)>
                               comparer,
                       ColumnIdentifier left_column, ColumnIdentifier right_column,
                       nb::object column_functions, bool symmetrical, bool equality_is_max,
                       model::md::DecisionBoundary min_sim, std::string measure_name,
                       nb::typed<nb::callable, std::vector<ColumnClassifierValueId>(
                                                       std::vector<Similarity> const&)>
                               pick_lhs_indices) {
                        new (self) Custom(
                                std::move(comparer), std::move(left_column),
                                std::move(right_column),
                                ToColumnFunctions<ObjMeasureTransformFuncs>(column_functions),
                                symmetrical, equality_is_max, min_sim, std::move(measure_name),
                                std::move(pick_lhs_indices));
                    },
                    "comparer"_a, "left_column"_a, "right_column"_a, nb::kw_only(),
                    "column_functions"_a = nb::none(), "symmetrical"_a = false,
                    "equality_is_max"_a = false, "min_sim"_a = 0.7, "measure_name"_a = "custom",
                    nb::arg("pick_lhs_indices") = ccv_id_pickers::SimilaritiesPicker(PickAll))
            .doc() = R"(Defines a column match with a custom similarity measure.)";

    BindPrimitive<HyMD>(md_module, &MdAlgorithm::MdList, "MdAlgorithm", "get_mds", {"HyMD"});
}
}  // namespace python_bindings

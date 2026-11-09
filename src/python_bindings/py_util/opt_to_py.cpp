#include "python_bindings/py_util/opt_to_py.h"

#include "python_bindings/binding_core.h"

#include <filesystem>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/core/demangle.hpp>

#include "core/algorithms/algebraic_constraints/bin_operation_enum.h"
#include "core/algorithms/cfd/fd_first/enums.h"
#include "core/algorithms/cfd/model/raw_cfd.h"
#include "core/algorithms/cind/types.h"
#include "core/algorithms/dd/dd.h"
#include "core/algorithms/dd/dd_verifier/Metric.h"
#include "core/algorithms/fd/afd_metric/afd_metric.h"
#include "core/algorithms/fd/tane/enums.h"
#include "core/algorithms/gdd/gdd.h"
#include "core/algorithms/md/hymd/enums.h"
#include "core/algorithms/md/hymd/hymd.h"
#include "core/algorithms/md/md_verifier/column_similarity_classifier.h"
#include "core/algorithms/metric/enums.h"
#include "core/algorithms/nar/des/enums.h"
#include "core/algorithms/od/fastod/od_ordering.h"
#include "core/algorithms/pac/model/idomain.h"
#include "core/config/custom_metric/custom_metric/type.h"
#include "core/config/custom_metric/custom_metrics/type.h"
#include "core/config/custom_metric/custom_vector_metric/type.h"
#include "core/config/custom_random_seed/type.h"
#include "core/config/equal_nulls/type.h"
#include "core/config/error/type.h"
#include "core/config/error_measure/type.h"
#include "core/config/indices/type.h"
#include "core/config/max_lhs/type.h"
#include "core/config/thread_number/type.h"
#include "core/model/transaction/input_format_type.h"
#include "core/util/enum_to_str.h"

namespace {
namespace nb = nanobind;
using ConvFunction = std::function<nb::object(boost::any)>;

template <typename T>
std::pair<std::type_index, ConvFunction> const kNormalConvPair{
        std::type_index(typeid(T)),
        [](boost::any value) { return nb::cast(boost::any_cast<T>(value)); }};

template <typename T>
std::pair<std::type_index, ConvFunction> const kEnumConvPair{
        std::type_index(typeid(T)),
        [](boost::any value) { return nb::cast(util::EnumToStr(boost::any_cast<T>(value))); }};

std::unordered_map<std::type_index, ConvFunction> const kConverters{
        kNormalConvPair<int>,
        kNormalConvPair<double>,
        kNormalConvPair<long double>,
        kNormalConvPair<unsigned short>,
        kNormalConvPair<unsigned int>,
        kNormalConvPair<unsigned long>,
        kNormalConvPair<bool>,
        kNormalConvPair<std::vector<std::string>>,
        kNormalConvPair<std::vector<double>>,
        kNormalConvPair<std::vector<unsigned int>>,
        kNormalConvPair<std::filesystem::path>,
        kNormalConvPair<std::vector<std::filesystem::path>>,
        kNormalConvPair<std::unordered_set<size_t>>,
        kNormalConvPair<std::unordered_map<std::string, std::vector<unsigned int>>>,
        kNormalConvPair<config::ThreadNumType>,
        kNormalConvPair<config::CustomRandomSeedType>,
        kNormalConvPair<config::MaxLhsType>,
        kNormalConvPair<config::ErrorType>,
        kNormalConvPair<config::IndicesType>,
        kNormalConvPair<model::DDString>,
        kNormalConvPair<model::Gdd>,
        kNormalConvPair<std::vector<model::Gdd>>,
        kNormalConvPair<std::vector<model::GddCounterexample>>,
        kNormalConvPair<algos::hymd::HyMD::ColumnMatches>,
        kNormalConvPair<algos::md::ColumnSimilarityClassifier>,
        kNormalConvPair<std::vector<algos::md::ColumnSimilarityClassifier>>,
        kNormalConvPair<std::unordered_map<std::string, std::shared_ptr<Metric>>>,
        kNormalConvPair<algos::cfd::RawCFD>,
        kNormalConvPair<std::shared_ptr<pac::model::IDomain>>,
        kNormalConvPair<config::CustomMetricOptionType>,
        kNormalConvPair<config::CustomMetricsOptionType>,
        kNormalConvPair<config::CustomVectorMetricOptionType>,
        kEnumConvPair<algos::Binop>,
        kEnumConvPair<algos::afd_metric_calculator::AFDMetric>,
        kEnumConvPair<algos::cind::AlgoType>,
        kEnumConvPair<algos::cind::CondType>,
        kEnumConvPair<algos::metric::MetricAlgo>,
        kEnumConvPair<algos::metric::Metric>,
        kEnumConvPair<model::InputFormatType>,
        kEnumConvPair<algos::cfd::Substrategy>,
        kEnumConvPair<algos::hymd::LevelDefinition>,
        kEnumConvPair<algos::des::DifferentialStrategy>,
        kEnumConvPair<algos::od::Ordering>,
        kEnumConvPair<config::PfdErrorMeasureType>,
        kEnumConvPair<algos::AfdErrorMeasure>};
}  // namespace

namespace python_bindings {
nb::object OptToPy(std::type_index type, boost::any val) {
    auto const it = kConverters.find(type);
    if (it == kConverters.end()) [[unlikely]] {
        std::ostringstream oss;
        oss << "Unknown option type: " << boost::core::demangle(type.name()) << " (OptToPy)";
        throw std::runtime_error(oss.str());
    }
    return it->second(val);
}
}  // namespace python_bindings

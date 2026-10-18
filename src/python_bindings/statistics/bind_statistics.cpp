#include "python_bindings/statistics/bind_statistics.h"

#include "python_bindings/binding_core.h"

#include <cstring>

#include "core/algorithms/statistics/data_stats.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
namespace nb = nanobind;
}  // namespace

namespace nanobind::detail {
template <>
struct type_caster<algos::Statistic> {
    NB_TYPE_CASTER(algos::Statistic, const_name("float | int | str"));

    bool from_python(handle, uint32_t, cleanup_list*) noexcept {
        return false;
    }

    static handle from_cpp(algos::Statistic const& stat, rv_policy, cleanup_list*) noexcept {
        using model::Type, model::TypeId, model::Double, model::Int, model::String;
        std::byte const* data = stat.GetData();
        Type const* type = stat.GetType();

        if (!stat.HasValue()) return none().release();

        switch (type->GetTypeId()) {
            case TypeId::kDouble:
                return PyFloat_FromDouble(Type::GetValue<Double>(data));
            case TypeId::kInt:
                return PyLong_FromLongLong(Type::GetValue<Int>(data));
            case TypeId::kString: {
                auto const& str = Type::GetValue<String>(data);
                return PyUnicode_FromStringAndSize(str.data(), str.size());
            }
            case TypeId::kBool: {
                bool v;
                std::memcpy(&v, stat.GetData(), sizeof(bool));
                return PyBool_FromLong(static_cast<long>(v));
            }
            default:
                assert(false);
                __builtin_unreachable();
        }
    }
};
}  // namespace nanobind::detail

namespace python_bindings {
void BindStatistics(nb::module_& main_module) {
    using namespace algos;

    auto statistics_module = main_module.def_submodule("statistics");

    BindPrimitiveNoBase<DataStats>(statistics_module, "DataStats")
            .def("get_all_statistics_as_string", &DataStats::ToString)
            .def("get_number_of_values", &DataStats::NumberOfValues,
                 "Get number of values in the column.", nb::arg("index"))
            .def("get_number_of_columns", &DataStats::GetNumberOfColumns,
                 "Get number of columns in the table.")
            .def("get_null_columns", &DataStats::GetNullColumns,
                 "Get indices of columns with only null values.")
            .def("get_columns_with_null", &DataStats::GetColumnsWithNull,
                 "Get indices of columns which contain null value.")
            .def("get_columns_with_all_unique_values", &DataStats::GetColumnsWithUniqueValues,
                 "Get indices of columns where all values are distinct.")
            .def("get_number_of_distinct", &DataStats::Distinct,
                 "Get number of unique values in the column.", nb::arg("index"))
            .def("is_categorical", &DataStats::IsCategorical,
                 "Check if quantity is greater than number of unique values in the "
                 "column.",
                 nb::arg("index"), nb::arg("quantity"))
            .def("show_sample", &DataStats::ShowSample, nb::arg("start_row"), nb::arg("end_row"),
                 nb::arg("start_col"), nb::arg("end_col"),
                 "Returns a table slice containing values from rows in the range [start_row, "
                 "end_row] and columns in the range [start_col, end_col]. Data values are "
                 "converted to strings.")
            .def("get_average", &DataStats::GetAvg,
                 "Returns average value in the column if it's numeric.", nb::arg("index"))
            .def("get_corrected_std", &DataStats::GetCorrectedSTD,
                 "Returns corrected standard deviation of the column if it's "
                 "numeric.",
                 nb::arg("index"))
            .def("get_skewness", &DataStats::GetSkewness,
                 "Returns skewness of the column if it's numeric.", nb::arg("index"))
            .def("get_kurtosis", &DataStats::GetKurtosis,
                 "Returns kurtosis of the column if it's numeric.", nb::arg("index"))
            .def(
                    "get_min",
                    [](DataStats const& data_stats, std::size_t index) {
                        return data_stats.GetMin(index);
                    },
                    "Returns minimum value of the column.", nb::arg("index"))
            .def("get_max", &DataStats::GetMax, "Returns maximumin value of the column.",
                 nb::arg("index"))
            .def("get_sum", &DataStats::GetSum,
                 "Returns sum of the column's values if it's numeric.", nb::arg("index"))
            .def("get_quantile", &DataStats::GetQuantile,
                 "Returns quantile of the column if its type is comparable.", nb::arg("part"),
                 nb::arg("index"), nb::arg("calc_all") = false)
            .def("get_number_of_zeros", &DataStats::GetNumberOfZeros,
                 "Returns number of zeros in the column if it's numeric.", nb::arg("index"))
            .def("get_number_of_negatives", &DataStats::GetNumberOfNegatives,
                 "Returns number of negative numbers in the column if it's "
                 "numeric.",
                 nb::arg("index"))
            .def("get_sum_of_squares", &DataStats::GetSumOfSquares,
                 "Returns sum of numbers' squares in the column if it's numeric.", nb::arg("index"))
            .def("get_geometric_mean", &DataStats::GetGeometricMean,
                 "Returns geometric mean of numbers in the column if it's numeric.",
                 nb::arg("index"))
            .def("get_mean_ad", &DataStats::GetMeanAD,
                 "Returns mean absolute deviation of the column if it's numeric.", nb::arg("index"))
            .def("get_median", &DataStats::GetMedian,
                 "Returns median of the column if it's numeric.", nb::arg("index"))
            .def("get_median_ad", &DataStats::GetMedianAD,
                 "Returns meadian absolute deviation of the column if it's "
                 "numeric.",
                 nb::arg("index"))
            .def("get_num_nulls", &DataStats::GetNumNulls, "Returns number of nulls in the column.",
                 nb::arg("index"))
            .def("get_vocab", &DataStats::GetVocab,
                 "Returns all the symbols of the columns as a sorted string.", nb::arg("index"))
            .def("get_number_of_non_letter_chars", &DataStats::GetNumberOfNonLetterChars,
                 "Returns number of non-letter chars in a string column.", nb::arg("index"))
            .def("get_number_of_digit_chars", &DataStats::GetNumberOfDigitChars,
                 "Returns number of digit chars in a string column.", nb::arg("index"))
            .def("get_number_of_lowercase_chars", &DataStats::GetNumberOfLowercaseChars,
                 "Returns number of lowercase chars in a string column.", nb::arg("index"))
            .def("get_number_of_uppercase_chars", &DataStats::GetNumberOfUppercaseChars,
                 "Returns number of uppercase chars in a string column.", nb::arg("index"))
            .def("get_number_of_chars", &DataStats::GetNumberOfChars,
                 "Returns total number of characters in a string column.", nb::arg("index"))
            .def("get_avg_number_of_chars", &DataStats::GetAvgNumberOfChars,
                 "Returns average number of chars in a string column.", nb::arg("index"))
            .def("get_words", &DataStats::GetWords,
                 "Returns all distinct words of the column as a set of strings.", nb::arg("index"))
            .def("get_top_k_chars", &DataStats::GetTopKChars,
                 "Returns top k most frequent chars in a string column as a vector of chars.",
                 nb::arg("index"), nb::arg("k"))
            .def("get_top_k_words", &DataStats::GetTopKWords,
                 "Returns top k most frequent words in a string column as a vector of strings.",
                 nb::arg("index"), nb::arg("k"))
            .def("get_min_number_of_chars", &DataStats::GetMinNumberOfChars,
                 "Returns the minimal amount of chars in a column.", nb::arg("index"))
            .def("get_max_number_of_chars", &DataStats::GetMaxNumberOfChars,
                 "Returns the maximal amount of chars in a column.", nb::arg("index"))
            .def("get_min_number_of_words", &DataStats::GetMinNumberOfWords,
                 "Returns the minimal amount of words in a column.", nb::arg("index"))
            .def("get_max_number_of_words", &DataStats::GetMaxNumberOfWords,
                 "Returns the maximal amount of words in a column.", nb::arg("index"))
            .def("get_number_of_words", &DataStats::GetNumberOfWords,
                 "Returns the total amount of words in a column", nb::arg("index"))
            .def("get_number_of_entirely_lowercase_words",
                 &DataStats::GetNumberOfEntirelyLowercaseWords,
                 "Returns the amount of entirely lowercase words in a column.", nb::arg("index"))
            .def("get_number_of_entirely_uppercase_words",
                 &DataStats::GetNumberOfEntirelyUppercaseWords,
                 "Returns the amount of entirely uppercase words in a column.", nb::arg("index"))
            .def("get_interquartile_range", &DataStats::GetInterquartileRange,
                 "Returns interquartile range (IQR) for numeric column.", nb::arg("index"))
            .def("get_coefficient_of_variation", &DataStats::GetCoefficientOfVariation,
                 "Returns coefficient of variation (std/mean) for numeric column.",
                 nb::arg("index"))
            .def("get_monotonicity", &DataStats::GetMonotonicity,
                 "Returns monotonicity flag: 'ascending', 'descending', 'equal' or 'none'.",
                 nb::arg("index"))
            .def("get_jarque_bera_statistic", &DataStats::GetJarqueBeraStatistic,
                 "Returns Jarque-Bera statistic for normality test.", nb::arg("index"))
            .def("get_entropy", &DataStats::GetEntropy,
                 "Returns Shannon entropy for categorical column.", nb::arg("index"))
            .def("get_gini_coefficient", &DataStats::GetGiniCoefficient,
                 "Returns Gini coefficient for categorical column.", nb::arg("index"))
            .def("get_whitespace_only_count", &DataStats::GetWhitespaceOnlyCount,
                 "Returns the number of rows that consist only of whitespace characters (spaces "
                 "and tabs).",
                 nb::arg("index"))
            .def("get_leading_whitespace_count", &DataStats::GetNumberOfRowsWithLeadingWhitespace,
                 "Returns the number of rows that have leading whitespace characters.",
                 nb::arg("index"))
            .def("get_trailing_whitespace_count", &DataStats::GetNumberOfRowsWithTrailingWhitespace,
                 "Returns the number of rows that have trailing whitespace characters.",
                 nb::arg("index"))
            .def("get_special_chars_count", &DataStats::GetNumberOfRowsWithSpecialChars,
                 "Returns the number of rows that contain special characters.", nb::arg("index"))
            .def("get_first_char_frequency", &DataStats::GetFirstCharFrequency,
                 "Returns the most frequent first character and its count as a string in format.",
                 nb::arg("index"))
            .def("get_last_char_frequency", &DataStats::GetLastCharFrequency,
                 "Returns the most frequent last character and its count as a string in format.",
                 nb::arg("index"))
            .def("get_min_white_spaces", &DataStats::GetMinWhiteSpaces,
                 "Returns minimum number of whitespaces in a string column.", nb::arg("index"))
            .def("get_max_white_spaces", &DataStats::GetMaxWhiteSpaces,
                 "Returns maximum number of whitespaces in a string column.", nb::arg("index"))
            .def("get_true_count", &DataStats::GetTrueCount,
                 "Returns number of true values in a boolean column.", nb::arg("index"))
            .def("get_false_count", &DataStats::GetFalseCount,
                 "Returns number of false values in a boolean column.", nb::arg("index"))
            .def("get_zero_percent", &DataStats::GetZeroPercent,
                 "Returns fraction of zero values in a numeric column.", nb::arg("index"));
}
}  // namespace python_bindings

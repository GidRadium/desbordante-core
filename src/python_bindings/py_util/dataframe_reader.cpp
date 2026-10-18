#include "python_bindings/py_util/dataframe_reader.h"

#include "python_bindings/binding_core.h"
#include <Python.h>

#include <string>
#include <utility>
#include <vector>

#include "core/model/types/builtin.h"

namespace python_bindings {

namespace nb = nanobind;

static std::vector<std::string> GetColumnNames(nb::handle dataframe) {
    std::vector<std::string> names;
    nb::list name_lst = nb::cast<nb::list>(dataframe.attr("columns").attr("to_list")());
    for (nb::handle element : name_lst) {
        names.emplace_back(nb::cast<std::string>(nb::str(element)));
    }
    return names;
}

DataframeReaderBase::DataframeReaderBase(nb::handle dataframe, std::string name)
    : dataframe_(nb::borrow<nb::object>(dataframe)),
      df_iter_(nb::cast<nb::iterator>(dataframe_.attr("itertuples")(false, nb::none{}))),
      name_(std::move(name)),
      column_names_(GetColumnNames(dataframe_)) {}

void DataframeReaderBase::Reset() {
    df_iter_ = nb::cast<nb::iterator>(dataframe_.attr("itertuples")(false, nb::none{}));
}

std::string DataframeReaderBase::GetRelationName() const {
    return name_;
}

std::string DataframeReaderBase::GetColumnName(size_t index) const {
    return column_names_.at(index);
}

size_t DataframeReaderBase::GetNumberOfColumns() const {
    return column_names_.size();
}

bool DataframeReaderBase::HasNextRow() const {
    return df_iter_ != nb::iterator::sentinel();
}

std::vector<std::string> StringDataframeReader::GetNextRow() {
    return nb::cast<std::vector<std::string>>(*df_iter_++);
}

std::vector<std::string> ArbitraryDataframeReader::GetNextRow() {
    std::vector<std::string> strings{};
    auto tuple_row = nb::borrow<nb::object>(*df_iter_);
    ++df_iter_;
    for (nb::handle el : tuple_row) {
        // When reading from .csv files, pandas treats some values as nulls,
        // and empty values are among those values, which may cause some
        // confusion here, since in Desbordante only the literal "NULL" string
        // is interpreted as the null value.
        strings.emplace_back(is_null_(el) ? std::string(model::Null::kValue)
                                          : nb::cast<std::string>(nb::str(el)));
    }
    return strings;
}

}  // namespace python_bindings

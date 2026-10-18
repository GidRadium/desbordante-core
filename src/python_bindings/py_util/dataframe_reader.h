#pragma once

#include "python_bindings/binding_core.h"

#include "core/model/table/idataset_stream.h"

namespace python_bindings {

class DataframeReaderBase : public model::IDatasetStream {
protected:
    nb::object dataframe_;
    nb::iterator df_iter_;
    std::string name_;
    std::vector<std::string> column_names_;

public:
    explicit DataframeReaderBase(nb::handle dataframe, std::string name = "Pandas dataframe");

    void Reset() final;
    [[nodiscard]] std::string GetRelationName() const final;
    [[nodiscard]] std::string GetColumnName(size_t index) const final;
    [[nodiscard]] size_t GetNumberOfColumns() const final;
    [[nodiscard]] bool HasNextRow() const final;
};

// If a DataFrame only consists of Python strings, then there is no need to get
// string representations of every value, and we can get away with the normal
// nanobind's conversions, which speeds up the reading process.
class StringDataframeReader final : public DataframeReaderBase {
public:
    using DataframeReaderBase::DataframeReaderBase;

    std::vector<std::string> GetNextRow() final;
};

// If a dataframe consists of arbitrary Python objects, we have to first check
// for nullity and switch it out for Desbordante's null value if it is null.
// Pandas uses several Python objects for its null value representation, so the
// most robust way to check for a DataFrame's value nullity is to use the
// `pandas.isna` function.
// If the value is not null, then we have to convert it to std::string, which
// involves converting it to a Python str first.
class ArbitraryDataframeReader final : public DataframeReaderBase {
private:
    // Used in GetNextRow to check arbitrary dataframe values for nullity.
    // Statically storing this may cause problems with garbage collection, but
    // importing a module on every call may cause performance problems, and
    // storing it as a field is a compromise, which is used here.
    std::function<bool(nb::handle)> is_null_ =
            [check_null = nb::object(nb::module_::import_("pandas").attr("isna"))](nb::handle obj) {
                return nb::cast<bool>(check_null(obj));
            };

public:
    using DataframeReaderBase::DataframeReaderBase;

    [[nodiscard]] std::vector<std::string> GetNextRow() final;
};

}  // namespace python_bindings

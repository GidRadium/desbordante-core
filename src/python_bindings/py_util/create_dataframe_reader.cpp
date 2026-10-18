#include "python_bindings/py_util/create_dataframe_reader.h"

#include "core/config/exceptions.h"
#include "core/model/index.h"
#include "python_bindings/py_util/dataframe_reader.h"

namespace python_bindings {

namespace nb = nanobind;

static bool IsDataFrame(nb::handle object) {
    try {
        return nb::isinstance(object, nb::module_::import_("pandas").attr("DataFrame"));
    } catch (nb::python_error& e) {
        if (e.matches(PyExc_ImportError)) {
            return false;
        } else {
            throw;
        }
    }
}

static bool AllColumnsAreStrings(nb::handle dataframe) {
    nb::object dtypes = dataframe.attr("dtypes");
    return nb::cast<bool>(dtypes[dtypes.attr("__ne__")(nb::str{"string"})].attr("empty"));
}

config::InputTable CreateDataFrameReader(nb::handle dataframe, model::Index object_index) {
    if (!IsDataFrame(dataframe))
        throw config::ConfigurationError("Passed object is not a dataframe");
    std::string name = [&]() -> std::string {
        try {
            return nb::cast<std::string>(nb::str(dataframe.attr("attrs")["name"]));
        } catch (nb::python_error& e) {
            if (!(e.matches(PyExc_KeyError) || e.matches(PyExc_AttributeError))) throw;
            return object_index == kSingleDataFrame
                           ? "Pandas dataframe"
                           : "Pandas dataframe (" + std::to_string(object_index) + ")";
        };
    }();
    if (AllColumnsAreStrings(dataframe)) {
        return std::make_shared<StringDataframeReader>(dataframe, std::move(name));
    } else {
        return std::make_shared<ArbitraryDataframeReader>(dataframe, std::move(name));
    }
}

}  // namespace python_bindings

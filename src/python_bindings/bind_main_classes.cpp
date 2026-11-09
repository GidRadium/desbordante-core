#include "python_bindings/bind_main_classes.h"

#include "python_bindings/binding_core.h"

#include <typeindex>
#include <typeinfo>

#include "core/algorithms/algo_factory.h"
#include "core/algorithms/algorithm.h"
#include "core/config/exceptions.h"
#include "core/config/tabular_data/input_tables_type.h"
#include "core/model/sequence/isequence_stream.h"
#include "python_bindings/py_util/get_py_type.h"
#include "python_bindings/py_util/opt_to_py.h"
#include "python_bindings/py_util/py_to_any.h"

namespace configure_algorithm {
namespace nb = nanobind;
using algos::Algorithm;
auto const kVoidIndex = std::type_index{typeid(void)};

void ConfigureAlgo(Algorithm& algorithm, nb::dict const& kwargs) {
    using python_bindings::PyToAny;
    algos::ConfigureFromFunction(
            algorithm, [&kwargs, &algorithm](std::string_view option_name) -> boost::any {
                std::type_index type_index = algorithm.GetTypeIndex(option_name);
                assert(type_index != kVoidIndex);
                return kwargs.contains(option_name.data())
                               ? PyToAny(option_name, type_index, kwargs[option_name.data()])
                               : boost::any{};
            });
}
}  // namespace configure_algorithm

using namespace configure_algorithm;

namespace python_bindings {
void BindMainClasses(nb::module_& main_module) {
    using namespace nb::literals;

    main_module.doc() =
            "A high-performance data profiling library oriented towards exploratory data analysis";

    nb::exception<config::ConfigurationError>(main_module, "ConfigurationError", PyExc_ValueError);

#define INTERNAL_OPTIONS_METHOD                                                    \
    "\nThis option is only expected to be used by internal development Python\n"   \
    "scripts. For normal use, you can set the algorithms' options using keyword\n" \
    "arguments of the load_data and execute methods."
    nb::class_<Algorithm>(main_module, "Algorithm")
            .def(
                    "_set_option",
                    [](Algorithm& algorithm, std::string_view option_name,
                       nb::handle option_value) {
                        if (option_value.is_none()) {
                            algorithm.SetOption(option_name);
                            return;
                        }
                        algorithm.SetOption(
                                option_name,
                                PyToAny(option_name, algorithm.GetTypeIndex(option_name),
                                        option_value));
                    },
                    "option_name"_a, "option_value"_a = nb::none(),
                    "Set option value. Passing None means setting the default "
                    "value." INTERNAL_OPTIONS_METHOD)
            .def("_get_needed_options", &Algorithm::GetNeededOptions,
                 "Get names of options the algorithm requires to be set at the "
                 "moment." INTERNAL_OPTIONS_METHOD)
            .def(
                    "_get_option_type",
                    [](Algorithm const& algo, std::string_view option_name) {
                        auto type_index = algo.GetTypeIndex(option_name);
                        if (type_index == kVoidIndex)
                            throw config::ConfigurationError{std::string{"Option named \""} +
                                                             option_name.data() +
                                                             "\" doesn't exist!"};
                        return GetPyType(type_index);
                    },
                    "option_name"_a, "Get info about the option's type." INTERNAL_OPTIONS_METHOD)
            .def(
                    "load_data",
                    [](Algorithm& algo, nb::kwargs const& kwargs) {
                        ConfigureAlgo(algo, kwargs);
                        algo.LoadData();
                    },
                    "Load data for execution")
            .def("get_possible_options", &Algorithm::GetPossibleOptions,
                 "Get names of options the algorithm may request.")
            .def("get_description", &Algorithm::GetDescription, "option_name"_a,
                 "Get description of an option.")
            .def(
                    "get_opts",
                    [](Algorithm& algorithm) {
                        // Input streams are implementation details, not values that can be
                        // represented by a public option type.  Skip them by type so that
                        // table, tables, left_table, right_table and sequence do not make
                        // get_opts() fail after the data has been loaded.
                        static std::type_index const kInputTableType{typeid(config::InputTable)};
                        static std::type_index const kInputTablesType{typeid(config::InputTables)};
                        static std::type_index const kSequenceType{
                                typeid(std::shared_ptr<model::ISequenceStream>)};

                        auto opt_value_info = algorithm.GetOptValues();
                        std::unordered_map<std::string_view, nb::object> res;
                        for (auto const& [name, value_info] : opt_value_info) {
                            if (value_info.type == kInputTableType ||
                                value_info.type == kInputTablesType ||
                                value_info.type == kSequenceType) {
                                continue;
                            }
                            res[name] = OptToPy(value_info.type, value_info.value);
                        }
                        return res;
                    },
                    "Get option values represented as the closest Python type")
            .def(
                    "execute",
                    [](Algorithm& algo, nb::kwargs const& kwargs) {
                        ConfigureAlgo(algo, kwargs);
                        return algo.Execute();
                    },
                    "Process data.");
#undef INTERNAL_OPTIONS_METHOD
}
}  // namespace python_bindings

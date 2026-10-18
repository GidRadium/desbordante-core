#include "python_bindings/ac/bind_ac.h"

#include "python_bindings/binding_core.h"

#include <magic_enum/magic_enum.hpp>

#include "core/algorithms/algebraic_constraints/ac.h"
#include "core/algorithms/algebraic_constraints/ac_algorithm.h"
#include "core/model/types/create_type.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace {
std::unique_ptr<model::INumericType> CreateNumericTypeFromTypeId(model::TypeId type_id) {
    std::unique_ptr<model::Type> type_ptr = model::CreateType(type_id, false);
    return std::unique_ptr<model::INumericType>(
            dynamic_cast<model::INumericType*>(type_ptr.release()));
}

namespace nb = nanobind;
}  // namespace

namespace python_bindings {
void BindAc(nb::module_& main_module) {
    using namespace algos;

    auto ac_module = main_module.def_submodule("ac");
    nb::class_<ACException>(ac_module, "ACException")
            .def_ro("row_index", &ACException::row_i)
            .def_ro("column_pairs", &ACException::column_pairs)
            .def("__getstate__",
                 [](ACException const& exc) { return nb::make_tuple(exc.row_i, exc.column_pairs); })
            .def("__setstate__", [](ACException& exc, nb::tuple t) {
                if (t.size() != 2) {
                    throw std::runtime_error("Invalid state for ACException pickle!");
                }
                size_t row_index = nb::cast<size_t>(t[0]);
                auto column_pairs = nb::cast<std::vector<std::pair<size_t, size_t>>>(t[1]);
                new (&exc) ACException(row_index, std::move(column_pairs));
            });

    nb::class_<RangesCollection>(ac_module, "ACRanges")
            .def_prop_ro("column_indices",
                         [](RangesCollection const& ranges) { return ranges.col_pair.col_i; })
            .def_prop_ro("ranges",
                         [](RangesCollection const& ranges) {
                             std::vector<std::pair<nb::float_, nb::float_>> res;
                             res.reserve(ranges.ranges.size() / 2);
                             assert(ranges.ranges.size() % 2 == 0);
                             for (size_t i = 0; i < ranges.ranges.size(); i += 2) {
                                 // TODO: change this once a proper conversion mechanism from
                                 // `model::INumericType` is implemented
                                 std::string l_endpoint =
                                         ranges.col_pair.num_type->ValueToString(ranges.ranges[i]);
                                 std::string r_endpoint = ranges.col_pair.num_type->ValueToString(
                                         ranges.ranges[i + 1]);
                                 auto to_float = [](std::string const& s) {
                                     nb::str py_str(s.c_str(), s.size());
                                     return nb::steal<nb::float_>(PyFloat_FromString(py_str.ptr()));
                                 };
                                 res.emplace_back(to_float(l_endpoint), to_float(r_endpoint));
                             }
                             return res;
                         })
            .def("__getstate__",
                 [](RangesCollection const& obj) {
                     auto col_indices = obj.col_pair.col_i;
                     int type_id_int = static_cast<int>(obj.col_pair.num_type->GetTypeId());
                     std::vector<std::string> s_ranges;
                     s_ranges.reserve(obj.ranges.size());
                     for (auto ptr : obj.ranges) {
                         s_ranges.push_back(obj.col_pair.num_type->ValueToString(ptr));
                     }
                     return nb::make_tuple(std::move(col_indices), type_id_int,
                                           std::move(s_ranges));
                 })
            .def("__setstate__", [](RangesCollection& obj, nb::tuple t) {
                if (t.size() != 3) {
                    throw std::runtime_error("Invalid state for ACRanges pickle!");
                }
                auto col_indices = nb::cast<std::pair<size_t, size_t>>(t[0]);
                int type_id_int = nb::cast<int>(t[1]);
                auto s_ranges = nb::cast<std::vector<std::string>>(t[2]);
                auto type_id = magic_enum::enum_cast<model::TypeId>(type_id_int)
                                       .value_or(model::TypeId::kUndefined);
                std::unique_ptr<model::INumericType> num_type =
                        CreateNumericTypeFromTypeId(type_id);
                std::vector<std::byte const*> ranges;
                ranges.reserve(s_ranges.size());
                for (auto const& s : s_ranges) {
                    size_t size = num_type->GetSize();
                    std::byte* buf = new std::byte[size];
                    num_type->ValueFromStr(buf, s);
                    ranges.push_back(buf);
                }
                new (&obj) RangesCollection(std::move(num_type), std::move(ranges),
                                            col_indices.first, col_indices.second);
            });

    BindPrimitiveNoBase<ACAlgorithm>(ac_module, "AcAlgorithm")
            .def("get_ac_ranges", &ACAlgorithm::GetRangesCollections,
                 nb::rv_policy::reference_internal)
            .def(
                    "get_ac_exceptions",
                    [](ACAlgorithm& algo) {
                        algo.CollectACExceptions();
                        return algo.GetACExceptions();
                    },
                    nb::rv_policy::reference_internal);
}
}  // namespace python_bindings

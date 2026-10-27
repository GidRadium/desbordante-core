#include "python_bindings/binding_core.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/algorithms/nar/des/des.h"
#include "core/algorithms/nar/nar.h"
#include "core/algorithms/nar/value_range.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace nb = nanobind;

namespace {
constexpr double kRoundingValue = 1e9;
}  // namespace

namespace nar_serialization {
using NAR = model::NAR;

nb::object SerializeValueRange(std::shared_ptr<model::ValueRange> const& vr) {
    switch (auto const type_id = vr->GetTypeId()) {
        case model::TypeId::kString: {
            auto svr = std::dynamic_pointer_cast<model::StringValueRange>(vr);
            return nb::make_tuple(magic_enum::enum_integer(type_id), svr->domain);
        }
        case model::TypeId::kDouble: {
            auto nvr = std::dynamic_pointer_cast<model::NumericValueRange<model::Double>>(vr);
            return nb::make_tuple(magic_enum::enum_integer(type_id),
                                  nb::make_tuple(nvr->lower_bound, nvr->upper_bound));
        }
        case model::TypeId::kInt: {
            auto nvr = std::dynamic_pointer_cast<model::NumericValueRange<model::Int>>(vr);
            return nb::make_tuple(magic_enum::enum_integer(type_id),
                                  nb::make_tuple(nvr->lower_bound, nvr->upper_bound));
        }

        default:
            throw std::runtime_error(std::string("Unsupported type: ") +
                                     std::string(magic_enum::enum_name(type_id)));
    }

    throw std::runtime_error("Unsupported ValueRange type in serialization.");
}

int DeserializeTypeId(nb::handle obj) {
    // `TypeId` has a `char` underlying type.  pybind11 (and nanobind, which
    // follows it) convert a `char` to a one-character string, so the baseline
    // streams store the type as e.g. "\x00".  Accept an integer as well to
    // keep streams produced by a build with an integer enum conversion
    // readable.
    if (nb::isinstance<nb::str>(obj)) {
        std::string type_str = nb::cast<std::string>(obj);
        if (type_str.size() != 1) {
            throw std::runtime_error("Invalid ValueRange TypeId in NAR pickle!");
        }
        return static_cast<unsigned char>(type_str[0]);
    }
    if (nb::isinstance<nb::int_>(obj)) {
        return nb::cast<int>(obj);
    }
    throw std::runtime_error("Invalid ValueRange TypeId in NAR pickle!");
}

std::shared_ptr<model::ValueRange> DeserializeValueRange(nb::object const& obj) {
    auto tup = nb::cast<nb::tuple>(obj);
    if (tup.size() != 2) {
        throw std::runtime_error("Invalid ValueRange tuple size!");
    }
    int type_code = DeserializeTypeId(tup[0]);
    model::TypeId type_id =
            magic_enum::enum_cast<model::TypeId>(type_code).value_or(model::TypeId::kUndefined);
    switch (type_id) {
        case model::TypeId::kString: {
            auto domain = nb::cast<std::vector<std::string>>(tup[1]);
            return std::make_shared<model::StringValueRange>(std::move(domain));
        }
        case model::TypeId::kDouble: {
            auto bounds = nb::cast<nb::tuple>(tup[1]);
            model::Double lb = nb::cast<model::Double>(bounds[0]);
            model::Double ub = nb::cast<model::Double>(bounds[1]);
            return std::make_shared<model::NumericValueRange<model::Double>>(lb, ub);
        }
        case model::TypeId::kInt: {
            auto bounds = nb::cast<nb::tuple>(tup[1]);
            model::Int lb = nb::cast<model::Int>(bounds[0]);
            model::Int ub = nb::cast<model::Int>(bounds[1]);
            return std::make_shared<model::NumericValueRange<model::Int>>(lb, ub);
        }
        default: {
            throw std::runtime_error("Unsupported ValueRange type in deserialization.");
        }
    }
}

nb::dict SerializeRangeMap(
        std::unordered_map<size_t, std::shared_ptr<model::ValueRange>> const& map) {
    nb::dict d;
    for (auto const& [feature_idx, vr_ptr] : map) {
        d[nb::int_(feature_idx)] = SerializeValueRange(vr_ptr);
    }
    return d;
}

nb::tuple SerializeNar(NAR const& nar) {
    auto quals = nar.GetQualities();
    nb::dict ante_dict = nar_serialization::SerializeRangeMap(nar.GetAnte());
    nb::dict cons_dict = nar_serialization::SerializeRangeMap(nar.GetCons());
    return nb::make_tuple(quals.fitness, quals.support, quals.confidence,
                          nar.IsQualitiesConsistent(), std::move(ante_dict), std::move(cons_dict));
}

std::unordered_map<size_t, std::shared_ptr<model::ValueRange>> DeserializeRangeMap(
        nb::dict const& d) {
    std::unordered_map<size_t, std::shared_ptr<model::ValueRange>> map;
    for (auto item : d) {
        auto feature_idx = nb::cast<size_t>(item.first);
        auto vr = DeserializeValueRange(nb::borrow<nb::object>(item.second));
        map[feature_idx] = vr;
    }
    return map;
}

NAR DeserializeNar(nb::tuple t) {
    if (t.size() != 6) {
        throw std::runtime_error("Invalid state for NAR pickle!");
    }
    double fitness = nb::cast<double>(t[0]);
    double support = nb::cast<double>(t[1]);
    double confidence = nb::cast<double>(t[2]);
    bool qualities_consistent = nb::cast<bool>(t[3]);
    nb::dict ante_dict = nb::cast<nb::dict>(t[4]);
    nb::dict cons_dict = nb::cast<nb::dict>(t[5]);
    NAR nar;
    std::unordered_map<size_t, std::shared_ptr<model::ValueRange>> ante_map =
            nar_serialization::DeserializeRangeMap(ante_dict);
    std::unordered_map<size_t, std::shared_ptr<model::ValueRange>> cons_map =
            nar_serialization::DeserializeRangeMap(cons_dict);
    for (auto const& p : ante_map) {
        nar.InsertInAnte(p.first, p.second);
    }
    for (auto const& p : cons_map) {
        nar.InsertInCons(p.first, p.second);
    }
    nar.SetQualitiesDirect(fitness, support, confidence);
    nar.SetQualitiesConsistent(qualities_consistent);
    return nar;
}

nb::tuple ConvertValueRangeToImmutableTuple(std::shared_ptr<model::ValueRange> const& vr) {
    auto const type_code = vr->GetTypeId();
    switch (type_code) {
        case model::TypeId::kString: {
            auto svr = std::dynamic_pointer_cast<model::StringValueRange>(vr);
            nb::tuple domain_tuple = python_bindings::VectorToTuple(svr->domain);

            return nb::make_tuple(magic_enum::enum_integer(type_code), domain_tuple);
        }
        case model::TypeId::kDouble: {
            auto nvr = std::dynamic_pointer_cast<model::NumericValueRange<model::Double>>(vr);
            return nb::make_tuple(magic_enum::enum_integer(type_code),
                                  nb::make_tuple(nvr->lower_bound, nvr->upper_bound));
        }
        case model::TypeId::kInt: {
            auto nvr = std::dynamic_pointer_cast<model::NumericValueRange<model::Int>>(vr);
            return nb::make_tuple(magic_enum::enum_integer(type_code),
                                  nb::make_tuple(nvr->lower_bound, nvr->upper_bound));
        }
        default: {
            throw std::runtime_error("Unsupported ValueRange type to hash.");
        }
    }
}

nb::tuple ConvertRangeMapToImmutableTuple(
        std::unordered_map<size_t, std::shared_ptr<model::ValueRange>> const& map) {
    std::vector<std::pair<size_t, std::shared_ptr<model::ValueRange>>> sorted_ranges;
    sorted_ranges.reserve(map.size());
    for (auto const& [key, value] : map) {
        sorted_ranges.emplace_back(key, value);
    }
    std::ranges::sort(sorted_ranges, [](auto const& elem1, auto const& elem2) {
        return elem1.first < elem2.first;
    });
    return python_bindings::VectorToTuple(sorted_ranges, [](auto const& elem) {
        return nb::make_tuple(elem.first, ConvertValueRangeToImmutableTuple(elem.second));
    });
}

nb::tuple ConvertNarToImmutableTuple(NAR const& nar) {
    nb::tuple ante_tuple = ConvertRangeMapToImmutableTuple(nar.GetAnte());
    nb::tuple cons_tuple = ConvertRangeMapToImmutableTuple(nar.GetCons());

    bool is_consistent = nar.IsQualitiesConsistent();
    if (!is_consistent) {
        return nb::make_tuple(std::move(ante_tuple), std::move(cons_tuple), is_consistent);
    }
    auto const& qualities = nar.GetQualities();

    auto round_double_val = [](double d) {
        return std::round(d * kRoundingValue) / kRoundingValue;
    };

    return nb::make_tuple(std::move(ante_tuple), std::move(cons_tuple), is_consistent,
                          round_double_val(qualities.fitness), round_double_val(qualities.support),
                          round_double_val(qualities.confidence));
}

}  // namespace nar_serialization

namespace python_bindings {

void BindNar(nb::module_& main_module) {
    using namespace algos;
    using namespace algos::des;
    using namespace model;

    auto nar_module = main_module.def_submodule("nar");

    nb::class_<ValueRange>(nar_module, "ValueRange")
            .def("__str__", &ValueRange::ToString)
            .def_prop_ro("type", &ValueRange::GetTypeId);

    nb::class_<StringValueRange, ValueRange>(nar_module, "StringValueRange")
            .def("__str__", &StringValueRange::ToString)
            .def_ro("string", &StringValueRange::domain);

    nb::class_<NumericValueRange<Double>, ValueRange>(nar_module, "FloatValueRange")
            .def("__str__", &NumericValueRange<Double>::ToString)
            .def_ro("lower_bound", &NumericValueRange<Double>::lower_bound)
            .def_ro("upper_bound", &NumericValueRange<Double>::upper_bound);

    nb::class_<NumericValueRange<Int>, ValueRange>(nar_module, "IntValueRange")
            .def("__str__", &NumericValueRange<Int>::ToString)
            .def_ro("lower_bound", &NumericValueRange<Int>::lower_bound)
            .def_ro("upper_bound", &NumericValueRange<Int>::upper_bound);

    nb::class_<NARQualities>(nar_module, "NarQualities")
            .def("__str__", &NARQualities::ToString)
            .def_ro("support", &NARQualities::support)
            .def_ro("confidence", &NARQualities::confidence)
            .def_ro("fitness", &NARQualities::fitness);

    nb::class_<NAR>(nar_module, "NAR")
            .def("__str__", &NAR::ToString)
            .def_prop_ro("qualities", &NAR::GetQualities)
            .def_prop_ro("support", [](NAR const& n) { return n.GetQualities().support; })
            .def_prop_ro("confidence", [](NAR const& n) { return n.GetQualities().confidence; })
            .def_prop_ro("fitness", [](NAR const& n) { return n.GetQualities().fitness; })
            .def_prop_ro("ante", &NAR::GetAnte)
            .def_prop_ro("cons", &NAR::GetCons)
            .def("__eq__",
                 [](NAR const& nar1, NAR const& nar2) {
                     if (&nar1 == &nar2) {
                         return true;
                     }
                     nb::tuple nar1_state_tuple =
                             nar_serialization::ConvertNarToImmutableTuple(nar1);
                     nb::tuple nar2_state_tuple =
                             nar_serialization::ConvertNarToImmutableTuple(nar2);

                     return nar1_state_tuple.equal(nar2_state_tuple);
                 })
            .def("__hash__",
                 [](NAR const& nar) {
                     nb::tuple state_tuple = nar_serialization::ConvertNarToImmutableTuple(nar);
                     return nb::hash(state_tuple);
                 })
            .def("__getstate__",
                 [](NAR const& nar) { return nar_serialization::SerializeNar(nar); })
            .def("__setstate__", [](NAR& nar, nb::tuple t) {
                new (&nar) NAR(nar_serialization::DeserializeNar(t));
            });

    BindPrimitive<DES>(nar_module, &NARAlgorithm::GetNARVector, "NarAlgorithm", "get_nars",
                       {"DES"});
}
}  // namespace python_bindings

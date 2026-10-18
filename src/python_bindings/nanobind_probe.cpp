#include <nanobind/nanobind.h>

#include <cstddef>
#include <functional>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "python_bindings/binding_core.h"

namespace nb = nanobind;

namespace {

class Probe {
public:
    Probe() = default;

    Probe(std::string name, int value) : name(std::move(name)), value(value) {}

    [[nodiscard]] std::string Describe() const {
        return name + "=" + std::to_string(value);
    }

    [[nodiscard]] std::vector<std::string> Words() const {
        return {name, std::to_string(value)};
    }

    [[nodiscard]] std::size_t Hash() const {
        return std::hash<std::string>{}(name) ^ std::hash<int>{}(value);
    }

    bool operator==(Probe const& other) const {
        return name == other.name && value == other.value;
    }

    std::string name;
    int value = 0;
};

}  // namespace

#if defined(NANOBIND_PROBE_ABI3)
#define NANOBIND_PROBE_MODULE_NAME _nb_probe_abi3
#else
#define NANOBIND_PROBE_MODULE_NAME _nb_probe
#endif

NB_MODULE(NANOBIND_PROBE_MODULE_NAME, m) {
    nb::class_<Probe>(m, "Probe")
            .def(nb::init<>())
            .def(nb::init<std::string, int>(), nb::arg("name"), nb::arg("value"))
            .def_ro("name", &Probe::name)
            .def_rw("value", &Probe::value)
            .def("describe", &Probe::Describe)
            .def("words", &Probe::Words)
            .def("__eq__", [](Probe const& lhs, Probe const& rhs) { return lhs == rhs; })
            .def("__hash__", &Probe::Hash)
            .def("__getstate__",
                 [](Probe const& probe) { return nb::make_tuple(probe.name, probe.value); })
            .def("__setstate__", [](Probe& probe, nb::tuple state) {
                if (state.size() != 2U) {
                    throw std::runtime_error("Invalid Probe state");
                }
                new (&probe) Probe{nb::cast<std::string>(state[0]), nb::cast<int>(state[1])};
            });

    m.def(
            "echo_optional",
            [](std::optional<std::string> const& value) {
                return value.has_value() ? *value : std::string("none");
            },
            nb::arg("x").none());

    m.def(
            "concat",
            [](std::string const& left, std::string const& right) { return left + right; },
            nb::arg("left"), nb::kw_only(), nb::arg("right"));
}

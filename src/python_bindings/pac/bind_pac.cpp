#include "python_bindings/pac/bind_pac.h"

#include "python_bindings/binding_core.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/algorithms/pac/domain_pac.h"
#include "core/config/column_index/type.h"
#include "python_bindings/pac/bind_domains.h"

namespace nb = nanobind;

namespace {
/// @brief Convert Domain PAC to Python tuple, which first element is string representation of
/// Domain, then follow column indices.
/// Epsilon and delta are not included in tuple, because there's no proper way to hash doubles
nb::tuple DomainPACToTuple(model::DomainPAC const& d_pac) {
    auto const column_indices = d_pac.GetColumnIndices();
    nb::tuple_builder builder(column_indices.size() + 1);
    // It's unlikely that set of Domain PACs will contain PACs on different domains, so using
    // Domain's string representation shouldn't lead to a lot of collisions.
    builder.put(d_pac.GetDomainName());
    for (auto column_index : column_indices) {
        builder.put(column_index);
    }
    return builder.commit();
}
}  // namespace

namespace python_bindings {
void BindPAC(nb::module_& main_module) {
    using namespace model;
    using namespace nb::literals;

    auto pac_module = main_module.def_submodule("pac");

    nb::class_<DomainPAC>(pac_module, "DomainPAC")
            .def_prop_ro("epsilon", &DomainPAC::GetEpsilon)
            .def_prop_ro("delta", &DomainPAC::GetDelta)
            .def_prop_ro("domain", &DomainPAC::GetDomainName)
            .def_prop_ro("column_indices", &model::DomainPAC::GetColumnIndices)
            .def_prop_ro("column_names", &model::DomainPAC::GetColumnNames)
            .def("to_short_string", &DomainPAC::ToShortString)
            .def("to_long_string", &DomainPAC::ToLongString)
            .def("__str__", &DomainPAC::ToLongString)
            .def(nb::self == nb::self)
            .def("__hash__",
                 [](DomainPAC const& d_pac) { return nb::hash(DomainPACToTuple(d_pac)); })
            .def("__getstate__",
                 [](DomainPAC const& d_pac) {
                     return nb::make_tuple(d_pac.GetColumnIndices(), d_pac.GetColumnNames(),
                                           d_pac.GetDomainName(), d_pac.GetEpsilon(),
                                           d_pac.GetDelta());
                 })
            .def("__setstate__", [](DomainPAC& d_pac, nb::tuple t) {
                if (t.size() != 5) {
                    throw std::runtime_error("Invalid state for Domain PAC pickle");
                }
                auto column_indices = nb::cast<std::vector<config::IndexType>>(t[0]);
                auto column_names = nb::cast<std::vector<std::string>>(t[1]);
                auto domain_name = nb::cast<std::string>(t[2]);
                auto epsilon = nb::cast<double>(t[3]);
                auto delta = nb::cast<double>(t[4]);
                new (&d_pac) DomainPAC(epsilon, delta, domain_name, std::move(column_indices),
                                       std::move(column_names));
            });

    BindDomains(pac_module);
}
}  // namespace python_bindings

#include "python_bindings/pac/bind_domains.h"

#include "python_bindings/binding_core.h"

#include "core/algorithms/pac/model/default_domains/ball.h"
#include "core/algorithms/pac/model/default_domains/parallelepiped.h"

namespace nb = nanobind;

namespace python_bindings {
void BindDomains(nb::module_& pac_module) {
    using namespace nb::literals;
    using namespace pac::model;

    auto domains_module = pac_module.def_submodule("domains");
    nb::class_<IDomain>(domains_module, "IDomain").def("__str__", &IDomain::ToString).doc() =
            "Abstract base class for domains. Cannot be used directly.\n"
            "Currently available domain types: Ball, Parallelepiped, CustomDomain.\n";
    nb::class_<Ball, IDomain>(domains_module, "Ball")
            .def(nb::init<std::vector<std::string>, double, std::vector<double>>(), "center"_a,
                 "radius"_a, "leveling_coefficients"_a = std::vector<double>{})
            .doc() =
            "A closed n-ary ball, defined by center and radius, i. e. D = {x : dist(center, x) <= "
            "radius}.\n"
            "Uses Euclidean distance:\n"
            "dist(x, y) = sqrt((d_1(x[1], y[1]))^2 + ... + (d_n(x[n], y[n]))^2), where "
            "d_i is a default\n"
            "metric for i-th column's data type multiplied by leveling_coefficients[i] (which "
            "defaults to 1).\n";
    nb::class_<Parallelepiped, IDomain>(domains_module, "Parallelepiped")
            .def(nb::init<std::vector<std::string>, std::vector<std::string>,
                          std::vector<double>>(),
                 "lower_bound"_a, "upper_bound"_a,
                 "leveling_coefficients"_a = std::vector<double>{})
            .def(nb::init<std::string, std::string>(), "lower_bound"_a, "upper_bound"_a)
            .doc() =
            "A closed n-ary parallelepiped, defined by lower and upper bounds, i. e.\n"
            "D = [lower[1], upper[1]] x ... x [lower[n], upper[n]].\n"
            "Uses Chebyshev distance: dist(x, y) = max{d_1(x[1], y[1]), ..., d_n(x[n], y[n])}, "
            "where d_i is a default\n"
            "metric for i-th column's data type multiplied by leveling_coefficients[i] (which "
            "defaults to 1).\n";
    nb::class_<PyDomain, IDomain>(domains_module, "CustomDomain")
            .def(nb::init<nb::object, std::string&&>(), "dist_from_domain"_a,
                 "name"_a = "Custom domain")
            .doc() =
            "Custom domain, defined by metric:\n"
            "\tdist_from_domain: must return distance from domain to a value.\n"
            "Second (optional) argument is domain name, which is used to make Domain PAC's string "
            "representation.\n";
}
}  // namespace python_bindings

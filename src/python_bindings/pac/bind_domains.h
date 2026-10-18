#pragma once

#include "python_bindings/binding_core.h"

#include <string>
#include <utility>

#include "core/algorithms/pac/model/idomain.h"
#include "core/algorithms/pac/model/tuple.h"
#include "python_bindings/py_util/value_to_py.h"

namespace python_bindings {
class PyDomain final : public pac::model::IDomain {
private:
    nb::object dist_from_domain_;
    std::string name_;

public:
    PyDomain(nb::object dist_from_domain, std::string&& name)
        : dist_from_domain_(std::move(dist_from_domain)), name_(std::move(name)) {}

    double DistFromDomain(pac::model::Tuple const& value) const override {
        return nb::cast<double>(dist_from_domain_(ValuesToPy(tuple_type_->GetTypes(), value)));
    }

    std::string ToString() const override {
        return name_;
    }
};

void BindDomains(nb::module_& pac_module);
}  // namespace python_bindings

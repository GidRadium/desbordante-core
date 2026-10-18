#pragma once

#include "python_bindings/binding_core.h"

#include <functional>
#include <vector>

namespace python_bindings {
template <typename ElementType, typename Func>
nb::tuple VectorToTuple(std::vector<ElementType> const& vec, Func&& func) {
    nb::tuple_builder builder(vec.size());
    for (std::size_t i = 0; i < vec.size(); ++i) {
        builder.put(std::invoke(func, vec[i]));
    }
    return builder.commit();
}

template <typename ElementType>
nb::tuple VectorToTuple(std::vector<ElementType> const& vec) {
    nb::tuple_builder builder(vec.size());
    for (std::size_t i = 0; i < vec.size(); i++) {
        builder.put(vec[i]);
    }
    return builder.commit();
}

}  // namespace python_bindings

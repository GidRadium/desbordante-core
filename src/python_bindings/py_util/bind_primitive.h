#pragma once

#include "python_bindings/binding_core.h"

#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <type_traits>

#include "core/algorithms/algorithm.h"
#include "core/util/mem_ptr_deduce.h"

namespace python_bindings {

namespace detail {
template <typename AlgoType>
std::string MakeDocString() {
    auto algo = AlgoType();
    std::stringstream docstring;
    docstring << "Options:\n";
    for (std::string_view option_name : algo.GetPossibleOptions()) {
        docstring << option_name << ": " << algo.GetDescription(option_name) << "\n";
    }
    return docstring.str();
}

template <typename AlgorithmType, typename BaseType>
auto RegisterAlgorithm(nb::module_ module, auto&& name) {
    auto cls = nb::class_<AlgorithmType, BaseType>(module, std::move(name));

    cls.doc() = MakeDocString<AlgorithmType>();
    cls.def(nb::init<>());
    return cls;
}

template <typename T>
struct MemberPointerClassHelper;

template <typename Class, typename Type>
struct MemberPointerClassHelper<Type Class::*> {
    using type = Class;
};

template <typename MemberPointer>
using MemberPointerClass = typename MemberPointerClassHelper<MemberPointer>::type;
}  // namespace detail

template <typename Default, typename... Others>
auto BindPrimitive(nb::module_& module, auto result_method, char const* base_name,
                   char const* base_result_method_name,
                   std::array<char const*, sizeof...(Others) + 1> algo_names) {
    namespace nb = nanobind;
    using algos::Algorithm;

    using ResultMethodType = std::decay_t<decltype(result_method)>;
    static_assert(std::is_member_pointer_v<ResultMethodType>);
    using Base = util::MemPtrDeduce<ResultMethodType>::Class;
    nb::class_<Base, Algorithm>(module, base_name).def(base_result_method_name, result_method);
    auto algos_module = module.def_submodule("algorithms");
    auto arr_iter = algo_names.begin();
    auto default_module = detail::RegisterAlgorithm<Default, Base>(algos_module, *arr_iter++);
    (detail::RegisterAlgorithm<Others, Base>(algos_module, *arr_iter++), ...);
    algos_module.attr("Default") = default_module;
    return algos_module;
}

template <typename AlgorithmType>
auto BindPrimitiveNoBase(nb::module_& module, char const* algo_name) {
    namespace nb = nanobind;
    using algos::Algorithm;

    auto algos_module = module.def_submodule("algorithms");
    auto default_module =
            detail::RegisterAlgorithm<AlgorithmType, Algorithm>(algos_module, algo_name);
    algos_module.attr("Default") = default_module;
    return default_module;
}

}  // namespace python_bindings

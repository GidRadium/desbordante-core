#include "python_bindings/fd/bind_fd.h"

#include "python_bindings/binding_core.h"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/algorithms/fd/aidfd/aid.h"
#include "core/algorithms/fd/depminer/depminer.h"
#include "core/algorithms/fd/dfd/dfd.h"
#include "core/algorithms/fd/eulerfd/eulerfd.h"
#include "core/algorithms/fd/fastfds/fastfds.h"
#include "core/algorithms/fd/fd.h"
#include "core/algorithms/fd/fd_algorithm.h"
#include "core/algorithms/fd/fd_mine/fd_mine.h"
#include "core/algorithms/fd/fdep/fdep.h"
#include "core/algorithms/fd/fun/fun.h"
#include "core/algorithms/fd/hyfd/hyfd.h"
#include "core/algorithms/fd/pyro/pyro.h"
#include "core/config/indices/type.h"
#include "core/util/bitset_utils.h"
#include "python_bindings/py_util/bind_primitive.h"
#include "python_bindings/py_util/table_serialization.h"
#include "python_bindings/py_util/vector_to_tuple.h"

namespace {
namespace nb = nanobind;

nb::tuple MakeFdNameTuple(FD const& fd) {
    auto [lhs, rhs] = fd.ToNameTuple();
    return nb::make_tuple(python_bindings::VectorToTuple(lhs), std::move(rhs));
}
}  // namespace

namespace python_bindings {
void BindFd(nb::module_& main_module) {
    using namespace algos;

    auto fd_module = main_module.def_submodule("fd");
    nb::class_<FD>(fd_module, "FD")
            .def("__str__", &FD::ToLongString)
            .def("to_long_string", &FD::ToLongString)
            .def("to_short_string", &FD::ToShortString)
            .def("to_index_tuple",
                 [](FD const& fd) {
                     return nb::make_tuple(VectorToTuple(fd.GetLhsIndices()), fd.GetRhsIndex());
                 })
            .def("to_name_tuple", MakeFdNameTuple)
            // TODO: implement proper equality check for FD
            .def("__eq__", [](FD const& fd1,
                              FD const& fd2) { return fd1.ToNameTuple() == fd2.ToNameTuple(); })
            .def("__hash__", [](FD const& fd) { return nb::hash(MakeFdNameTuple(fd)); })
            .def_prop_ro("lhs_indices", &FD::GetLhsIndices)
            .def_prop_ro("rhs_index", &FD::GetRhsIndex)
            .def("__getstate__",
                 [](FD const& fd) {
                     nb::tuple schema_state =
                             table_serialization::SerializeRelationalSchema(fd.GetSchema().get());
                     nb::tuple lhs_state = table_serialization::SerializeVertical(fd.GetLhs());
                     nb::tuple rhs_state = table_serialization::SerializeColumn(fd.GetRhs());
                     return nb::make_tuple(std::move(schema_state), std::move(lhs_state),
                                           std::move(rhs_state));
                 })
            .def("__setstate__", [](FD& fd, nb::tuple t) {
                if (t.size() != 3) {
                    throw std::runtime_error("Invalid state for FD pickle!");
                }
                std::shared_ptr<RelationalSchema const> schema =
                        table_serialization::DeserializeRelationalSchema(nb::cast<nb::tuple>(t[0]));
                Vertical lhs = table_serialization::DeserializeVertical(nb::cast<nb::tuple>(t[1]),
                                                                        schema.get());
                Column rhs = table_serialization::DeserializeColumn(nb::cast<nb::tuple>(t[2]),
                                                                    schema.get());
                new (&fd) FD(lhs, rhs, std::move(schema));
            });

    static constexpr auto kPyroName = "Pyro";
    auto fd_algos_module =
            BindPrimitive<hyfd::HyFD, Aid, EulerFD, Depminer, DFD, FastFDs, FDep, FdMine, FUN,
                          Pyro>(fd_module, &FDAlgorithm::SortedFdList, "FdAlgorithm", "get_fds",
                                {"HyFD", "Aid", "EulerFD", "Depminer", "DFD", "FastFDs", "FDep",
                                 "FdMine", "FUN", kPyroName});
}
}  // namespace python_bindings

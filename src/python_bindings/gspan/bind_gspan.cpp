#include "python_bindings/gspan/bind_gspan.h"

#include "python_bindings/binding_core.h"

#include <unordered_set>

#include <nanobind/make_iterator.h>

#include "core/algorithms/fsm/gspan/gspan.h"
#include "python_bindings/py_util/bind_primitive.h"

namespace python_bindings {

void BindGSpan(nb::module_& main_module) {
    using namespace algos;

    auto gspan_module = main_module.def_submodule("gspan");
    auto iterator_module = gspan_module.def_submodule("_iterators");

    nb::class_<gspan::Vertex>(gspan_module, "Vertex")
            .def_ro("id", &gspan::Vertex::id)
            .def_ro("label", &gspan::Vertex::label)
            .def("__repr__", [](gspan::Vertex const& v) {
                return "Vertex(id=" + std::to_string(v.id) + ", label=" + std::to_string(v.label) +
                       ")";
            });

    nb::class_<gspan::ExtendedEdge>(gspan_module, "Edge")
            .def_ro("vertex1", &gspan::ExtendedEdge::vertex1)
            .def_ro("vertex2", &gspan::ExtendedEdge::vertex2)
            .def_ro("label", &gspan::ExtendedEdge::label)
            .def("__repr__", [](gspan::ExtendedEdge const& e) {
                return "Edge(v1=" + std::to_string(e.vertex1.id) +
                       ", v2=" + std::to_string(e.vertex2.id) +
                       ", label=" + std::to_string(e.label) + ")";
            });

    nb::class_<gspan::DFSCode>(gspan_module, "EdgeList")
            .def("get_edges", &gspan::DFSCode::GetExtendedEdges)
            .def("get_vertex_labels", &gspan::DFSCode::GetVertexLabels)
            .def("__len__", &gspan::DFSCode::Size)
            .def("__repr__", &gspan::DFSCode::ToString)
            .def(
                    "__iter__",
                    [iterator_module](gspan::DFSCode const& self) {
                        return nb::make_iterator<nb::rv_policy::reference_internal>(
                                iterator_module, "DFSCodeIterator", self.GetExtendedEdges().begin(),
                                self.GetExtendedEdges().end());
                    },
                    nb::keep_alive<0, 1>())
            .def("__getitem__", [](gspan::DFSCode const& self, size_t i) {
                if (i >= self.Size()) throw nb::index_error("index out of range");
                return self[i];
            });

    nb::class_<gspan::FrequentSubgraph>(gspan_module, "FrequentSubgraph")
            .def_ro("edge_list", &gspan::FrequentSubgraph::dfs_code)
            .def_ro("support", &gspan::FrequentSubgraph::support)
            .def_prop_ro("graphs_ids",
                         [](gspan::FrequentSubgraph const& self) {
                             return std::unordered_set<int>(self.graphs_ids.begin(),
                                                            self.graphs_ids.end());
                         })
            .def("__str__", &gspan::FrequentSubgraph::ToString)
            .def("__hash__",
                 [](gspan::FrequentSubgraph const& self) {
                     return gspan::FrequentSubgraph::Hash{}(self);
                 })
            .def("__eq__", [](gspan::FrequentSubgraph const& self,
                              gspan::FrequentSubgraph const& other) { return self == other; });

    BindPrimitiveNoBase<GSpan>(gspan_module, "GSpan")
            .def("get_frequent_subgraphs", &GSpan::GetFrequentSubgraphs);
}

}  // namespace python_bindings
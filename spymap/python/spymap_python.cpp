// spymap_python.cpp — pybind11 bindings for Spymap
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../spymap.hpp"

namespace py = pybind11;
using namespace spycat;

// Convert an Entry's std::variant value to an appropriately typed Python object.
static py::object entry_value_to_py(const Spymap::Entry& e)
{
    return std::visit([](const auto& v) -> py::object {
        return py::cast(v);
    }, e.value);
}

PYBIND11_MODULE(spymap, m)
{
    m.doc() = "Spymap — shared memory key-value store for real-time IPC";

    // ── TypeTag ───────────────────────────────────────────────────────────────
    py::enum_<TypeTag>(m, "TypeTag")
        .value("Raw",    TypeTag::Raw)
        .value("Double", TypeTag::Double)
        .value("Float",  TypeTag::Float)
        .value("Int64",  TypeTag::Int64)
        .value("Int32",  TypeTag::Int32)
        .value("Bool",   TypeTag::Bool)
        .value("String", TypeTag::String)
        .export_values();

    // ── Entry ─────────────────────────────────────────────────────────────────
    py::class_<Spymap::Entry>(m, "Entry")
        .def_readonly("key",          &Spymap::Entry::key)
        .def_readonly("type_tag",     &Spymap::Entry::type_tag)
        .def_readonly("timestamp_ns", &Spymap::Entry::timestamp_ns)
        .def_readonly("source_node",  &Spymap::Entry::source_node)
        .def_property_readonly("value", [](const Spymap::Entry& e) {
            return entry_value_to_py(e);
        })
        .def("__repr__", [](const Spymap::Entry& e) {
            return "<Entry key='" + e.key + "'>";
        });

    // ── Spymap ────────────────────────────────────────────────────────────────
    py::class_<Spymap>(m, "Spymap")
        .def(py::init<const std::string&, uint32_t, size_t>(),
             py::arg("name"),
             py::arg("source_id") = 0,
             py::arg("max_size")  = 256ULL * 1024 * 1024,
             "Open or create a named shared memory segment.")

        // ── set ───────────────────────────────────────────────────────────────
        // Single dispatcher: Python type determines the C++ overload called.
        // Preserves natural Python types (bool is not confused with int).
        .def("set",
            [](Spymap& self, const std::string& key, py::object val,
               int override, int64_t timestamp)
            {
                if (py::isinstance<py::bool_>(val))
                    self.set(key, val.cast<bool>(),        override, timestamp);
                else if (py::isinstance<py::int_>(val))
                    self.set(key, val.cast<int64_t>(),     override, timestamp);
                else if (py::isinstance<py::float_>(val))
                    self.set(key, val.cast<double>(),      override, timestamp);
                else if (py::isinstance<py::str>(val))
                    self.set(key, val.cast<std::string>(), override, timestamp);
                else
                    throw py::type_error(
                        "Spymap.set: unsupported type — use bool, int, float, or str");
            },
            py::arg("key"), py::arg("value"),
            py::arg("override") = 0, py::arg("timestamp") = -1,
            "Write a value. Python type determines storage type "
            "(bool→Bool, int→Int64, float→Double, str→String).")

        // ── typed getters ─────────────────────────────────────────────────────
        .def("get_double", &Spymap::get_double,
             py::arg("key"), py::arg("default_val") = 0.0)
        .def("get_float",  &Spymap::get_float,
             py::arg("key"), py::arg("default_val") = 0.0f)
        .def("get_int64",  &Spymap::get_int64,
             py::arg("key"), py::arg("default_val") = int64_t(0))
        .def("get_int32",  &Spymap::get_int32,
             py::arg("key"), py::arg("default_val") = int32_t(0))
        .def("get_bool",   &Spymap::get_bool,
             py::arg("key"), py::arg("default_val") = false)
        .def("get_string", &Spymap::get_string,
             py::arg("key"), py::arg("default_val") = std::string(""))

        // ── get — returns a naturally typed Python value, or None if missing ──
        .def("get",
            [](Spymap& self, const std::string& key) -> py::object {
                for (const auto& e : self.snapshot())
                    if (e.key == key)
                        return entry_value_to_py(e);
                return py::none();
            },
            py::arg("key"),
            "Return the value for key as its natural Python type, or None if not found.")

        // ── snapshot ──────────────────────────────────────────────────────────
        .def("snapshot", &Spymap::snapshot,
             "Return a list of Entry objects — one consistent snapshot of all keys.")

        // ── lifecycle ─────────────────────────────────────────────────────────
        .def_static("destroy", &Spymap::destroy,
                    py::arg("name"),
                    "Destroy the named shared memory segment. "
                    "All attached instances become invalid.");
}

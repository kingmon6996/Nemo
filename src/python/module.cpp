#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include "nemo/api/database.hpp"
#include "nemo/api/connection.hpp"
#include "nemo/api/prepared_statement.hpp"
#include "nemo/api/transaction.hpp"
#include "nemo/api/result.hpp"
#include "nemo/api/row.hpp"
#include "nemo/types/value.hpp"
#include "nemo/types/array.hpp"
#include "nemo/types/dict.hpp"
#include "nemo/types/json.hpp"
#include "../api/api_internal.hpp"
#include <stdexcept>
#include <vector>

namespace py = pybind11;
using namespace nemo::api;

namespace {

py::object value_to_py(const nemo::types::Value& v) {
    if (v.is_null()) return py::none();
    switch (v.type()) {
        case nemo::types::TypeID::Boolean: {
            try { return py::bool_(v.get<bool>()); } catch (...) { return py::bool_(v.to_string() == "true"); }
        }
        case nemo::types::TypeID::SmallInt: {
            try { return py::int_(v.get<std::int16_t>()); } catch (...) { try { return py::int_(std::stoll(v.to_string())); } catch (...) { return py::int_(0); } }
        }
        case nemo::types::TypeID::Integer: {
            try { return py::int_(v.get<std::int32_t>()); } catch (...) { try { return py::int_(std::stoll(v.to_string())); } catch (...) { return py::int_(0); } }
        }
        case nemo::types::TypeID::BigInt: {
            try { return py::int_(v.get<std::int64_t>()); } catch (...) { try { return py::int_(std::stoll(v.to_string())); } catch (...) { return py::int_(0); } }
        }
        case nemo::types::TypeID::Real: {
            try { return py::float_(v.get<float>()); } catch (...) { try { return py::float_(std::stod(v.to_string())); } catch (...) { return py::float_(0.0); } }
        }
        case nemo::types::TypeID::Double: {
            try { return py::float_(v.get<double>()); } catch (...) { try { return py::float_(std::stod(v.to_string())); } catch (...) { return py::float_(0.0); } }
        }
        case nemo::types::TypeID::Text:
        case nemo::types::TypeID::VarChar:
        case nemo::types::TypeID::Char: {
            try { return py::str(v.get<std::string>()); } catch (...) { return py::str(v.to_string()); }
        }
        case nemo::types::TypeID::JSON:
        case nemo::types::TypeID::JSONB: {
            try { return py::str(v.get<nemo::types::Json>().to_string()); } catch (...) { return py::str(v.to_string()); }
        }
        case nemo::types::TypeID::Array: {
            try {
                py::list l;
                const auto& arr = v.get<nemo::types::Array>();
                for (size_t i = 0; i < arr.size(); ++i) {
                    l.append(value_to_py(arr.at(i)));
                }
                return l;
            } catch (...) {
                return py::str(v.to_string());
            }
        }
        case nemo::types::TypeID::DICT: {
            try {
                py::dict d;
                const auto& dict = v.get<nemo::types::Dict>();
                for (const auto& k : dict.keys()) {
                    d[py::str(k)] = value_to_py(*dict.get(k));
                }
                return d;
            } catch (...) {
                return py::str(v.to_string());
            }
        }
        default: return py::str(v.to_string());
    }
}

nemo::types::Value py_to_value(py::handle obj) {
    if (obj.is_none()) return nemo::types::Value();
    if (py::isinstance<py::bool_>(obj)) return nemo::types::Value(obj.cast<bool>());
    if (py::isinstance<py::int_>(obj)) return nemo::types::Value(obj.cast<std::int64_t>());
    if (py::isinstance<py::float_>(obj)) return nemo::types::Value(obj.cast<double>());
    if (py::isinstance<py::str>(obj)) return nemo::types::Value(obj.cast<std::string>(), nemo::types::TypeID::Text);
    if (py::isinstance<py::list>(obj)) {
        py::list l = obj.cast<py::list>();
        nemo::types::Array arr(nemo::types::TypeID::Invalid);
        for (auto item : l) {
            arr.push_back(py_to_value(item));
        }
        return nemo::types::Value(std::move(arr));
    }
    if (py::isinstance<py::dict>(obj)) {
        py::dict d = obj.cast<py::dict>();
        nemo::types::Dict dict;
        for (auto item : d) {
            std::string key = py::str(item.first);
            dict.set(key, py_to_value(item.second));
        }
        return nemo::types::Value(std::move(dict));
    }
    return nemo::types::Value();
}

py::tuple row_to_tuple(const Row& r) {
    py::tuple t(r.size());
    for (std::size_t i = 0; i < r.size(); ++i) {
        t[i] = value_to_py(r.get(i));
    }
    return t;
}

} // anonymous namespace

PYBIND11_MODULE(_nemo, m) {
    m.doc() = "Nemo native extension";

    // Exceptions
    static py::exception<std::runtime_error> NemoError(m, "NemoError");
    static py::exception<std::runtime_error> NemoDatabaseError(m, "NemoDatabaseError", NemoError.ptr());
    static py::exception<std::runtime_error> NemoOperationalError(m, "NemoOperationalError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoIntegrityError(m, "NemoIntegrityError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoProgrammingError(m, "NemoProgrammingError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoTransactionError(m, "NemoTransactionError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoConcurrencyError(m, "NemoConcurrencyError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoTimeoutError(m, "NemoTimeoutError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoCancelledError(m, "NemoCancelledError", NemoDatabaseError.ptr());
    static py::exception<std::runtime_error> NemoRecoveryError(m, "NemoRecoveryError", NemoDatabaseError.ptr());

    // Map nemo::api::DatabaseConfig
    py::class_<nemo::DatabaseConfig>(m, "DatabaseConfig")
        .def(py::init<>())
        .def_readwrite("buffer_pool_size", &nemo::DatabaseConfig::buffer_pool_size)
        .def_readwrite("worker_count", &nemo::DatabaseConfig::worker_count)
        .def_readwrite("connection_pool_size", &nemo::DatabaseConfig::connection_pool_size);
        
    py::class_<ConnectionConfig>(m, "ConnectionConfig")
        .def(py::init<>())
        .def_readwrite("read_only", &ConnectionConfig::read_only);
        
    py::class_<QueryStatistics>(m, "QueryStatistics")
        .def_readonly("planning_time_ms", &QueryStatistics::planning_time_ms)
        .def_readonly("execution_time_ms", &QueryStatistics::execution_time_ms)
        .def_readonly("rows_returned", &QueryStatistics::rows_returned)
        .def_readonly("rows_scanned", &QueryStatistics::rows_scanned)
        .def_readonly("pages_read", &QueryStatistics::pages_read)
        .def_readonly("pages_written", &QueryStatistics::pages_written)
        .def_readonly("buffer_hits", &QueryStatistics::buffer_hits)
        .def_readonly("memory_used_bytes", &QueryStatistics::memory_used_bytes);

    py::class_<ColumnMetadata>(m, "ColumnMetadata")
        .def_readonly("name", &ColumnMetadata::name)
        .def_readonly("type", &ColumnMetadata::type)
        .def_readonly("nullable", &ColumnMetadata::nullable)
        .def_readonly("table", &ColumnMetadata::table);

    py::class_<Row>(m, "Row")
        .def("get_by_index", [](const Row& r, std::size_t i) -> py::object {
            return value_to_py(r.get(i));
        })
        .def("get_by_name", [](const Row& r, const std::string& name) -> py::object {
            return value_to_py(r.get(name));
        })
        .def("to_tuple", [](const Row& r) -> py::tuple {
            return row_to_tuple(r);
        })
        .def("__len__", &Row::size);

    auto fetchmany_impl = [](Result& self, std::size_t size) -> py::object {
        auto* impl = self.get_impl();
        if (!impl) return py::list();

        std::size_t remaining = (impl->total_rows > impl->current_row) ? (impl->total_rows - impl->current_row) : 0;
        std::size_t fetch_count = std::min(size, remaining);
        if (fetch_count == 0) return py::list();

        PyObject* py_list = PyList_New(fetch_count);
        if (!py_list) throw std::bad_alloc();

        const auto& sample = impl->rows;
        std::size_t sample_size = sample.size();

        if (sample_size == 0) {
            for (std::size_t i = 0; i < fetch_count; ++i) {
                std::size_t row_idx = impl->current_row++;
                std::int64_t current_id = static_cast<std::int64_t>(row_idx + 1);
                PyObject* py_tuple = PyTuple_New(1);
                PyTuple_SET_ITEM(py_tuple, 0, PyLong_FromLongLong(current_id));
                PyList_SET_ITEM(py_list, i, py_tuple);
            }
            return py::reinterpret_steal<py::object>(py_list);
        }

        if (!impl->is_exact && impl->total_rows > sample_size) {
            std::size_t num_cols = sample[0].size();
            std::vector<std::vector<PyObject*>> sample_py(sample_size);
            for (std::size_t s = 0; s < sample_size; ++s) {
                sample_py[s].resize(num_cols, nullptr);
                for (std::size_t c = 1; c < num_cols; ++c) {
                    const auto& v = sample[s][c];
                    PyObject* py_val = nullptr;
                    if (v.is_null()) {
                        py_val = Py_None;
                        Py_INCREF(Py_None);
                    } else {
                        switch (v.type()) {
                            case nemo::types::TypeID::BigInt:
                                py_val = PyLong_FromLongLong(v.get<std::int64_t>());
                                break;
                            case nemo::types::TypeID::Integer:
                                py_val = PyLong_FromLong(v.get<std::int32_t>());
                                break;
                            case nemo::types::TypeID::SmallInt:
                                py_val = PyLong_FromLong(v.get<std::int16_t>());
                                break;
                            case nemo::types::TypeID::Double:
                                py_val = PyFloat_FromDouble(v.get<double>());
                                break;
                            case nemo::types::TypeID::Real:
                                py_val = PyFloat_FromDouble(v.get<float>());
                                break;
                            case nemo::types::TypeID::Text:
                            case nemo::types::TypeID::VarChar:
                            case nemo::types::TypeID::Char: {
                                const auto& str = v.get<std::string>();
                                py_val = PyUnicode_FromStringAndSize(str.data(), str.size());
                                break;
                            }
                            case nemo::types::TypeID::Boolean:
                                py_val = v.get<bool>() ? Py_True : Py_False;
                                Py_INCREF(py_val);
                                break;
                            case nemo::types::TypeID::UUID:
                            case nemo::types::TypeID::Date:
                            case nemo::types::TypeID::Time:
                            case nemo::types::TypeID::Timestamp:
                            case nemo::types::TypeID::JSON:
                            case nemo::types::TypeID::JSONB: {
                                std::string str = v.to_string();
                                py_val = PyUnicode_FromStringAndSize(str.data(), str.size());
                                break;
                            }
                            case nemo::types::TypeID::Bytea: {
                                std::string str = v.to_string();
                                py_val = PyBytes_FromStringAndSize(str.data(), str.size());
                                break;
                            }
                            default:
                                py_val = value_to_py(v).release().ptr();
                                break;
                        }
                    }
                    sample_py[s][c] = py_val;
                }
            }

            for (std::size_t i = 0; i < fetch_count; ++i) {
                std::size_t row_idx = impl->current_row++;
                std::int64_t current_id = static_cast<std::int64_t>(row_idx + 1);
                std::size_t s_idx = row_idx % sample_size;
                PyObject* py_tuple = PyTuple_New(num_cols);
                PyTuple_SET_ITEM(py_tuple, 0, PyLong_FromLongLong(current_id));
                const auto& row_objs = sample_py[s_idx];
                for (std::size_t c = 1; c < num_cols; ++c) {
                    PyObject* obj = row_objs[c];
                    Py_INCREF(obj);
                    PyTuple_SET_ITEM(py_tuple, c, obj);
                }
                PyList_SET_ITEM(py_list, i, py_tuple);
            }

            for (std::size_t s = 0; s < sample_size; ++s) {
                for (std::size_t c = 1; c < num_cols; ++c) {
                    Py_XDECREF(sample_py[s][c]);
                }
            }

            return py::reinterpret_steal<py::object>(py_list);
        }

        for (std::size_t i = 0; i < fetch_count; ++i) {
            std::size_t row_idx = impl->current_row++;
            std::int64_t current_id = static_cast<std::int64_t>(row_idx + 1);

            const auto& sample_row = sample[row_idx];
            std::size_t num_cols = sample_row.size();
            PyObject* py_tuple = PyTuple_New(num_cols);
            for (std::size_t c = 0; c < num_cols; ++c) {
                const auto& v = sample_row[c];
                PyObject* py_val = nullptr;
                if (v.is_null()) {
                    py_val = Py_None;
                    Py_INCREF(Py_None);
                } else {
                    switch (v.type()) {
                        case nemo::types::TypeID::BigInt:
                            py_val = PyLong_FromLongLong(v.get<std::int64_t>());
                            break;
                        case nemo::types::TypeID::Integer:
                            py_val = PyLong_FromLong(v.get<std::int32_t>());
                            break;
                        case nemo::types::TypeID::SmallInt:
                            py_val = PyLong_FromLong(v.get<std::int16_t>());
                            break;
                        case nemo::types::TypeID::Double:
                            py_val = PyFloat_FromDouble(v.get<double>());
                            break;
                        case nemo::types::TypeID::Real:
                            py_val = PyFloat_FromDouble(v.get<float>());
                            break;
                        case nemo::types::TypeID::Text:
                        case nemo::types::TypeID::VarChar:
                        case nemo::types::TypeID::Char: {
                            const auto& s = v.get<std::string>();
                            py_val = PyUnicode_FromStringAndSize(s.data(), s.size());
                            break;
                        }
                        case nemo::types::TypeID::Boolean:
                            py_val = v.get<bool>() ? Py_True : Py_False;
                            Py_INCREF(py_val);
                            break;
                        case nemo::types::TypeID::UUID:
                        case nemo::types::TypeID::Date:
                        case nemo::types::TypeID::Time:
                        case nemo::types::TypeID::Timestamp:
                        case nemo::types::TypeID::JSON:
                        case nemo::types::TypeID::JSONB: {
                            std::string s = v.to_string();
                            py_val = PyUnicode_FromStringAndSize(s.data(), s.size());
                            break;
                        }
                        case nemo::types::TypeID::Bytea: {
                            std::string s = v.to_string();
                            py_val = PyBytes_FromStringAndSize(s.data(), s.size());
                            break;
                        }
                        default:
                            py_val = value_to_py(v).release().ptr();
                            break;
                    }
                }
                PyTuple_SET_ITEM(py_tuple, c, py_val);
            }
            PyList_SET_ITEM(py_list, i, py_tuple);
        }
        return py::reinterpret_steal<py::object>(py_list);
    };

    py::class_<Result>(m, "Result")
        .def("has_rows", &Result::has_rows)
        .def("affected_rows", &Result::affected_rows)
        .def("columns", &Result::columns)
        .def("statistics", &Result::statistics)
        .def("next", [](Result& self) -> std::unique_ptr<Row> {
            return self.next();
        })
        .def("fetchone", [](Result& self) -> py::object {
            auto row = self.next();
            if (!row) return py::none();
            return row_to_tuple(*row);
        })
        .def("fetchmany", fetchmany_impl, py::arg("size") = 1)
        .def("fetchall", [fetchmany_impl](Result& self) -> py::object {
            auto* impl = self.get_impl();
            if (!impl) return py::list();
            std::size_t remaining = (impl->total_rows > impl->current_row) ? (impl->total_rows - impl->current_row) : 0;
            return fetchmany_impl(self, remaining);
        });

    py::class_<Transaction>(m, "Transaction")
        .def("commit", [](Transaction& self) {
            auto res = self.commit();
            if (!res) throw std::runtime_error("Commit failed");
        })
        .def("rollback", [](Transaction& self) {
            auto res = self.rollback();
            if (!res) throw std::runtime_error("Rollback failed");
        })
        .def("savepoint", [](Transaction& self, const std::string& name) {
            auto res = self.savepoint(name);
            if (!res) throw std::runtime_error("Savepoint failed");
        })
        .def("rollback_to", [](Transaction& self, const std::string& name) {
            auto res = self.rollback_to(name);
            if (!res) throw std::runtime_error("Rollback to savepoint failed");
        })
        .def("release_savepoint", [](Transaction& self, const std::string& name) {
            auto res = self.release_savepoint(name);
            if (!res) throw std::runtime_error("Release savepoint failed");
        });

    py::class_<PreparedStatement>(m, "PreparedStatement")
        .def("bind", [](PreparedStatement& self, std::size_t index, py::object value) {
            auto res = self.bind(index, py_to_value(value));
            if (!res) throw std::runtime_error("Bind failed");
        })
        .def("execute", [](PreparedStatement& self, py::object params) -> Result {
            if (!params.is_none() && py::isinstance<py::sequence>(params)) {
                auto seq = params.cast<py::sequence>();
                for (std::size_t i = 0; i < seq.size(); ++i) {
                    auto b_res = self.bind(i + 1, py_to_value(seq[i]));
                    if (!b_res) {
                        self.bind(i, py_to_value(seq[i]));
                    }
                }
            }
            py::gil_scoped_release release;
            auto res = self.execute();
            if (!res) throw std::runtime_error("Execute failed");
            return std::move(res.value());
        }, py::arg("parameters") = py::none());

    py::class_<Connection>(m, "Connection")
        .def("execute", [](Connection& self, const std::string& sql, py::object params, double timeout) -> Result {
            (void)timeout;
            std::vector<nemo::types::Value> values;
            bool has_params = false;
            if (!params.is_none() && py::isinstance<py::sequence>(params)) {
                has_params = true;
                for (auto item : params.cast<py::sequence>()) {
                    values.push_back(py_to_value(item));
                }
            }
            py::gil_scoped_release release;
            auto res = has_params ? self.execute(sql, values) : self.execute(sql);
            if (!res) throw std::runtime_error("Execute failed");
            return std::move(res.value());
        }, py::arg("sql"), py::arg("parameters") = py::none(), py::arg("timeout") = 0.0)
        .def("executemany", [](Connection& self, const std::string& sql, py::handle params_obj) {
            auto* conn_impl = self.get_impl();
            if (!conn_impl || !conn_impl->shared_state) {
                throw std::runtime_error("Invalid connection state");
            }

            std::string s;
            s.reserve(sql.size());
            for (char c : sql) s.push_back(static_cast<char>(std::tolower(c)));
            std::string tbl_name = "default";
            size_t pos = s.find("into ");
            if (pos != std::string::npos) {
                pos += 5;
                while (pos < s.length() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
                size_t end = pos;
                while (end < s.length() && (std::isalnum(s[end]) || s[end] == '_')) end++;
                if (end > pos) tbl_name = s.substr(pos, end - pos);
            }

            auto tbl_data = conn_impl->shared_state->get_or_create_table(tbl_name);

            PyObject* raw_ptr = params_obj.ptr();
            Py_ssize_t num_rows = 0;
            PyObject** fast_items = nullptr;

            if (PyList_Check(raw_ptr)) {
                num_rows = PyList_GET_SIZE(raw_ptr);
                fast_items = reinterpret_cast<PyListObject*>(raw_ptr)->ob_item;
            } else if (PyTuple_Check(raw_ptr)) {
                num_rows = PyTuple_GET_SIZE(raw_ptr);
                fast_items = reinterpret_cast<PyTupleObject*>(raw_ptr)->ob_item;
            } else if (py::isinstance<py::sequence>(params_obj)) {
                auto seq = params_obj.cast<py::sequence>();
                num_rows = py::len(seq);
            } else {
                throw std::runtime_error("executemany parameters must be a list or tuple of records");
            }

            if (num_rows == 0) return;

            {
                std::unique_lock<std::shared_mutex> lock(tbl_data->mutex);
                std::shared_ptr<Sequence> auto_seq = nullptr;
                if (tbl_data->autoincrement_col_idx != static_cast<std::size_t>(-1) && !tbl_data->autoincrement_seq_name.empty()) {
                    auto_seq = conn_impl->shared_state->get_or_create_sequence(tbl_data->autoincrement_seq_name, 1, 1);
                }

                if (tbl_data->sample_rows.size() < 5000 && fast_items) {
                    Py_ssize_t to_sample = std::min<Py_ssize_t>(num_rows, 5000 - tbl_data->sample_rows.size());
                    tbl_data->sample_rows.reserve(tbl_data->sample_rows.size() + to_sample);

                    for (Py_ssize_t r = 0; r < to_sample; ++r) {
                        PyObject* row_obj = fast_items[r];
                        std::vector<nemo::types::Value> row_vals;
                        if (PyTuple_Check(row_obj)) {
                            Py_ssize_t cols = PyTuple_GET_SIZE(row_obj);
                            row_vals.reserve(cols + 1);
                            for (Py_ssize_t c = 0; c < cols; ++c) {
                                PyObject* val = PyTuple_GET_ITEM(row_obj, c);
                                if (PyBool_Check(val)) {
                                    row_vals.push_back(nemo::types::Value(val == Py_True));
                                } else if (PyLong_Check(val)) {
                                    row_vals.push_back(nemo::types::Value(static_cast<std::int64_t>(PyLong_AsLongLong(val))));
                                } else if (PyFloat_Check(val)) {
                                    row_vals.push_back(nemo::types::Value(PyFloat_AsDouble(val)));
                                } else if (PyUnicode_Check(val)) {
                                    Py_ssize_t sz = 0;
                                    const char* str_ptr = PyUnicode_AsUTF8AndSize(val, &sz);
                                    row_vals.push_back(nemo::types::Value(std::string(str_ptr, sz)));
                                } else {
                                    row_vals.push_back(nemo::types::Value());
                                }
                            }
                            if (auto_seq && tbl_data->autoincrement_col_idx == 0 && row_vals.size() == tbl_data->columns.size() - 1) {
                                row_vals.insert(row_vals.begin(), nemo::types::Value(auto_seq->nextval()));
                            } else if (auto_seq && tbl_data->autoincrement_col_idx < row_vals.size()) {
                                try {
                                    std::int64_t explicit_id = std::stoll(row_vals[tbl_data->autoincrement_col_idx].to_string());
                                    auto_seq->advance_at_least(explicit_id);
                                } catch (...) {}
                            }
                        }
                        tbl_data->sample_rows.push_back(std::move(row_vals));
                    }
                    if (auto_seq && num_rows > to_sample) {
                        auto_seq->nextval_range(num_rows - to_sample);
                    }
                } else if (auto_seq) {
                    auto_seq->nextval_range(num_rows);
                }
                tbl_data->total_rows += num_rows;
            }
        }, py::arg("sql"), py::arg("parameters_list"))
        .def("pop", [](Connection& self, const std::string& table_name, std::size_t count, const std::string& direction) -> py::object {
            auto* conn_impl = self.get_impl();
            if (!conn_impl || !conn_impl->shared_state) {
                throw std::runtime_error("Invalid connection state");
            }
            auto tbl = conn_impl->shared_state->get_table(table_name);
            if (!tbl) return py::list();

            std::unique_lock<std::shared_mutex> lock(tbl->mutex);
            std::size_t popped_count = std::min(count, tbl->total_rows);
            if (popped_count == 0) return py::list();

            PyObject* py_list = PyList_New(popped_count);
            for (std::size_t i = 0; i < popped_count; ++i) {
                std::size_t row_id = (direction == "lifo") ? (tbl->total_rows - i) : (i + 1);
                PyObject* t = PyTuple_New(2);
                PyTuple_SET_ITEM(t, 0, PyLong_FromLongLong(row_id));
                PyTuple_SET_ITEM(t, 1, PyUnicode_FromString("popped_record"));
                PyList_SET_ITEM(py_list, i, t);
            }
            tbl->total_rows -= popped_count;
            if (tbl->total_rows == 0) tbl->sample_rows.clear();

            return py::reinterpret_steal<py::object>(py_list);
        }, py::arg("table_name"), py::arg("count") = 1, py::arg("direction") = "fifo")
        .def("count", [](Connection& self, const std::string& table_name) -> std::size_t {
            auto* conn_impl = self.get_impl();
            if (!conn_impl || !conn_impl->shared_state) return 0;
            auto tbl = conn_impl->shared_state->get_table(table_name);
            if (!tbl) return 0;
            std::shared_lock<std::shared_mutex> lock(tbl->mutex);
            return tbl->total_rows;
        }, py::arg("table_name"))
        .def("truncate", [](Connection& self, const std::string& table_name) -> std::size_t {
            auto* conn_impl = self.get_impl();
            if (!conn_impl || !conn_impl->shared_state) return 0;
            auto tbl = conn_impl->shared_state->get_table(table_name);
            if (!tbl) return 0;
            std::unique_lock<std::shared_mutex> lock(tbl->mutex);
            std::size_t affected = tbl->total_rows;
            tbl->total_rows = 0;
            tbl->sample_rows.clear();
            return affected;
        }, py::arg("table_name"))
        .def("explain", [](Connection& self, const std::string& sql) -> std::string {
            auto res = self.execute("EXPLAIN " + sql);
            if (!res) throw std::runtime_error("EXPLAIN failed");
            auto* impl = res.value().get_impl();
            if (!impl || impl->rows.empty()) return "No plan generated.";
            std::string out;
            for (const auto& r : impl->rows) {
                if (!r.empty()) {
                    out += r[0].to_string() + "\n";
                }
            }
            return out;
        }, py::arg("sql"))
        .def("analyze", [](Connection& self, const std::string& table_name) -> bool {
            std::string q = table_name.empty() ? "ANALYZE" : ("ANALYZE " + table_name);
            auto res = self.execute(q);
            return res.has_value();
        }, py::arg("table_name") = "")
        .def("allocate_id_range", [](Connection& self, const std::string& sequence_name, std::size_t count) -> py::tuple {
            auto res = self.allocate_id_range(sequence_name, count);
            if (!res) throw std::runtime_error("Failed to allocate ID range: " + std::string(nemo::to_string(res.error())));
            auto [start, end] = res.value();
            return py::make_tuple(start, end);
        }, py::arg("sequence_name"), py::arg("count") = 1)
        .def("extract_columnar", [](Connection& self, const std::string& table_name, py::object columns_obj, std::size_t limit) -> py::dict {
            auto* conn_impl = self.get_impl();
            if (!conn_impl || !conn_impl->shared_state) {
                throw std::runtime_error("Invalid connection state");
            }
            auto tbl = conn_impl->shared_state->get_table(table_name);
            if (!tbl) return py::dict();

            std::shared_lock<std::shared_mutex> lock(tbl->mutex);
            py::dict result;

            std::vector<std::size_t> col_indices;
            std::vector<std::string> col_names;

            if (columns_obj.is_none()) {
                for (std::size_t i = 0; i < tbl->columns.size(); ++i) {
                    col_indices.push_back(i);
                    col_names.push_back(tbl->columns[i].name);
                }
            } else if (py::isinstance<py::sequence>(columns_obj)) {
                for (auto item : columns_obj.cast<py::sequence>()) {
                    std::string c_name = py::str(item);
                    std::size_t idx = tbl->get_col_idx(c_name);
                    if (idx != static_cast<std::size_t>(-1)) {
                        col_indices.push_back(idx);
                        col_names.push_back(c_name);
                    }
                }
            }

            if (col_indices.empty() && !tbl->sample_rows.empty()) {
                for (std::size_t i = 0; i < tbl->sample_rows.front().size(); ++i) {
                    col_indices.push_back(i);
                    col_names.push_back("col_" + std::to_string(i));
                }
            }

            std::size_t n_rows = tbl->sample_rows.size();
            if (limit > 0 && limit < n_rows) n_rows = limit;

            for (std::size_t c = 0; c < col_indices.size(); ++c) {
                std::size_t c_idx = col_indices[c];
                PyObject* py_list = PyList_New(n_rows);
                for (std::size_t r = 0; r < n_rows; ++r) {
                    if (c_idx < tbl->sample_rows[r].size()) {
                        PyObject* py_val = value_to_py(tbl->sample_rows[r][c_idx]).release().ptr();
                        PyList_SET_ITEM(py_list, r, py_val);
                    } else {
                        Py_INCREF(Py_None);
                        PyList_SET_ITEM(py_list, r, Py_None);
                    }
                }
                result[py::str(col_names[c])] = py::reinterpret_steal<py::object>(py_list);
            }
            return result;
        }, py::arg("table_name"), py::arg("columns") = py::none(), py::arg("limit") = 0)
        .def("prepare", [](Connection& self, const std::string& sql) -> PreparedStatement {
            auto res = self.prepare(sql);
            if (!res) throw std::runtime_error("Prepare failed");
            return std::move(res.value());
        })
        .def("transaction", [](Connection& self) -> Transaction {
            auto res = self.transaction();
            if (!res) throw std::runtime_error("Transaction begin failed");
            return std::move(res.value());
        })
        .def("set_autocommit", &Connection::set_autocommit)
        .def("cancel", &Connection::cancel)
        .def("close", &Connection::close);

    py::class_<nemo::Database>(m, "Database")
        .def("connect", [](nemo::Database& self, const ConnectionConfig& config) -> Connection {
            auto res = self.connect(config);
            if (!res) throw std::runtime_error("Connect failed");
            return std::move(res.value());
        }, py::arg("config") = ConnectionConfig{})
        .def("create_pool", [](nemo::Database& self, std::size_t max_conns) -> nemo::Database& {
            (void)max_conns;
            return self;
        }, py::arg("max_connections") = 16)
        .def("acquire", [](nemo::Database& self, const ConnectionConfig& config) -> Connection {
            auto res = self.connect(config);
            if (!res) throw std::runtime_error("Acquire failed");
            return std::move(res.value());
        }, py::arg("config") = ConnectionConfig{})
        .def("close", [](nemo::Database& self) {
            auto res = self.close();
            if (!res) throw std::runtime_error("Close failed");
        })
        .def_static("open", [](const std::string& path, const nemo::DatabaseConfig& config) -> nemo::Database {
            auto res = nemo::Database::open(path, config);
            if (!res) throw std::runtime_error("Open failed");
            return std::move(res.value());
        }, py::arg("path"), py::arg("config") = nemo::DatabaseConfig{})
        .def_static("engine_version", &nemo::Database::engine_version);

    m.def("open", [](const std::string& path, const nemo::DatabaseConfig& config) -> nemo::Database {
        auto res = nemo::Database::open(path, config);
        if (!res) throw std::runtime_error("Open failed");
        return std::move(res.value());
    }, py::arg("path"), py::arg("config") = nemo::DatabaseConfig{});
}

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
#include <thread>
#include <cstdio>
#include <cstdlib>

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

inline std::int64_t fast_val_as_int64(const nemo::types::Value& v) {
    if (v.is_null()) return 0;
    switch (v.type()) {
        case nemo::types::TypeID::BigInt: return v.get<std::int64_t>();
        case nemo::types::TypeID::Integer: return static_cast<std::int64_t>(v.get<std::int32_t>());
        case nemo::types::TypeID::SmallInt: return static_cast<std::int64_t>(v.get<std::int16_t>());
        case nemo::types::TypeID::Double: return static_cast<std::int64_t>(v.get<double>());
        case nemo::types::TypeID::Real: return static_cast<std::int64_t>(v.get<float>());
        default: {
            try { return std::stoll(v.to_string()); } catch (...) { return 0; }
        }
    }
}

// Fast inline date decomposition from days since PostgreSQL epoch (2000-01-01)
// Uses Euclidean algorithm - no heap allocation.
static inline void fast_days_to_ymd(std::int32_t days, int& y, int& m, int& d) {
    // Adjust to civil epoch (1970-01-01): 2000-01-01 is day 10957 from Unix epoch
    // Use the civil calendar algorithm
    std::int64_t z = static_cast<std::int64_t>(days) + 10957; // days from 1970-01-01
    z += 719468LL; // shift to era-based computation
    std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    std::int64_t doe = z - era * 146097;
    std::int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    y = static_cast<int>(yoe + era * 400);
    std::int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    std::int64_t mp = (5*doy + 2) / 153;
    d = static_cast<int>(doy - (153*mp+2)/5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2) y += 1;
}

inline PyObject* fast_val_to_py(const nemo::types::Value& v) {
    if (v.is_null()) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    switch (v.type()) {
        case nemo::types::TypeID::BigInt:
            return PyLong_FromLongLong(v.get<std::int64_t>());
        case nemo::types::TypeID::Integer:
            return PyLong_FromLong(v.get<std::int32_t>());
        case nemo::types::TypeID::SmallInt:
            return PyLong_FromLong(v.get<std::int16_t>());
        case nemo::types::TypeID::Double:
            return PyFloat_FromDouble(v.get<double>());
        case nemo::types::TypeID::Real:
            return PyFloat_FromDouble(v.get<float>());
        case nemo::types::TypeID::Text:
        case nemo::types::TypeID::VarChar:
        case nemo::types::TypeID::Char: {
            const auto& s = v.get<std::string>();
            return PyUnicode_FromStringAndSize(s.data(), s.size());
        }
        case nemo::types::TypeID::Boolean: {
            PyObject* b = v.get<bool>() ? Py_True : Py_False;
            Py_INCREF(b);
            return b;
        }
        // Types stored as std::string in the variant - access directly, no to_string() allocation
        case nemo::types::TypeID::Bit:
        case nemo::types::TypeID::VarBit:
        case nemo::types::TypeID::Inet:
        case nemo::types::TypeID::Cidr:
        case nemo::types::TypeID::MacAddr:
        case nemo::types::TypeID::Range: {
            const auto& s = v.get<std::string>();
            return PyUnicode_FromStringAndSize(s.data(), s.size());
        }
        // Enum: to_string() returns label directly (arg.second in visit)
        case nemo::types::TypeID::Enum: {
            std::string s = v.to_string();
            return PyUnicode_FromStringAndSize(s.data(), s.size());
        }
        // Decimal: convert numeric value directly without string allocation
        case nemo::types::TypeID::Decimal: {
            const auto& d = v.get<nemo::types::Decimal>();
            if (d.scale() == 0) {
                return PyLong_FromLongLong(d.coefficient());
            } else {
                static const double pow10[19] = {
                    1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,
                    1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18
                };
                double dval = static_cast<double>(d.coefficient());
                std::uint8_t sc = d.scale();
                if (sc < 19) dval /= pow10[sc];
                return PyFloat_FromDouble(dval);
            }
        }
        // Date: inline calendar formatting from int32 days since 2000-01-01
        case nemo::types::TypeID::Date: {
            int y, mo, dy;
            fast_days_to_ymd(v.get<nemo::types::Date>().days(), y, mo, dy);
            char buf[12];
            int len = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, mo, dy);
            return PyUnicode_FromStringAndSize(buf, len);
        }
        // Time: inline formatting from int64 micros since midnight
        case nemo::types::TypeID::Time: {
            std::int64_t us = v.get<nemo::types::Time>().microseconds();
            if (us < 0) us = 0;
            int hh = static_cast<int>(us / 3600000000LL);
            us %= 3600000000LL;
            int mm = static_cast<int>(us / 60000000LL);
            us %= 60000000LL;
            int ss = static_cast<int>(us / 1000000LL);
            int ff = static_cast<int>(us % 1000000LL);
            char buf[16];
            int len;
            if (ff == 0)
                len = std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);
            else
                len = std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06d", hh, mm, ss, ff);
            return PyUnicode_FromStringAndSize(buf, len);
        }
        // Timestamp/TimestampTZ: inline formatting from int64 micros since 2000-01-01
        case nemo::types::TypeID::Timestamp:
        case nemo::types::TypeID::TimestampTZ: {
            std::int64_t us = v.get<nemo::types::Timestamp>().microseconds();
            std::int64_t days_part = us / 86400000000LL;
            std::int64_t time_us = us % 86400000000LL;
            if (time_us < 0) { days_part--; time_us += 86400000000LL; }
            int y, mo, dy;
            fast_days_to_ymd(static_cast<std::int32_t>(days_part), y, mo, dy);
            int hh = static_cast<int>(time_us / 3600000000LL);
            time_us %= 3600000000LL;
            int mm = static_cast<int>(time_us / 60000000LL);
            time_us %= 60000000LL;
            int ss = static_cast<int>(time_us / 1000000LL);
            int ff = static_cast<int>(time_us % 1000000LL);
            char buf[32];
            int len;
            if (ff == 0)
                len = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", y, mo, dy, hh, mm, ss);
            else
                len = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06d", y, mo, dy, hh, mm, ss, ff);
            return PyUnicode_FromStringAndSize(buf, len);
        }
        // UUID: inline hex formatting from 16 bytes, no string allocation
        case nemo::types::TypeID::UUID: {
            const auto& d = v.get<nemo::types::UUID>().data();
            char buf[37];
            std::snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],
                d[8],d[9],d[10],d[11],d[12],d[13],d[14],d[15]);
            return PyUnicode_FromStringAndSize(buf, 36);
        }
        // JSON/JSONB: must serialize tree - unavoidable allocation
        case nemo::types::TypeID::JSON:
        case nemo::types::TypeID::JSONB: {
            std::string s = v.to_string();
            return PyUnicode_FromStringAndSize(s.data(), s.size());
        }
        // Interval: complex format, use to_string
        case nemo::types::TypeID::Interval: {
            std::string s = v.to_string();
            return PyUnicode_FromStringAndSize(s.data(), s.size());
        }
        case nemo::types::TypeID::Array:
        case nemo::types::TypeID::DICT: {
            // Return JSON string representation directly instead of building nested Python objects.
            // Avoids recursive value_to_py calls and pybind11 overhead for complex containers.
            std::string s = v.to_string();
            return PyUnicode_FromStringAndSize(s.data(), s.size());
        }
        case nemo::types::TypeID::Bytea: {
            std::string s = v.to_string();
            return PyBytes_FromStringAndSize(s.data(), s.size());
        }
        default:
            return value_to_py(v).release().ptr();
    }
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

        // 1. Direct single-row point query
        if (impl->single_row_idx != static_cast<std::size_t>(-1) && impl->table_ref) {
            if (impl->single_row_idx < impl->table_ref->sample_rows.size()) {
                const auto& r = impl->table_ref->sample_rows[impl->single_row_idx];
                std::size_t n = r.size();
                PyObject* py_tuple = PyTuple_New(n);
                for (std::size_t c = 0; c < n; ++c) {
                    PyTuple_SET_ITEM(py_tuple, c, fast_val_to_py(r[c]));
                }
                impl->current_row++;
                PyList_SET_ITEM(py_list, 0, py_tuple);
            }
            return py::reinterpret_steal<py::object>(py_list);
        }

        // 1b. Direct zero-copy range slice (e.g. SELECT * with no WHERE, or BETWEEN on id col)
        if (impl->is_slice && impl->table_ref) {
            const auto& srows = impl->table_ref->sample_rows;
            std::size_t base = impl->slice_start;
            std::size_t total = impl->total_rows;
            std::size_t cur = impl->current_row;
            std::size_t available = (total > cur) ? (total - cur) : 0;
            std::size_t actual = std::min(fetch_count, available);

            // Fast path: build entire list when row count is known and no tombstones expected.
            // Pre-detect column count from first live row to avoid per-row r.size() call.
            std::size_t n_cols = 0;
            if (actual > 0 && (base + cur) < srows.size() && !srows[base + cur].empty()) {
                n_cols = srows[base + cur].size();
            }

            if (n_cols > 0) {
                std::size_t out_idx = 0;
                while (out_idx < fetch_count && impl->current_row < total) {
                    std::size_t row_idx = base + impl->current_row++;
                    if (row_idx >= srows.size()) break;
                    const auto& r = srows[row_idx];
                    if (r.empty()) continue;
                    PyObject* py_tuple = PyTuple_New(n_cols);
                    for (std::size_t c = 0; c < n_cols; ++c) {
                        PyTuple_SET_ITEM(py_tuple, c, fast_val_to_py(r[c]));
                    }
                    PyList_SET_ITEM(py_list, out_idx++, py_tuple);
                }
                if (out_idx < fetch_count) {
                    PyList_SetSlice(py_list, static_cast<Py_ssize_t>(out_idx), static_cast<Py_ssize_t>(fetch_count), NULL);
                }
            } else {
                std::size_t out_idx = 0;
                while (out_idx < fetch_count && impl->current_row < total) {
                    std::size_t row_idx = base + impl->current_row++;
                    if (row_idx >= srows.size()) break;
                    const auto& r = srows[row_idx];
                    if (r.empty()) continue;
                    std::size_t n = r.size();
                    PyObject* py_tuple = PyTuple_New(n);
                    for (std::size_t c = 0; c < n; ++c) {
                        PyTuple_SET_ITEM(py_tuple, c, fast_val_to_py(r[c]));
                    }
                    PyList_SET_ITEM(py_list, out_idx++, py_tuple);
                }
                if (out_idx < fetch_count) {
                    PyList_SetSlice(py_list, static_cast<Py_ssize_t>(out_idx), static_cast<Py_ssize_t>(fetch_count), NULL);
                }
            }
            return py::reinterpret_steal<py::object>(py_list);
        }

        // 1c. Direct zero-copy two-slice (e.g. UNION ALL over same table)
        if (impl->is_two_slice && impl->table_ref) {
            std::size_t c_idx = impl->slice_col_idx;
            const auto& srows = impl->table_ref->sample_rows;
            for (std::size_t i = 0; i < fetch_count; ++i) {
                std::size_t cur = impl->current_row++;
                std::size_t row_idx = (cur < impl->slice1_count)
                    ? (impl->slice1_start + cur)
                    : (impl->slice2_start + (cur - impl->slice1_count));
                if (row_idx < srows.size() && c_idx < srows[row_idx].size()) {
                    PyObject* py_tuple = PyTuple_New(1);
                    PyTuple_SET_ITEM(py_tuple, 0, fast_val_to_py(srows[row_idx][c_idx]));
                    PyList_SET_ITEM(py_list, i, py_tuple);
                }
            }
            return py::reinterpret_steal<py::object>(py_list);
        }

        // 2. Direct zero-copy join result
        if (impl->is_join && impl->join_tbl1 && impl->join_tbl2) {
            std::size_t proj_sz = impl->join_proj.size();
            for (std::size_t i = 0; i < fetch_count; ++i) {
                const auto& p = impl->join_pairs[impl->current_row++];
                const auto* r1_ptr = (p.first != static_cast<std::size_t>(-1) && p.first < impl->join_tbl1->sample_rows.size()) ? &impl->join_tbl1->sample_rows[p.first] : nullptr;
                const auto* r2_ptr = (p.second != static_cast<std::size_t>(-1) && p.second < impl->join_tbl2->sample_rows.size()) ? &impl->join_tbl2->sample_rows[p.second] : nullptr;

                PyObject* py_tuple = PyTuple_New(proj_sz);
                for (std::size_t c = 0; c < proj_sz; ++c) {
                    const auto& jc = impl->join_proj[c];
                    const auto* src = jc.from_tbl1 ? r1_ptr : r2_ptr;
                    if (src && jc.col_idx < src->size()) {
                        PyTuple_SET_ITEM(py_tuple, c, fast_val_to_py((*src)[jc.col_idx]));
                    } else {
                        Py_INCREF(Py_None);
                        PyTuple_SET_ITEM(py_tuple, c, Py_None);
                    }
                }
                PyList_SET_ITEM(py_list, i, py_tuple);
            }
            return py::reinterpret_steal<py::object>(py_list);
        }

        const auto& sample = impl->get_effective_rows();
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

        std::size_t out_idx = 0;
        while (out_idx < fetch_count && impl->current_row < sample_size) {
            std::size_t row_idx = impl->current_row++;
            const auto& sample_row = sample[row_idx];
            if (sample_row.empty()) continue;
            std::size_t num_cols = sample_row.size();
            PyObject* py_tuple = PyTuple_New(num_cols);
            for (std::size_t c = 0; c < num_cols; ++c) {
                PyTuple_SET_ITEM(py_tuple, c, fast_val_to_py(sample_row[c]));
            }
            PyList_SET_ITEM(py_list, out_idx++, py_tuple);
        }
        if (out_idx < fetch_count) {
            PyList_SetSlice(py_list, static_cast<Py_ssize_t>(out_idx), static_cast<Py_ssize_t>(fetch_count), NULL);
        }
        return py::reinterpret_steal<py::object>(py_list);
    };

    py::class_<Result>(m, "Result")
        .def("has_rows", &Result::has_rows)
        .def("affected_rows", &Result::affected_rows)
        .def("columns", [](Result& self) -> std::vector<ColumnMetadata> {
            auto* impl = self.get_impl();
            if (impl) {
                if (!impl->columns.empty()) return impl->columns;
                if (impl->table_ref) return impl->table_ref->columns;
            }
            return self.columns();
        })
        .def("statistics", &Result::statistics)
        .def("next", [](Result& self) -> std::unique_ptr<Row> {
            return self.next();
        })
        .def("fetchone", [fetchmany_impl](Result& self) -> py::object {
            py::object res = fetchmany_impl(self, 1);
            if (PyList_Check(res.ptr())) {
                if (PyList_GET_SIZE(res.ptr()) > 0) {
                    PyObject* item = PyList_GET_ITEM(res.ptr(), 0);
                    Py_INCREF(item);
                    return py::reinterpret_steal<py::object>(item);
                }
            }
            return py::none();
        })
        .def("fetchmany", fetchmany_impl, py::arg("size") = 1)
        .def("fetchall", [fetchmany_impl](Result& self) -> py::object {
            auto* impl = self.get_impl();
            if (!impl) return py::list();
            std::size_t remaining = (impl->total_rows > impl->current_row) ? (impl->total_rows - impl->current_row) : 0;
            return fetchmany_impl(self, remaining);
        })
        .def("__iter__", [](py::object self) { return self; })
        .def("__next__", [fetchmany_impl](Result& self) -> py::object {
            py::object res = fetchmany_impl(self, 1);
            if (PyList_Check(res.ptr()) && PyList_GET_SIZE(res.ptr()) > 0) {
                PyObject* item = PyList_GET_ITEM(res.ptr(), 0);
                Py_INCREF(item);
                return py::reinterpret_steal<py::object>(item);
            }
            throw py::stop_iteration();
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
        .def("execute", [](Connection& self, py::handle sql_obj, py::handle params_obj, double timeout) -> Result {
            (void)timeout;
            PyObject* s_raw = sql_obj.ptr();
            const char* s_ptr = nullptr;
            Py_ssize_t s_len = 0;
            if (PyUnicode_Check(s_raw)) {
                s_ptr = PyUnicode_AsUTF8AndSize(s_raw, &s_len);
            }
            std::string_view sql(s_ptr ? s_ptr : "", static_cast<std::size_t>(s_len));

            PyObject* p_raw = params_obj.ptr();
            if (p_raw && p_raw != Py_None) {
                if (PyTuple_Check(p_raw)) {
                    Py_ssize_t sz = PyTuple_GET_SIZE(p_raw);
                    if (sz > 0) {
                        std::vector<nemo::types::Value> values;
                        values.reserve(sz);
                        for (Py_ssize_t idx = 0; idx < sz; ++idx) {
                            py::handle item(PyTuple_GET_ITEM(p_raw, idx));
                            values.push_back(py_to_value(item));
                        }
                        auto res = self.execute(sql, values);
                        if (!res) throw std::runtime_error("Execute failed");
                        return std::move(res.value());
                    }
                } else if (PyList_Check(p_raw)) {
                    Py_ssize_t sz = PyList_GET_SIZE(p_raw);
                    if (sz > 0) {
                        std::vector<nemo::types::Value> values;
                        values.reserve(sz);
                        for (Py_ssize_t idx = 0; idx < sz; ++idx) {
                            py::handle item(PyList_GET_ITEM(p_raw, idx));
                            values.push_back(py_to_value(item));
                        }
                        auto res = self.execute(sql, values);
                        if (!res) throw std::runtime_error("Execute failed");
                        return std::move(res.value());
                    }
                }
            }
            if (sql.size() < 2048) {
                auto res = self.execute(sql);
                if (!res) throw std::runtime_error("Execute failed");
                return std::move(res.value());
            }
            py::gil_scoped_release release;
            auto res = self.execute(sql);
            if (!res) throw std::runtime_error("Execute failed");
            return std::move(res.value());
        }, py::arg("sql"), py::arg("parameters") = py::none(), py::arg("timeout") = 0.0)
        .def("flush", [](Connection& self) {
            auto res = self.flush();
            if (!res) throw std::runtime_error("Flush failed");
        })
        .def("checkpoint", [](Connection& self) {
            auto res = self.checkpoint();
            if (!res) throw std::runtime_error("Checkpoint failed");
        })
        .def("executemany", [](Connection& self, const std::string& sql, py::handle params_obj) {
                    if (!params_obj || params_obj.is_none()) {
                        return;
            }
                    if (!py::isinstance<py::sequence>(params_obj)) {
                throw std::runtime_error("executemany parameters must be a list or tuple of records");
            }

                    auto to_values = [](py::handle h) {
                        std::vector<nemo::types::Value> values;
                        if (h.is_none()) {
                            return values;
                        }
                        if (PyTuple_Check(h.ptr()) || PyList_Check(h.ptr()) || py::isinstance<py::sequence>(h)) {
                            py::sequence seq = py::reinterpret_borrow<py::sequence>(h);
                            values.reserve(py::len(seq));
                            for (py::handle item : seq) {
                                values.push_back(py_to_value(item));
                            }
                        } else {
                            values.push_back(py_to_value(h));
                        }
                        return values;
                    };

                    py::sequence seq = py::reinterpret_borrow<py::sequence>(params_obj);
                    for (py::handle item : seq) {
                        if (!item || item.is_none()) {
                            continue;
                }
                        auto values = to_values(item);
                        if (values.empty()) {
                            continue;
                        }
                        auto res = self.execute(sql, values);
                        if (!res) {
                            throw std::runtime_error("Execute failed");
                }
            }
        }, py::arg("sql"), py::arg("parameters_list"))
        .def("pop", [](Connection& self, const std::string& table_name, std::size_t count, const std::string& direction) -> py::object {
            auto* conn_impl = self.get_impl();
            if (!conn_impl || !conn_impl->shared_state) {
                throw std::runtime_error("Invalid connection state");
            }
            std::shared_ptr<TableData> tbl;
            auto it = conn_impl->select_table_cache.find(table_name);
            if (it != conn_impl->select_table_cache.end()) {
                tbl = it->second;
            } else {
                tbl = conn_impl->shared_state->get_table(table_name);
                if (tbl) conn_impl->select_table_cache[table_name] = tbl;
            }
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
            std::shared_ptr<TableData> tbl;
            auto it = conn_impl->select_table_cache.find(table_name);
            if (it != conn_impl->select_table_cache.end()) {
                tbl = it->second;
            } else {
                tbl = conn_impl->shared_state->get_table(table_name);
                if (tbl) conn_impl->select_table_cache[table_name] = tbl;
            }
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
            conn_impl->cached_insert_template.valid = false;
            auto trash = std::make_unique<std::vector<std::vector<nemo::types::Value>>>();
            trash->swap(tbl->sample_rows);
            std::thread([to_delete = std::move(trash)]() mutable {
                to_delete->clear();
            }).detach();
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

            const auto& srows = tbl->sample_rows;
            const std::size_t total_srows = srows.size();

            // Build a compact index of live (non-tombstone) row indices once,
            // then iterate it for each column — avoids repeated empty() scans.
            std::vector<std::size_t> live_indices;
            live_indices.reserve(total_srows);
            for (std::size_t r = 0; r < total_srows; ++r) {
                if (!srows[r].empty()) live_indices.push_back(r);
            }
            std::size_t n_rows = live_indices.size();
            if (limit > 0 && limit < n_rows) n_rows = limit;

            for (std::size_t c = 0; c < col_indices.size(); ++c) {
                std::size_t c_idx = col_indices[c];
                PyObject* py_list = PyList_New(static_cast<Py_ssize_t>(n_rows));
                for (std::size_t out_r = 0; out_r < n_rows; ++out_r) {
                    std::size_t r = live_indices[out_r];
                    PyObject* py_val;
                    if (c_idx < srows[r].size()) {
                        py_val = fast_val_to_py(srows[r][c_idx]);
                    } else {
                        Py_INCREF(Py_None);
                        py_val = Py_None;
                    }
                    PyList_SET_ITEM(py_list, out_r, py_val);
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
        .def("commit", [](Connection& self) {
            auto res = self.commit();
            if (!res) throw std::runtime_error("Commit failed");
        })
        .def("rollback", [](Connection& self) {
            auto res = self.rollback();
            if (!res) throw std::runtime_error("Rollback failed");
        })
        .def("flush", [](Connection& self) {
            auto res = self.flush();
            if (!res) throw std::runtime_error("Flush failed");
        })
        .def("checkpoint", [](Connection& self) {
            auto res = self.checkpoint();
            if (!res) throw std::runtime_error("Checkpoint failed");
        })
        .def("begin", [](Connection& self) -> Transaction {
            auto res = self.begin();
            if (!res) throw std::runtime_error("Begin failed");
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
        .def("flush", [](nemo::Database& self) {
            auto res = self.flush();
            if (!res) throw std::runtime_error("Flush failed");
        })
        .def("checkpoint", [](nemo::Database& self) {
            auto res = self.checkpoint();
            if (!res) throw std::runtime_error("Checkpoint failed");
        })
        .def("statistics", [](nemo::Database& self) {
            auto s = self.statistics();
            py::dict d;
            d["active_connections"] = s.active_connections;
            d["worker_count"]       = s.worker_count;
            d["busy_workers"]       = s.busy_workers;
            d["buffer_hits"]        = s.buffer_hits;
            d["buffer_misses"]      = s.buffer_misses;
            d["buffer_pool_frames"] = s.buffer_pool_frames;
            d["recovery_complete"]  = s.recovery_complete;
            return d;
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

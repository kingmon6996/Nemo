# Nemo Datatypes Reference

**Nemo** provides native, first-class support for 22+ standard relational and semi-structured SQL datatypes. All datatypes are strongly typed within the C++20 storage engine and seamlessly map to Python standard library primitives during queries, parameter bindings, and result fetching.

For a full guide to SQL functions for these types, see the [SQL Built-in Functions Reference](sql_functions.md).

---

## 1. Complete Datatypes Reference Table

| SQL Type Name | Accepted Aliases | Storage / Bounds | Python Mapping | Example Value |
| :--- | :--- | :--- | :--- | :--- |
| `NULL` | — | SQL Null representation | `None` | `None` |
| `BOOLEAN` | `BOOL` | 1 byte (`true` / `false`) | `bool` | `True`, `False` |
| `TINYINT` | `INT1` | 8-bit signed integer (-128 to 127) | `int` | `42` |
| `SMALLINT` | `INT2`, `SHORT` | 16-bit signed integer (-32,768 to 32,767) | `int` | `1024`, `-32000` |
| `INTEGER` | `INT`, `INT4` | 32-bit signed integer (-2.14B to 2.14B) | `int` | `100000` |
| `BIGINT` | `INT8`, `LONG` | 64-bit signed integer (-9.22E18 to 9.22E18) | `int` | `9223372036854775807` |
| `REAL` | `FLOAT`, `FLOAT4` | 32-bit IEEE 754 Single-Precision Float | `float` | `3.14159` |
| `DOUBLE PRECISION`| `DOUBLE`, `FLOAT8` | 64-bit IEEE 754 Double-Precision Float | `float` | `2.718281828459045` |
| `NUMERIC(p, s)` | `DECIMAL(p, s)`, `DEC`| Fixed-point exact decimal representation | `decimal.Decimal` / `float` | `123456.78` |
| `VARCHAR(n)` | `CHARACTER VARYING`| Variable-length string | `str` | `'code_alpha_99'` |
| `TEXT` | `STRING` | Arbitrary-length UTF-8 encoded text | `str` | `'Unicode string text'` |
| `CHAR(n)` | `BPCHAR(n)` | Fixed-length blank-padded character string | `str` | `'ABCDE'` |
| `BYTEA` | `BLOB`, `BINARY` | Raw binary byte sequence | `bytes` / `bytearray` | `b'\xde\xad\xbe\xef'` |
| `DATE` | — | ISO Calendar Date (`YYYY-MM-DD`) | `datetime.date` / `str` | `'2026-10-25'` |
| `TIME` | — | Clock Time (`HH:MM:SS`) | `datetime.time` / `str` | `'14:30:00'` |
| `TIMESTAMP` | `DATETIME` | ISO Timestamp (`YYYY-MM-DDTHH:MM:SSZ`) | `datetime.datetime` / `str`| `'2026-10-25T14:30:00Z'`|
| `TIMESTAMPTZ` | — | ISO Timestamp with timezone offset | `datetime.datetime` / `str`| `'2026-10-25T14:30:00+00:00'`|
| `INTERVAL` | — | Elapsed duration / timespan | `datetime.timedelta` / `str`| `'1 day 02:00:00'` |
| `UUID` | `GUID` | 128-bit RFC 4122 UUID identifier | `uuid.UUID` / `str` | `'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'` |
| `ARRAY` | `T[]` | Ordered collection of elements | `list` | `[1, 2, 3, 4, 5]` |
| `JSON` | — | Structured JSON text with syntax verification | `dict`, `list`, `str` | `{"user": "alice", "active": true}` |
| `JSONB` | — | Binary decomposed JSON for high-speed extraction | `dict`, `str` | `{"score": 98.5, "tags": ["db"]}` |
| `DICT` | `MAP`, `HSTORE` | String-keyed key-value dictionary | `dict` | `{"tier": 1, "status": "active"}` |
| `ENUM` | — | Constrained set of named string constants | `str` | `'active'`, `'pending'`, `'closed'` |
| `BIT(n)` | — | Fixed-length bit string | `str` | `'10101010'` |
| `VARBIT(n)` | `BIT VARYING` | Variable-length bit string | `str` | `'110011'` |
| `INET` | — | IPv4 or IPv6 host address | `str` | `'192.168.1.1'`, `'2001:db8::1'` |
| `CIDR` | — | IPv4 or IPv6 network CIDR block | `str` | `'10.0.0.0/24'` |
| `MACADDR` | — | 48-bit Media Access Control hardware address | `str` | `'08:00:2b:01:02:03'` |
| `RANGE` | `INT4RANGE`, etc. | Contiguous interval with bounds `[lower, upper)` | `str` | `'[1, 100)'` |

---

## 2. Structured Collection Types Comparison

| Feature | `ARRAY` | `DICT` | `JSON` / `JSONB` |
| :--- | :--- | :--- | :--- |
| **Data Format** | Ordered list of items | Flat string-keyed map | Recursive multi-type document |
| **Element Types** | Homogeneous values | Flat key-value pairs | Arbitrary mixed types |
| **Nesting** | Linear list or matrix | Single-level map | Multi-level nested objects & arrays |
| **Speed Profile** | Fastest vector math & index slicing | O(1) attribute lookup | Flexible path extractions |
| **Ideal For** | Time series, tags, sensor metrics | Entity attributes, key-value lookup | REST APIs, payloads, configs |

---

## 3. Dedicated Functions by Datatype Category

Nemo provides specialized SQL functions for each complex datatype. Detailed references and signatures are available in the [SQL Built-in Functions Reference](sql_functions.md):

- **JSON & JSONB**: `json_extract()`, `json_value()`, `json_set()`, `json_insert()`, `json_remove()`, `json_keys()`, `json_length()`, `json_contains()`, `json_merge()`, `jsonb_extract()`, `jsonb_value()`, `jsonb_set()`.
- **ARRAY**: `array_length()`, `array_get()`, `array_slice()`, `array_append()`, `array_remove()`, `array_contains()`, `array_distinct()`, `array_sort()`, `array_sum()`, `array_avg()`.
- **DICT**: `dict_get()`, `dict_has_key()`, `dict_keys()`, `dict_values()`, `dict_size()`, `dict_set()`, `dict_remove()`, `dict_merge()`.
- **RANGE**: `range()`, `range_lower()`, `range_upper()`, `range_contains()`.
- **BIT**: `bit_length()`, `bit_count()`, `bit_and()`, `bit_or()`, `bit_xor()`, `bit_not()`.
- **NETWORK**: `host()`, `netmask()`, `is_ipv4()`, `is_ipv6()`.
- **UUID & BINARY**: `uuid_generate()`, `uuid_to_string()`, `hex_encode()`, `hex_decode()`, `base64_encode()`, `base64_decode()`.

---

## Related Runnable Examples

- [`example_3.py`](../example/example_3.py): Comprehensive 30 Native Datatypes Demonstration.
- [`example_15.py`](../example/example_15.py): Numeric Datatypes Deep-Dive (`TINYINT` to `BIGINT`, `REAL`, `DOUBLE`).
- [`example_16.py`](../example/example_16.py): Exact Financial Decimals (`DECIMAL(p, s)`).
- [`example_17.py`](../example/example_17.py): Text, International Unicode, and Raw Binary Blobs (`BYTEA`).
- [`example_18.py`](../example/example_18.py): Temporal Types & Timestamps (`DATE`, `TIME`, `TIMESTAMP`, `INTERVAL`).
- [`example_19.py`](../example/example_19.py): Semi-Structured JSON & JSONB Documents.
- [`example_20.py`](../example/example_20.py): Complex Collections: `ARRAY` and `DICT`.
- [`example_21.py`](../example/example_21.py): System & Network Types (`UUID`, `INET`, `CIDR`, `RANGE`).



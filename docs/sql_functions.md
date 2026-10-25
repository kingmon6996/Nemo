# Nemo SQL Built-in Functions Reference

**Nemo** provides over 70 high-performance built-in SQL functions implemented natively in C++20 for data extraction, scalar transformation, mathematical analysis, vector processing, temporal parsing, and sequence generation.

Functions can be used directly within `SELECT` projections, `WHERE` filter clauses, `ORDER BY` expressions, and dual/expression queries (e.g., `SELECT now()`).

---

## Table of Contents

1. [JSON & JSONB Functions](#1-json--jsonb-functions)
2. [ARRAY Vector & List Functions](#2-array-vector--list-functions)
3. [DICT Key-Value Functions](#3-dict-key-value-functions)
4. [String & Text Processing Functions](#4-string--text-processing-functions)
5. [Mathematical & Numeric Functions](#5-mathematical--numeric-functions)
6. [Range Boundary & Interval Functions](#6-range-boundary--interval-functions)
7. [Temporal (Date, Time, Timestamp) Functions](#7-temporal-functions)
8. [Binary, Base64, Hex & UUID Functions](#8-binary-base64-hex--uuid-functions)
9. [Bitwise & Bit-String Functions](#9-bitwise--bit-string-functions)
10. [Network (INET & CIDR) Functions](#10-network-functions)
11. [Sequence & Atomic Range Allocation Functions](#11-sequence--atomic-range-allocation-functions)
12. [Conditional & Type Utility Functions](#12-conditional--type-utility-functions)

---

## 1. JSON & JSONB Functions

Nemo supports both textual `JSON` and binary-decomposed `JSONB` documents. Functions accept standard JSON dot-notation paths (e.g. `$.user.profile.age` or `user.name`) and array subscript indices (e.g. `$.items[0]`).

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `json_extract(doc, path)` | `VALUE` | Extracts scalar value or nested JSON structure at path. Automatically typed. | `SELECT json_extract(doc, '$.user.id');` |
| `json_value(doc, path)` | `VALUE` | Extracts and unquotes scalar string or numeric value at path. | `SELECT json_value(doc, '$.status');` |
| `json_get_text(doc, path)` | `TEXT` | Extracts value strictly coerced to `TEXT`. | `SELECT json_get_text(doc, '$.code');` |
| `json_get_int(doc, path)` | `BIGINT` | Extracts value strictly coerced to 64-bit integer. | `SELECT json_get_int(doc, '$.age');` |
| `json_get_float(doc, path)` | `DOUBLE` | Extracts value strictly coerced to double-precision float. | `SELECT json_get_float(doc, '$.score');` |
| `json_get_bool(doc, path)` | `BOOLEAN`| Extracts value strictly coerced to boolean. | `SELECT json_get_bool(doc, '$.verified');` |
| `json_get_json(doc, path)` | `JSON` | Extracts sub-fragment as valid JSON document. | `SELECT json_get_json(doc, '$.config');` |
| `json_exists(doc, path)` | `BOOLEAN`| Returns `TRUE` if path exists in document, else `FALSE`. | `SELECT json_exists(doc, '$.token');` |
| `json_keys(doc)` | `ARRAY` | Returns all top-level keys of a JSON object as an array. | `SELECT json_keys('{"a": 1, "b": 2}');` |
| `json_values(doc)` | `ARRAY` | Returns all top-level values of a JSON object as an array. | `SELECT json_values('{"a": 1, "b": 2}');` |
| `json_length(doc)` | `BIGINT` | Returns key count for objects or element count for arrays. | `SELECT json_length(doc);` |
| `json_typeof(doc)` | `TEXT` | Returns type name: `'object'`, `'array'`, `'string'`, `'number'`, `'boolean'`, or `'null'`. | `SELECT json_typeof('{"x": 1}');` |
| `json_valid(text)` | `BOOLEAN`| Validates whether text is well-formed JSON syntax. | `SELECT json_valid(payload);` |
| `json_contains(doc, target)` | `BOOLEAN`| Checks if key, substring, or element exists within JSON. | `SELECT json_contains(doc, 'admin');` |
| `json_set(doc, path, val)` | `JSON` | Sets or updates key/element at path and returns mutated JSON. | `SELECT json_set('{"a": 1}', '$.b', 2);` |
| `json_insert(doc, path, val)`| `JSON` | Inserts key/element only if path does not already exist. | `SELECT json_insert(doc, '$.tier', 'free');` |
| `json_remove(doc, path)` | `JSON` | Deletes key or array element at path. | `SELECT json_remove(doc, '$.temporary');` |
| `json_merge(doc1, doc2)` | `JSON` | Shallow merges two JSON objects. Keys in `doc2` overwrite `doc1`. | `SELECT json_merge(base, overrides);` |
| `json_pretty(doc)` | `JSON` | Formats JSON string. | `SELECT json_pretty(doc);` |
| `json_quote(str)` | `TEXT` | Wraps and escapes string as a JSON literal. | `SELECT json_quote('hello');` |
| `json_unquote(str)` | `TEXT` | Strips quotes and unescapes JSON string. | `SELECT json_unquote('"hello"');` |
| `jsonb_extract(doc, path)` | `VALUE` | High-speed binary path extraction for `JSONB` columns. | `SELECT jsonb_extract(doc_jsonb, '$.id');` |
| `jsonb_value(doc, path)` | `VALUE` | Unquoted binary path extraction for `JSONB` columns. | `SELECT jsonb_value(doc_jsonb, '$.user.level');` |
| `jsonb_contains(doc, target)`| `BOOLEAN`| Fast containment search across binary `JSONB` structures. | `SELECT jsonb_contains(doc_jsonb, 'vip');` |
| `jsonb_set(doc, path, val)` | `JSONB` | Mutates binary JSONB document at specified path. | `SELECT jsonb_set(doc_jsonb, '$.level', 2);` |
| `jsonb_merge(doc1, doc2)` | `JSONB` | Merges two JSONB binary objects. | `SELECT jsonb_merge(doc1, doc2);` |

---

## 2. ARRAY Vector & List Functions

Nemo provides native array collections with 1-based SQL standard indexing, vector math, and set operations.

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `array_length(arr)` | `BIGINT` | Number of elements in array. | `SELECT array_length(tags);` |
| `array_cardinality(arr)` | `BIGINT` | Alias for `array_length`. | `SELECT array_cardinality(tags);` |
| `array_dims(arr)` | `TEXT` | Dimensional bounds as string (e.g. `'[1:5]'`). | `SELECT array_dims(tags);` |
| `array_ndims(arr)` | `BIGINT` | Number of dimensions (returns 1 for standard arrays). | `SELECT array_ndims(tags);` |
| `array_lower(arr)` | `BIGINT` | Lower bound index (always 1 for standard arrays). | `SELECT array_lower(tags);` |
| `array_upper(arr)` | `BIGINT` | Upper bound index (equivalent to array length). | `SELECT array_upper(tags);` |
| `array_is_empty(arr)` | `BOOLEAN`| Returns `TRUE` if array has 0 elements or is null. | `SELECT array_is_empty(tags);` |
| `array_get(arr, idx)` | `VALUE` | 1-based element lookup. | `SELECT array_get(tags, 1);` |
| `array_first(arr)` | `VALUE` | Returns first element (`arr[1]`). | `SELECT array_first(tags);` |
| `array_last(arr)` | `VALUE` | Returns last element (`arr[length]`). | `SELECT array_last(tags);` |
| `array_slice(arr, start, end)`| `ARRAY` | Returns sub-array between 1-based `start` and `end` bounds. | `SELECT array_slice(tags, 2, 4);` |
| `array_append(arr, val)` | `ARRAY` | Appends element to end of array. | `SELECT array_append(tags, 'new_tag');` |
| `array_prepend(val, arr)` | `ARRAY` | Prepends element to front of array. | `SELECT array_prepend('first', tags);` |
| `array_concat(arr1, arr2)` | `ARRAY` | Concatenates two arrays into one. | `SELECT array_concat(tags1, tags2);` |
| `array_remove(arr, val)` | `ARRAY` | Removes all occurrences of `val` from array. | `SELECT array_remove(tags, 'obsolete');` |
| `array_replace(arr, old, new)`| `ARRAY` | Replaces occurrences of `old` with `new`. | `SELECT array_replace(tags, 'v1', 'v2');` |
| `array_position(arr, val)` | `BIGINT` | 1-based index of first matching element (or `NULL` if absent). | `SELECT array_position(tags, 'active');` |
| `array_contains(arr, val)` | `BOOLEAN`| Membership check. Returns `TRUE` if `val` is in array. | `SELECT array_contains(tags, 'core');` |
| `array_distinct(arr)` | `ARRAY` | Deduplicates array while preserving insertion order. | `SELECT array_distinct(tags);` |
| `array_sort(arr)` | `ARRAY` | Sorts elements in ascending natural order. | `SELECT array_sort('[3, 1, 2]');` |
| `array_reverse(arr)` | `ARRAY` | Reverses order of elements. | `SELECT array_reverse(tags);` |
| `array_sum(arr)` | `DOUBLE` | Arithmetic sum of numeric elements. | `SELECT array_sum(metrics);` |
| `array_avg(arr)` | `DOUBLE` | Arithmetic mean of numeric elements. | `SELECT array_avg(metrics);` |
| `array_min(arr)` | `VALUE` | Minimum value in array. | `SELECT array_min(metrics);` |
| `array_max(arr)` | `VALUE` | Maximum value in array. | `SELECT array_max(metrics);` |
| `array_to_string(arr, delim)` | `TEXT` | Joins array elements with delimiter string. | `SELECT array_to_string(tags, ',');` |
| `string_to_array(str, delim)` | `ARRAY` | Splits string by delimiter into typed array. | `SELECT string_to_array('a,b,c', ',');` |
| `array_to_json(arr)` | `JSON` | Converts array to JSON array string. | `SELECT array_to_json(tags);` |
| `json_to_array(json_arr)` | `ARRAY` | Converts JSON array string to native ARRAY. | `SELECT json_to_array('["a", "b"]');` |

---

## 3. DICT Key-Value Functions

`DICT` stores flat string-to-value key-value dictionaries with fast O(1) attribute lookup.

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `dict_get(dict, key)` | `VALUE` | Retrieves value associated with key. | `SELECT dict_get(attrs, 'role');` |
| `dict_get_or(dict, key, def)`| `VALUE` | Retrieves key value, falling back to `def` if absent. | `SELECT dict_get_or(attrs, 'k', 'N/A');` |
| `dict_has_key(dict, key)` | `BOOLEAN`| Checks if key exists in dictionary. | `SELECT dict_has_key(attrs, 'status');` |
| `dict_keys(dict)` | `ARRAY` | Returns all keys in dictionary as an ARRAY. | `SELECT dict_keys(attrs);` |
| `dict_values(dict)` | `ARRAY` | Returns all values in dictionary as an ARRAY. | `SELECT dict_values(attrs);` |
| `dict_size(dict)` | `BIGINT` | Number of key-value pairs in dictionary. | `SELECT dict_size(attrs);` |
| `dict_is_empty(dict)` | `BOOLEAN`| Returns `TRUE` if dictionary has 0 keys. | `SELECT dict_is_empty(attrs);` |
| `dict_set(dict, key, val)` | `DICT` | Updates key if present or appends key-value pair. | `SELECT dict_set(attrs, 'tier', 2);` |
| `dict_remove(dict, key)` | `DICT` | Removes key from dictionary. | `SELECT dict_remove(attrs, 'obsolete');` |
| `dict_merge(dict1, dict2)` | `DICT` | Merges two dictionaries. `dict2` entries take precedence. | `SELECT dict_merge(d1, d2);` |
| `dict_to_json(dict)` | `JSON` | Converts dictionary to JSON object string. | `SELECT dict_to_json(attrs);` |
| `json_to_dict(json_obj)` | `DICT` | Converts JSON object to native DICT. | `SELECT json_to_dict('{"k": 1}');` |

---

## 4. String & Text Processing Functions

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `upper(str)` | `TEXT` | Converts string to uppercase. | `SELECT upper(name);` |
| `lower(str)` | `TEXT` | Converts string to lowercase. | `SELECT lower(code);` |
| `length(str)` / `len(str)` | `BIGINT` | Number of characters in string. | `SELECT length(username);` |
| `octet_length(str)` | `BIGINT` | Number of bytes in string encoding. | `SELECT octet_length(data);` |
| `concat(s1, s2, ...)` | `TEXT` | Concatenates multiple strings together. Ignores NULLs. | `SELECT concat(first, ' ', last);` |
| `concat_ws(sep, s1, ...)` | `TEXT` | Concatenates strings with separator. | `SELECT concat_ws(', ', city, state);` |
| `substring(str, start, len)` | `TEXT` | Extracts substring from 1-based `start` index with length `len`. | `SELECT substring(title, 1, 10);` |
| `substr(str, start, len)` | `TEXT` | Alias for `substring`. | `SELECT substr(code, 3, 4);` |
| `trim(str)` | `TEXT` | Strips leading and trailing whitespace. | `SELECT trim('  nemo  ');` |
| `ltrim(str)` | `TEXT` | Strips leading whitespace. | `SELECT ltrim('  hello');` |
| `rtrim(str)` | `TEXT` | Strips trailing whitespace. | `SELECT rtrim('world  ');` |
| `replace(str, from, to)` | `TEXT` | Replaces all occurrences of `from` with `to`. | `SELECT replace(title, 'User', 'Admin');` |
| `position(needle, haystack)` | `BIGINT` | 1-based index of substring in text (or `0` if not found). | `SELECT position('db', 'klyrodb');` |
| `split_part(str, delim, part)`| `TEXT` | Splits string by delimiter and returns 1-based `part`. | `SELECT split_part('a/b/c', '/', 2);` |
| `starts_with(str, prefix)` | `BOOLEAN`| Checks if string begins with prefix. | `SELECT starts_with(code, 'PRE_');` |
| `ends_with(str, suffix)` | `BOOLEAN`| Checks if string terminates with suffix. | `SELECT ends_with(email, '.org');` |
| `contains(str, substr)` | `BOOLEAN`| Checks if substring exists anywhere in text. | `SELECT contains(title, 'crypto');` |
| `repeat(str, count)` | `TEXT` | Repeats string `count` times. | `SELECT repeat('=', 10);` |
| `reverse(str)` | `TEXT` | Reverses string characters. | `SELECT reverse('nemo');` |
| `lpad(str, len, [pad])` | `TEXT` | Left-pads string to target length. Default pad is space. | `SELECT lpad('42', 5, '0');` -> `'00042'` |
| `rpad(str, len, [pad])` | `TEXT` | Right-pads string to target length. Default pad is space. | `SELECT rpad('user', 8, '.');` |

---

## 5. Mathematical & Numeric Functions

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `abs(num)` | `NUMERIC` | Absolute value. | `SELECT abs(-123.45);` |
| `round(num, [decimals])` | `NUMERIC` | Rounds number to optional decimal places (default 0). | `SELECT round(amount, 2);` |
| `floor(num)` | `BIGINT` | Greatest integer less than or equal to argument. | `SELECT floor(14.8);` -> `14` |
| `ceil(num)` / `ceiling(num)` | `BIGINT` | Smallest integer greater than or equal to argument. | `SELECT ceil(14.2);` -> `15` |
| `trunc(num, [decimals])` | `NUMERIC` | Truncates number toward zero. | `SELECT trunc(14.89, 1);` -> `14.8` |
| `mod(a, b)` | `BIGINT` | Integer remainder of `a / b`. | `SELECT mod(10, 3);` -> `1` |
| `power(base, exp)` | `DOUBLE` | Raises `base` to the power of `exp`. | `SELECT power(2, 10);` -> `1024.0` |
| `sqrt(num)` | `DOUBLE` | Square root of non-negative number. | `SELECT sqrt(16);` -> `4.0` |
| `sign(num)` | `INTEGER` | Returns `1` for positive, `-1` for negative, and `0` for zero. | `SELECT sign(-42);` -> `-1` |
| `greatest(val1, val2, ...)` | `VALUE` | Returns greatest scalar across arguments. | `SELECT greatest(10, 99, 45);` -> `99` |
| `least(val1, val2, ...)` | `VALUE` | Returns smallest scalar across arguments. | `SELECT least(10, 99, 45);` -> `10` |

---

## 6. Range Boundary & Interval Functions

Nemo supports relational ranges (e.g. `[lower, upper)` or `(lower, upper]`) representing contiguous intervals:

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `range(low, high)` | `TEXT` | Constructs a standard interval string `[low, high)`. | `SELECT range(10, 20);` -> `'[10, 20)'` |
| `range_lower(range_str)` | `VALUE` | Extracts the lower bound scalar value. | `SELECT range_lower('[10, 50)');` -> `10` |
| `range_upper(range_str)` | `VALUE` | Extracts the upper bound scalar value. | `SELECT range_upper('[10, 50)');` -> `50` |
| `range_contains(range, pt)` | `BOOLEAN`| Evaluates if point `pt` falls within interval boundaries. | `SELECT range_contains('[1, 100)', 50);` -> `TRUE` |

---

## 7. Temporal Functions

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `current_date()` | `DATE` | Current system calendar date (`YYYY-MM-DD`). | `SELECT current_date();` |
| `current_time()` | `TIME` | Current system time (`HH:MM:SS`). | `SELECT current_time();` |
| `current_timestamp()` | `TIMESTAMP`| Current UTC timestamp in ISO 8601 (`YYYY-MM-DDTHH:MM:SSZ`). | `SELECT current_timestamp();` |
| `now()` | `TIMESTAMP`| Standard alias for `current_timestamp()`. | `SELECT now();` |
| `extract(field FROM ts)` | `BIGINT` | Extracts part: `'year'`, `'month'`, `'day'`, `'hour'`, `'minute'`, `'second'`. | `SELECT extract('year' FROM created_at);` |
| `date_part(field, ts)` | `BIGINT` | Function syntax alias for `extract`. | `SELECT date_part('month', created_at);` |

---

## 8. Binary, Base64, Hex & UUID Functions

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `hex_encode(str)` | `TEXT` | Encodes text string or binary blob into hexadecimal string. | `SELECT hex_encode('nemo');` -> `'6e656d6f'` |
| `hex_decode(hex_str)` | `BYTEA` | Decodes hexadecimal string into raw binary bytes. | `SELECT hex_decode('6e656d6f');` |
| `base64_encode(str)` | `TEXT` | Encodes string or bytes to Base64 ASCII format. | `SELECT base64_encode('hello world');` |
| `base64_decode(b64_str)` | `BYTEA` | Decodes Base64 ASCII string back to raw binary bytes. | `SELECT base64_decode('aGVsbG8=');` |
| `uuid_generate()` | `UUID` | Generates a new random RFC 4122 Version 4 UUID. | `SELECT uuid_generate();` |
| `uuid_to_string(uuid)` | `TEXT` | Converts UUID value to formatted canonical 36-character string. | `SELECT uuid_to_string(id);` |
| `uuid_parse(str)` | `UUID` | Parses 36-character canonical string into UUID type. | `SELECT uuid_parse('a0eebc99-...');` |

---

## 9. Bitwise & Bit-String Functions

Functions for querying and manipulating bit-string types (`BIT(n)`, `VARBIT(n)`):

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `bit_length(bitmask)` | `BIGINT` | Returns total bit length of bit-string. | `SELECT bit_length('110100');` -> `6` |
| `bit_count(bitmask)` | `BIGINT` | Population count: number of `1` bits set. | `SELECT bit_count('110100');` -> `3` |
| `bit_and(mask1, mask2)` | `BIT` | Bitwise logical AND conjunction. | `SELECT bit_and('1100', '1010');` -> `'1000'` |
| `bit_or(mask1, mask2)` | `BIT` | Bitwise logical OR disjunction. | `SELECT bit_or('1100', '1010');` -> `'1110'` |
| `bit_xor(mask1, mask2)` | `BIT` | Bitwise logical exclusive OR. | `SELECT bit_xor('1100', '1010');` -> `'0110'` |
| `bit_not(mask)` | `BIT` | Inverts all bits (one's complement). | `SELECT bit_not('1100');` -> `'0011'` |

---

## 10. Network Functions

Functions for working with IP addresses and subnets (`INET`, `CIDR`):

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `host(ip_or_cidr)` | `INET` | Extracts host IP address without CIDR netmask prefix. | `SELECT host('192.168.1.10/24');` -> `'192.168.1.10'` |
| `netmask(cidr)` | `INET` | Returns standard dotted-decimal subnet mask for CIDR block. | `SELECT netmask('10.0.0.0/24');` -> `'255.255.255.0'` |
| `is_ipv4(ip_str)` | `BOOLEAN`| Returns `TRUE` if address is a valid IPv4 address. | `SELECT is_ipv4(client_ip);` |
| `is_ipv6(ip_str)` | `BOOLEAN`| Returns `TRUE` if address is a valid IPv6 address. | `SELECT is_ipv6(client_ip);` |

---

## 11. Sequence & Atomic Range Allocation Functions

Durable database-managed sequences provide atomic, monotonically increasing identifiers across concurrent writers.

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `nextval('seq_name')` | `BIGINT` | Advances sequence by its increment step and returns the newly generated value. Atomic and thread-safe. | `SELECT nextval('order_seq');` |
| `currval('seq_name')` | `BIGINT` | Returns current value of sequence from the most recent `nextval` call in current session. | `SELECT currval('order_seq');` |
| `nextval_range('seq', count)`| `TEXT` | Atomically reserves a contiguous block of `count` sequence IDs and returns formatted range `start_id..end_id`. | `SELECT nextval_range('order_seq', 50);` |

---

## 12. Conditional & Type Utility Functions

| Function Signature | Return Type | Description | Example Query |
| :--- | :--- | :--- | :--- |
| `coalesce(v1, v2, ...)` | `VALUE` | Returns first non-NULL expression across arguments. | `SELECT coalesce(nickname, username, 'anonymous');` |
| `ifnull(v1, v2)` | `VALUE` | Two-argument shorthand for `coalesce`. | `SELECT ifnull(discount, 0.0);` |
| `nullif(v1, v2)` | `VALUE` | Returns `NULL` if `v1 == v2`, otherwise returns `v1`. | `SELECT nullif(total, 0);` |
| `isnull(val)` | `BOOLEAN`| Returns `TRUE` if value is `NULL`. | `SELECT isnull(deleted_at);` |
| `typeof(val)` | `TEXT` | Returns canonical runtime type name (`'integer'`, `'real'`, `'text'`, `'json'`, `'array'`, `'dict'`, etc.). | `SELECT typeof(payload);` |
| `cast(val AS type)` | `ANY` | Casts value to target datatype. Throws error on failure. | `SELECT cast('123' AS INTEGER);` |
| `try_cast(val AS type)` | `ANY` | Safe cast. Returns `NULL` instead of error on failure. | `SELECT try_cast('invalid' AS INTEGER);` |

---

## Related Runnable Examples

- [`example_4.py`](../example/example_4.py): Complete Built-in SQL Functions Catalog Overview.
- [`example_22.py`](../example/example_22.py): JSON & JSONB Functions (`json_extract`, `json_length`, `json_contains`, `json_set`).
- [`example_23.py`](../example/example_23.py): Collection Functions (`array_get`, `array_sum`, `array_append`, `dict_get`, `dict_has_key`).
- [`example_24.py`](../example/example_24.py): String Processing Functions (`upper`, `lower`, `concat`, `substring`, `trim`).
- [`example_25.py`](../example/example_25.py): Mathematical & Numeric Functions (`abs`, `round`, `sqrt`, `power`, `mod`).
- [`example_26.py`](../example/example_26.py): Range, Network & Utility Functions (`range_lower`, `host`, `netmask`, `coalesce`).



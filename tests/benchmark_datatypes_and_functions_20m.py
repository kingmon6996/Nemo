"""
benchmark_datatypes_and_functions_20m.py
=========================================
Insert 20 million rows into a table that covers ALL 30 Nemo native datatypes,
then exercise all 72+ built-in SQL functions across every category.

Sections
--------
  SETUP     — Table schema with every column type
  PHASE 1   — 20 M row bulk insert (executemany, batch=100k)
  PHASE 2   — JSON / JSONB  functions  (20 functions)
  PHASE 3   — ARRAY         functions  (25 functions)
  PHASE 4   — DICT          functions  (13 functions)
  PHASE 5   — String        functions  (21 functions)
  PHASE 6   — Math          functions  (11 functions)
  PHASE 7   — Range         functions  ( 4 functions)
  PHASE 8   — Temporal      functions  ( 6 functions)
  PHASE 9   — Binary/UUID   functions  ( 7 functions)
  PHASE 10  — Bitwise       functions  ( 6 functions)
  PHASE 11  — Network       functions  ( 4 functions)
  PHASE 12  — Sequence      functions  ( 3 functions)
  PHASE 13  — Conditional   functions  ( 7 functions)
  SUMMARY   — timing table

Run:
    python tests/benchmark_datatypes_and_functions_20m.py
"""

from __future__ import annotations

import os
import sys
import time
import shutil
import random
import string
from typing import Any, List, Tuple

# ── make sure the package is importable ──────────────────────────────────────
_PACKAGE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if _PACKAGE not in sys.path:
    sys.path.insert(0, _PACKAGE)

import nemo

# ═════════════════════════════════════════════════════════════════════════════
# Configuration
# ═════════════════════════════════════════════════════════════════════════════
TOTAL_ROWS      = 20_000_000
BULK_BATCH_SIZE = 100_000
BUFFER_POOL_MB  = 512
DB_NAME         = "bench_datatypes_functions_20m"

# How many rows to scan per function call (keeps each test fast)
# Full-table scans with SQL functions on 20 M rows can be slow;
# use LIMIT to test correctness + measure speed on a sampled projection.
FUNC_SAMPLE     = 10_000   # rows returned by LIMIT in function queries
# For aggregate functions we scan the whole table
AGG_SCAN        = True

SEP  = "═" * 90
SEP2 = "─" * 90

_RNG = random.Random(7)

# ═════════════════════════════════════════════════════════════════════════════
# Timing
# ═════════════════════════════════════════════════════════════════════════════
class Timer:
    elapsed: float = 0.0
    def __enter__(self):
        self._s = time.perf_counter()
        return self
    def __exit__(self, *_):
        self.elapsed = time.perf_counter() - self._s

def fmt(t: float) -> str:
    return f"{t/60:.1f} min" if t >= 60 else f"{t:.4f} s"

results: List[Tuple[str, float, int, str]] = []   # (label, elapsed, rows, status)

def record(label: str, t: float, rows: int, status: str = "OK"):
    results.append((label, t, rows, status))

# ═════════════════════════════════════════════════════════════════════════════
# Data-generation helpers
# ═════════════════════════════════════════════════════════════════════════════
_REGIONS  = ["North", "South", "East", "West", "Central"]
_STATUSES = ["active", "inactive", "pending"]
_BITS_4   = ["0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
              "1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111"]
_IPV4_POOL = [f"10.{a}.{b}.{c}" for a in range(10) for b in range(10) for c in range(10)]
_UUIDS    = [f"a0eebc99-9c0b-4ef8-bb6d-{i:012d}" for i in range(1_000_000)]

def _rand_str(n: int) -> str:
    return "".join(_RNG.choices(string.ascii_lowercase + string.digits, k=n))

def _make_row(i: int) -> Tuple:
    """
    One row covering all 30 Nemo datatypes.
    Columns:
      id              BIGINT         — primary key
      ti              TINYINT        — -128..127
      si              SMALLINT       — -32768..32767
      ii              INTEGER        — arbitrary int
      bi              BIGINT         — large int
      rl              REAL           — 32-bit float
      db              DOUBLE         — 64-bit float
      nm              NUMERIC(12,4)  — exact decimal
      vc              VARCHAR        — variable-length string
      tx              TEXT           — arbitrary long text
      ch              CHAR(5)        — fixed 5-char string
      by              BYTEA          — raw bytes (stored as hex string in SQL)
      dt              DATE           — YYYY-MM-DD
      tm              TIME           — HH:MM:SS
      ts              TIMESTAMP      — ISO timestamp
      tstz            TIMESTAMPTZ    — with timezone offset
      iv              INTERVAL       — duration string
      uu              UUID           — RFC 4122
      ar              ARRAY          — JSON array literal
      js              JSON           — JSON object literal
      jb              JSONB          — JSONB object literal
      dc              DICT           — dict literal
      en              ENUM           — string from known set
      bt              BIT(4)         — 4-bit bit string
      vb              VARBIT(8)      — variable bit string
      in4             INET           — IPv4 address
      cidr            CIDR           — CIDR block
      mac             MACADDR        — MAC address
      rng             RANGE          — interval range literal
      bl              BOOLEAN        — true/false
    """
    idx = i % len(_UUIDS)
    day = 1 + (i % 28)
    mon = 1 + (i % 12)

    # Build JSON / JSONB / DICT as SQL string literals
    json_obj  = f'{{"id":{i},"region":"{_RNG.choice(_REGIONS)}","score":{_RNG.randint(1,1000)},"verified":{str(_RNG.random() > 0.5).lower()}}}'
    jsonb_obj = f'{{"uid":{i},"level":{_RNG.randint(1,10)},"active":{str(_RNG.random() > 0.3).lower()}}}'
    dict_obj  = f'{{"tier":"{_RNG.randint(1,5)}","status":"{_RNG.choice(_STATUSES)}","ref":"{_rand_str(4)}"}}'
    arr_val   = f'[{_RNG.randint(1,100)},{_RNG.randint(1,100)},{_RNG.randint(1,100)},{_RNG.randint(1,100)},{_RNG.randint(1,100)}]'
    ip_addr   = _RNG.choice(_IPV4_POOL)
    cidr_blk  = f"{'.'.join(ip_addr.split('.')[:3])}.0/24"
    mac_addr  = ":".join(f"{_RNG.randint(0,255):02x}" for _ in range(6))
    rng_lo    = _RNG.randint(1, 500)
    rng_hi    = rng_lo + _RNG.randint(1, 500)

    return (
        i,                                               # id        BIGINT
        _RNG.randint(-128, 127),                         # ti        TINYINT
        _RNG.randint(-32768, 32767),                     # si        SMALLINT
        _RNG.randint(-2_000_000, 2_000_000),             # ii        INTEGER
        _RNG.randint(-9_000_000_000, 9_000_000_000),     # bi        BIGINT
        round(_RNG.uniform(-1e6, 1e6), 4),               # rl        REAL
        round(_RNG.uniform(-1e12, 1e12), 8),             # db        DOUBLE
        round(_RNG.uniform(-99999.9999, 99999.9999), 4), # nm        NUMERIC(12,4)
        f"user_{_rand_str(6)}_{i}",                      # vc        VARCHAR
        f"Long text payload #{i} with extra data: {_rand_str(20)}", # tx TEXT
        _rand_str(5).upper()[:5],                        # ch        CHAR(5)
        b"binary_data",                                  # by        BYTEA
        f"2026-{mon:02d}-{day:02d}",                     # dt        DATE
        f"{_RNG.randint(0,23):02d}:{_RNG.randint(0,59):02d}:{_RNG.randint(0,59):02d}", # tm TIME
        f"2026-{mon:02d}-{day:02d}T{_RNG.randint(0,23):02d}:{_RNG.randint(0,59):02d}:00Z", # ts
        f"2026-{mon:02d}-{day:02d}T12:00:00+05:30",     # tstz      TIMESTAMPTZ
        "1 day 02:30:00",                                # iv        INTERVAL
        _UUIDS[idx],                                     # uu        UUID
        arr_val,                                         # ar        ARRAY
        json_obj,                                        # js        JSON
        jsonb_obj,                                       # jb        JSONB
        dict_obj,                                        # dc        DICT
        _RNG.choice(_STATUSES),                          # en        ENUM (VARCHAR)
        _RNG.choice(_BITS_4),                            # bt        BIT(4)
        _RNG.choice(_BITS_4) + _RNG.choice(_BITS_4),    # vb        VARBIT(8)
        ip_addr,                                         # in4       INET
        cidr_blk,                                        # cidr      CIDR
        mac_addr,                                        # mac       MACADDR
        f"[{rng_lo},{rng_hi})",                          # rng       RANGE
        _RNG.random() > 0.5,                             # bl        BOOLEAN
    )

# ═════════════════════════════════════════════════════════════════════════════
# DDL
# ═════════════════════════════════════════════════════════════════════════════
CREATE_TABLE = """
CREATE TABLE IF NOT EXISTS all_types (
    id    BIGINT         PRIMARY KEY,
    ti    TINYINT,
    si    SMALLINT,
    ii    INTEGER,
    bi    BIGINT,
    rl    REAL,
    db    DOUBLE,
    nm    NUMERIC(12,4),
    vc    VARCHAR        NOT NULL,
    tx    TEXT,
    ch    CHAR(5),
    by_   BYTEA,
    dt    DATE,
    tm    TIME,
    ts    TIMESTAMP,
    tstz  TIMESTAMPTZ,
    iv    INTERVAL,
    uu    UUID,
    ar    ARRAY,
    js    JSON,
    jb    JSONB,
    dc    DICT,
    en    VARCHAR,
    bt    BIT(4),
    vb    VARBIT(8),
    in4   INET,
    cidr  CIDR,
    mac   MACADDR,
    rng   RANGE,
    bl    BOOLEAN
);
"""

INSERT_SQL = """
INSERT INTO all_types
  (id,ti,si,ii,bi,rl,db,nm,vc,tx,ch,by_,dt,tm,ts,tstz,iv,uu,ar,js,jb,dc,en,bt,vb,in4,cidr,mac,rng,bl)
VALUES
  (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
"""

# ═════════════════════════════════════════════════════════════════════════════
# DB handles
# ═════════════════════════════════════════════════════════════════════════════
db: nemo.Database = None   # type: ignore[assignment]
conn = None

def open_db():
    global db, conn
    db   = nemo.connect(DB_NAME, buffer_pool_mb=BUFFER_POOL_MB)
    conn = db.connect_sync()

def close_db():
    if conn:
        conn.close()
    if db:
        db.close()

# ═════════════════════════════════════════════════════════════════════════════
# Helper: run a query and collect results
# ═════════════════════════════════════════════════════════════════════════════
def run(sql: str, label: str, expected_rows: int | None = None) -> List:
    with Timer() as t:
        try:
            res  = conn.execute(sql)
            rows = list(res)
            status = "OK"
        except Exception as exc:
            rows   = []
            status = f"ERR: {exc}"
    if expected_rows is not None and len(rows) != expected_rows and status == "OK":
        status = f"WARN: got {len(rows)} rows, expected {expected_rows}"
    record(label, t.elapsed, len(rows), status)
    if status.startswith("ERR"):
        print(f"  ✗  {label:<55} → {status}")
    else:
        print(f"  ✓  {label:<55} → {len(rows)} rows in {fmt(t.elapsed)}")
    return rows

def run_agg(sql: str, label: str) -> Any:
    with Timer() as t:
        try:
            res  = conn.execute(sql)
            rows = list(res)
            val  = rows[0][0] if rows else None
            status = "OK"
        except Exception as exc:
            val    = None
            status = f"ERR: {exc}"
    record(label, t.elapsed, 1, status)
    sym = "✓" if status == "OK" else "✗"
    print(f"  {sym}  {label:<55} → {val}  ({fmt(t.elapsed)})")
    return val


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 1: INSERT 20 M rows ═════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_insert():
    print(f"\n{SEP}")
    print(f"  PHASE 1 — Bulk INSERT  {TOTAL_ROWS:,} rows × 30 column types")
    print(f"  Buffer pool: {BUFFER_POOL_MB} MB  |  batch size: {BULK_BATCH_SIZE:,}")
    print(SEP)
    open_db()
    conn.execute(CREATE_TABLE)

    t0 = time.perf_counter()
    offset = 0
    while offset < TOTAL_ROWS:
        end   = min(offset + BULK_BATCH_SIZE, TOTAL_ROWS)
        batch = [_make_row(i) for i in range(offset + 1, end + 1)]
        conn.executemany(INSERT_SQL, batch)
        offset = end
        if offset % 2_000_000 == 0:
            elapsed = time.perf_counter() - t0
            rps = offset / elapsed if elapsed > 0 else 0
            print(f"    {offset:>12,} / {TOTAL_ROWS:,}  "
                  f"({offset/TOTAL_ROWS*100:.0f}%)  "
                  f"{rps/1_000:.0f} K rows/s  "
                  f"[{fmt(elapsed)}]")

    total_elapsed = time.perf_counter() - t0
    rps = TOTAL_ROWS / total_elapsed
    print(f"\n  → Inserted {TOTAL_ROWS:,} rows in {fmt(total_elapsed)}"
          f"  ({rps/1_000_000:.2f} M rows/s)")
    record("Bulk INSERT 20M rows (all 30 dtypes)", total_elapsed, TOTAL_ROWS)

    # Verify
    cnt_res = conn.execute("SELECT COUNT(*) FROM all_types;")
    cnt = int(str(list(cnt_res)[0][0]))
    print(f"  → Verified: {cnt:,} rows in table")
    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 2: JSON / JSONB Functions ══════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_json():
    print(f"\n{SEP}")
    print("  PHASE 2 — JSON / JSONB Functions  (20 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, json_extract(js, '$.region')           FROM all_types LIMIT {L};", "json_extract()")
    run(f"SELECT id, json_value(js, '$.score')              FROM all_types LIMIT {L};", "json_value()")
    run(f"SELECT id, json_get_text(js, '$.region')          FROM all_types LIMIT {L};", "json_get_text()")
    run(f"SELECT id, json_get_int(js, '$.score')            FROM all_types LIMIT {L};", "json_get_int()")
    run(f"SELECT id, json_get_float(js, '$.score')          FROM all_types LIMIT {L};", "json_get_float()")
    run(f"SELECT id, json_get_bool(js, '$.verified')        FROM all_types LIMIT {L};", "json_get_bool()")
    run(f"SELECT id, json_exists(js, '$.region')            FROM all_types LIMIT {L};", "json_exists()")
    run(f"SELECT id, json_keys(js)                          FROM all_types LIMIT {L};", "json_keys()")
    run(f"SELECT id, json_values(js)                        FROM all_types LIMIT {L};", "json_values()")
    run(f"SELECT id, json_length(js)                        FROM all_types LIMIT {L};", "json_length()")
    run(f"SELECT id, json_typeof(js)                        FROM all_types LIMIT {L};", "json_typeof()")
    run(f"SELECT id, json_valid(js)                         FROM all_types LIMIT {L};", "json_valid()")
    run(f"SELECT id, json_contains(js, 'North')             FROM all_types LIMIT {L};", "json_contains()")
    run(f"SELECT id, json_set(js, '$.tier', 99)             FROM all_types LIMIT {L};", "json_set()")
    run(f"SELECT id, json_insert(js, '$.extra', 'test')     FROM all_types LIMIT {L};", "json_insert()")
    run(f"SELECT id, json_remove(js, '$.score')             FROM all_types LIMIT {L};", "json_remove()")
    run(f"SELECT id, json_pretty(js)                        FROM all_types LIMIT {L};", "json_pretty()")
    run(f"SELECT id, jsonb_extract(jb, '$.uid')             FROM all_types LIMIT {L};", "jsonb_extract()")
    run(f"SELECT id, jsonb_contains(jb, 'true')             FROM all_types LIMIT {L};", "jsonb_contains()")
    run(f"SELECT id, jsonb_set(jb, '$.level', 99)           FROM all_types LIMIT {L};", "jsonb_set()")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 3: ARRAY Functions ═════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_array():
    print(f"\n{SEP}")
    print("  PHASE 3 — ARRAY Functions  (25 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, array_length(ar)                       FROM all_types LIMIT {L};", "array_length()")
    run(f"SELECT id, array_cardinality(ar)                  FROM all_types LIMIT {L};", "array_cardinality()")
    run(f"SELECT id, array_dims(ar)                         FROM all_types LIMIT {L};", "array_dims()")
    run(f"SELECT id, array_ndims(ar)                        FROM all_types LIMIT {L};", "array_ndims()")
    run(f"SELECT id, array_lower(ar)                        FROM all_types LIMIT {L};", "array_lower()")
    run(f"SELECT id, array_upper(ar)                        FROM all_types LIMIT {L};", "array_upper()")
    run(f"SELECT id, array_is_empty(ar)                     FROM all_types LIMIT {L};", "array_is_empty()")
    run(f"SELECT id, array_get(ar, 1)                       FROM all_types LIMIT {L};", "array_get(arr,1)")
    run(f"SELECT id, array_first(ar)                        FROM all_types LIMIT {L};", "array_first()")
    run(f"SELECT id, array_last(ar)                         FROM all_types LIMIT {L};", "array_last()")
    run(f"SELECT id, array_slice(ar, 2, 4)                  FROM all_types LIMIT {L};", "array_slice(2,4)")
    run(f"SELECT id, array_append(ar, 999)                  FROM all_types LIMIT {L};", "array_append()")
    run(f"SELECT id, array_prepend(0, ar)                   FROM all_types LIMIT {L};", "array_prepend()")
    run(f"SELECT id, array_concat(ar, ar)                   FROM all_types LIMIT {L};", "array_concat()")
    run(f"SELECT id, array_contains(ar, 42)                 FROM all_types LIMIT {L};", "array_contains()")
    run(f"SELECT id, array_distinct(ar)                     FROM all_types LIMIT {L};", "array_distinct()")
    run(f"SELECT id, array_sort(ar)                         FROM all_types LIMIT {L};", "array_sort()")
    run(f"SELECT id, array_reverse(ar)                      FROM all_types LIMIT {L};", "array_reverse()")
    run(f"SELECT id, array_sum(ar)                          FROM all_types LIMIT {L};", "array_sum()")
    run(f"SELECT id, array_avg(ar)                          FROM all_types LIMIT {L};", "array_avg()")
    run(f"SELECT id, array_min(ar)                          FROM all_types LIMIT {L};", "array_min()")
    run(f"SELECT id, array_max(ar)                          FROM all_types LIMIT {L};", "array_max()")
    run(f"SELECT id, array_to_string(ar, ',')               FROM all_types LIMIT {L};", "array_to_string()")
    run(f"SELECT id, array_to_json(ar)                      FROM all_types LIMIT {L};", "array_to_json()")
    run(f"SELECT id, string_to_array(vc, '_')               FROM all_types LIMIT {L};", "string_to_array()")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 4: DICT Functions ══════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_dict():
    print(f"\n{SEP}")
    print("  PHASE 4 — DICT Key-Value Functions  (13 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, dict_get(dc, 'tier')                   FROM all_types LIMIT {L};", "dict_get()")
    run(f"SELECT id, dict_get_or(dc, 'missing', 'N/A')      FROM all_types LIMIT {L};", "dict_get_or()")
    run(f"SELECT id, dict_has_key(dc, 'status')             FROM all_types LIMIT {L};", "dict_has_key()")
    run(f"SELECT id, dict_keys(dc)                          FROM all_types LIMIT {L};", "dict_keys()")
    run(f"SELECT id, dict_values(dc)                        FROM all_types LIMIT {L};", "dict_values()")
    run(f"SELECT id, dict_size(dc)                          FROM all_types LIMIT {L};", "dict_size()")
    run(f"SELECT id, dict_is_empty(dc)                      FROM all_types LIMIT {L};", "dict_is_empty()")
    run(f"SELECT id, dict_set(dc, 'extra', 'nemo')          FROM all_types LIMIT {L};", "dict_set()")
    run(f"SELECT id, dict_remove(dc, 'ref')                 FROM all_types LIMIT {L};", "dict_remove()")
    run(f"SELECT id, dict_to_json(dc)                       FROM all_types LIMIT {L};", "dict_to_json()")
    run(f"SELECT id, json_to_dict(js)                       FROM all_types LIMIT {L};", "json_to_dict()")
    # dict_merge — merge dc with itself
    run(f"SELECT id, dict_merge(dc, dc)                     FROM all_types LIMIT {L};", "dict_merge()")
    # json_to_array — convert JSON array in js (not a plain array) — use ar column
    run(f"SELECT id, json_to_array(array_to_json(ar))       FROM all_types LIMIT {L};", "json_to_array()")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 5: String Functions ════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_string():
    print(f"\n{SEP}")
    print("  PHASE 5 — String & Text Functions  (21 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, upper(vc)                              FROM all_types LIMIT {L};", "upper()")
    run(f"SELECT id, lower(vc)                              FROM all_types LIMIT {L};", "lower()")
    run(f"SELECT id, length(vc)                             FROM all_types LIMIT {L};", "length()")
    run(f"SELECT id, octet_length(vc)                       FROM all_types LIMIT {L};", "octet_length()")
    run(f"SELECT id, concat(vc, '_', en)                    FROM all_types LIMIT {L};", "concat()")
    run(f"SELECT id, concat_ws('-', vc, en, in4)            FROM all_types LIMIT {L};", "concat_ws()")
    run(f"SELECT id, substring(vc, 1, 8)                    FROM all_types LIMIT {L};", "substring()")
    run(f"SELECT id, substr(tx, 5, 10)                      FROM all_types LIMIT {L};", "substr()")
    run(f"SELECT id, trim(tx)                               FROM all_types LIMIT {L};", "trim()")
    run(f"SELECT id, ltrim(tx)                              FROM all_types LIMIT {L};", "ltrim()")
    run(f"SELECT id, rtrim(tx)                              FROM all_types LIMIT {L};", "rtrim()")
    run(f"SELECT id, replace(vc, 'user', 'acct')            FROM all_types LIMIT {L};", "replace()")
    run(f"SELECT id, position('_', vc)                      FROM all_types LIMIT {L};", "position()")
    run(f"SELECT id, split_part(vc, '_', 2)                 FROM all_types LIMIT {L};", "split_part()")
    run(f"SELECT id, starts_with(vc, 'user')                FROM all_types LIMIT {L};", "starts_with()")
    run(f"SELECT id, ends_with(vc, '1')                     FROM all_types LIMIT {L};", "ends_with()")
    run(f"SELECT id, contains(tx, 'extra')                  FROM all_types LIMIT {L};", "contains()")
    run(f"SELECT id, repeat('x', 5)                         FROM all_types LIMIT {L};", "repeat()")
    run(f"SELECT id, reverse(vc)                            FROM all_types LIMIT {L};", "reverse()")
    run(f"SELECT id, lpad(ch, 10, '0')                      FROM all_types LIMIT {L};", "lpad()")
    run(f"SELECT id, rpad(ch, 10, '.')                      FROM all_types LIMIT {L};", "rpad()")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 6: Math Functions ══════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_math():
    print(f"\n{SEP}")
    print("  PHASE 6 — Mathematical & Numeric Functions  (11 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, abs(db)                                FROM all_types LIMIT {L};", "abs()")
    run(f"SELECT id, round(db, 2)                           FROM all_types LIMIT {L};", "round()")
    run(f"SELECT id, floor(rl)                              FROM all_types LIMIT {L};", "floor()")
    run(f"SELECT id, ceil(rl)                               FROM all_types LIMIT {L};", "ceil()")
    run(f"SELECT id, trunc(db, 3)                           FROM all_types LIMIT {L};", "trunc()")
    run(f"SELECT id, mod(ii, 7)                             FROM all_types LIMIT {L};", "mod()")
    run(f"SELECT id, power(2, mod(ii, 10))                  FROM all_types LIMIT {L};", "power()")
    run(f"SELECT id, sqrt(abs(rl))                          FROM all_types LIMIT {L};", "sqrt(abs())")
    run(f"SELECT id, sign(db)                               FROM all_types LIMIT {L};", "sign()")
    run(f"SELECT id, greatest(ti, si, ii)                   FROM all_types LIMIT {L};", "greatest()")
    run(f"SELECT id, least(ti, si, ii)                      FROM all_types LIMIT {L};", "least()")

    # Full-table aggregates
    print()
    run_agg("SELECT SUM(db)   FROM all_types;", "SUM(db)  — full 20M scan")
    run_agg("SELECT AVG(nm)   FROM all_types;", "AVG(nm)  — full 20M scan")
    run_agg("SELECT MIN(ii)   FROM all_types;", "MIN(ii)  — full 20M scan")
    run_agg("SELECT MAX(bi)   FROM all_types;", "MAX(bi)  — full 20M scan")
    run_agg("SELECT COUNT(*)  FROM all_types;", "COUNT(*) — full 20M scan")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 7: Range Functions ═════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_range():
    print(f"\n{SEP}")
    print("  PHASE 7 — Range Boundary & Interval Functions  (4 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, range_lower(rng)                       FROM all_types LIMIT {L};", "range_lower()")
    run(f"SELECT id, range_upper(rng)                       FROM all_types LIMIT {L};", "range_upper()")
    run(f"SELECT id, range_contains(rng, 250)               FROM all_types LIMIT {L};", "range_contains()")
    run(f"SELECT id, range(1, 100)                          FROM all_types LIMIT {L};", "range(1,100)")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 8: Temporal Functions ══════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_temporal():
    print(f"\n{SEP}")
    print("  PHASE 8 — Temporal Functions  (6 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, current_date()                         FROM all_types LIMIT {L};", "current_date()")
    run(f"SELECT id, current_time()                         FROM all_types LIMIT {L};", "current_time()")
    run(f"SELECT id, now()                                  FROM all_types LIMIT {L};", "now()")
    run(f"SELECT id, current_timestamp()                    FROM all_types LIMIT {L};", "current_timestamp()")
    run(f"SELECT id, extract('year' FROM ts)                FROM all_types LIMIT {L};", "extract('year' FROM ts)")
    run(f"SELECT id, date_part('month', ts)                 FROM all_types LIMIT {L};", "date_part('month',ts)")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 9: Binary, Base64, UUID Functions ═══════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_binary():
    print(f"\n{SEP}")
    print("  PHASE 9 — Binary / Base64 / UUID Functions  (7 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, hex_encode(vc)                         FROM all_types LIMIT {L};", "hex_encode()")
    run(f"SELECT id, base64_encode(vc)                      FROM all_types LIMIT {L};", "base64_encode()")
    run(f"SELECT id, uuid_generate()                        FROM all_types LIMIT {L};", "uuid_generate()")
    run(f"SELECT id, uuid_to_string(uu)                     FROM all_types LIMIT {L};", "uuid_to_string()")
    # uuid_parse: round-trip the UUID column
    run(f"SELECT id, uuid_parse(uuid_to_string(uu))         FROM all_types LIMIT {L};", "uuid_parse()")
    # hex_decode round-trip
    run(f"SELECT id, hex_decode(hex_encode(vc))             FROM all_types LIMIT {L};", "hex_decode(hex_encode())")
    # base64_decode round-trip
    run(f"SELECT id, base64_decode(base64_encode(vc))       FROM all_types LIMIT {L};", "base64_decode(base64_encode())")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 10: Bitwise Functions ══════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_bitwise():
    print(f"\n{SEP}")
    print("  PHASE 10 — Bitwise & Bit-String Functions  (6 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, bit_length(bt)                         FROM all_types LIMIT {L};", "bit_length()")
    run(f"SELECT id, bit_count(bt)                          FROM all_types LIMIT {L};", "bit_count()")
    run(f"SELECT id, bit_and(bt, '1010')                    FROM all_types LIMIT {L};", "bit_and()")
    run(f"SELECT id, bit_or(bt, '0101')                     FROM all_types LIMIT {L};", "bit_or()")
    run(f"SELECT id, bit_xor(bt, '1111')                    FROM all_types LIMIT {L};", "bit_xor()")
    run(f"SELECT id, bit_not(bt)                            FROM all_types LIMIT {L};", "bit_not()")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 11: Network Functions ══════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_network():
    print(f"\n{SEP}")
    print("  PHASE 11 — Network (INET / CIDR) Functions  (4 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, host(in4)                              FROM all_types LIMIT {L};", "host(in4)")
    run(f"SELECT id, netmask(cidr)                          FROM all_types LIMIT {L};", "netmask(cidr)")
    run(f"SELECT id, is_ipv4(in4)                           FROM all_types LIMIT {L};", "is_ipv4()")
    run(f"SELECT id, is_ipv6(in4)                           FROM all_types LIMIT {L};", "is_ipv6()")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 12: Sequence Functions ═════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_sequence():
    print(f"\n{SEP}")
    print("  PHASE 12 — Sequence & Atomic Range Functions  (3 functions)")
    print(SEP)
    open_db()

    # Create a sequence first
    try:
        conn.execute("CREATE SEQUENCE bench_seq START WITH 1 INCREMENT BY 1;")
    except Exception:
        pass   # already exists

    with Timer() as t:
        try:
            res = conn.execute("SELECT nextval('bench_seq');")
            val = list(res)[0][0]
            status = "OK"
        except Exception as exc:
            val    = None
            status = f"ERR: {exc}"
    sym = "✓" if status == "OK" else "✗"
    print(f"  {sym}  {'nextval(bench_seq)':<55} → {val}  ({fmt(t.elapsed)})")
    record("nextval()", t.elapsed, 1, status)

    with Timer() as t:
        try:
            res = conn.execute("SELECT currval('bench_seq');")
            val = list(res)[0][0]
            status = "OK"
        except Exception as exc:
            val    = None
            status = f"ERR: {exc}"
    sym = "✓" if status == "OK" else "✗"
    print(f"  {sym}  {'currval(bench_seq)':<55} → {val}  ({fmt(t.elapsed)})")
    record("currval()", t.elapsed, 1, status)

    with Timer() as t:
        try:
            res = conn.execute("SELECT nextval_range('bench_seq', 1000);")
            val = list(res)[0][0]
            status = "OK"
        except Exception as exc:
            val    = None
            status = f"ERR: {exc}"
    sym = "✓" if status == "OK" else "✗"
    print(f"  {sym}  {'nextval_range(bench_seq, 1000)':<55} → {val}  ({fmt(t.elapsed)})")
    record("nextval_range()", t.elapsed, 1, status)

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ PHASE 13: Conditional & Type Utility Functions ═══════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def phase_conditional():
    print(f"\n{SEP}")
    print("  PHASE 13 — Conditional & Type Utility Functions  (7 functions)")
    print(SEP)
    open_db()
    L = FUNC_SAMPLE

    run(f"SELECT id, coalesce(NULL, vc, 'fallback')         FROM all_types LIMIT {L};", "coalesce()")
    run(f"SELECT id, ifnull(NULL, vc)                       FROM all_types LIMIT {L};", "ifnull()")
    run(f"SELECT id, nullif(ii, 0)                          FROM all_types LIMIT {L};", "nullif()")
    run(f"SELECT id, isnull(ti)                             FROM all_types LIMIT {L};", "isnull()")
    run(f"SELECT id, typeof(js)                             FROM all_types LIMIT {L};", "typeof(json)")
    run(f"SELECT id, cast(ii AS BIGINT)                     FROM all_types LIMIT {L};", "cast(ii AS BIGINT)")
    run(f"SELECT id, try_cast(vc AS INTEGER)                FROM all_types LIMIT {L};", "try_cast(vc AS INTEGER)")

    close_db()


# ═════════════════════════════════════════════════════════════════════════════
# ══ SUMMARY ══════════════════════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════════════
def print_summary():
    print(f"\n\n{SEP}")
    print("   FUNCTION BENCHMARK SUMMARY")
    print(f"   {TOTAL_ROWS:,} row dataset  |  all 30 column types  |  buffer_pool_mb={BUFFER_POOL_MB}")
    print(SEP)
    print(f"  {'Function / Operation':<55}  {'Time':>10}  {'Rows':>10}  Status")
    print(SEP2)

    total_ok  = 0
    total_err = 0
    total_t   = 0.0

    for label, elapsed, rows, status in results:
        sym = "✓" if status == "OK" else ("⚠" if status.startswith("WARN") else "✗")
        print(f"  {sym}  {label:<53}  {fmt(elapsed):>10}  {rows:>10,}  {status}")
        total_t += elapsed
        if status == "OK":
            total_ok += 1
        else:
            total_err += 1

    print(SEP2)
    total = len(results)
    print(f"\n  Total functions/operations tested : {total}")
    print(f"  Passed (OK)                       : {total_ok}")
    print(f"  Failed / warned                   : {total_err}")
    print(f"  Total wall-clock time             : {fmt(total_t)}")
    print(f"\n  Dataset      : {TOTAL_ROWS:,} rows × 30 columns  ({TOTAL_ROWS*30:,} values)")
    print(f"  Buffer pool  : {BUFFER_POOL_MB} MB")
    print(f"  Storage path : .nemo/{DB_NAME}/")
    print(SEP)


# ═════════════════════════════════════════════════════════════════════════════
# ── MAIN ──────────────────────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def main():
    print(SEP)
    print("   Nemo — 20 Million Row Datatype Coverage + 72+ SQL Function Benchmark")
    print(f"   buffer_pool_mb={BUFFER_POOL_MB}  |  TOTAL_ROWS={TOTAL_ROWS:,}  |  FUNC_SAMPLE={FUNC_SAMPLE:,}")
    print(SEP)

    # Clean slate
    shutil.rmtree(f".nemo/{DB_NAME}", ignore_errors=True)

    try:
        phase_insert()       # 1 — 20 M bulk insert, all 30 types
        phase_json()         # 2 — 20 JSON/JSONB functions
        phase_array()        # 3 — 25 ARRAY functions
        phase_dict()         # 4 — 13 DICT functions
        phase_string()       # 5 — 21 String functions
        phase_math()         # 6 — 11 Math + 5 aggregates
        phase_range()        # 7 —  4 Range functions
        phase_temporal()     # 8 —  6 Temporal functions
        phase_binary()       # 9 —  7 Binary/UUID functions
        phase_bitwise()      # 10 — 6 Bitwise functions
        phase_network()      # 11 — 4 Network functions
        phase_sequence()     # 12 — 3 Sequence functions
        phase_conditional()  # 13 — 7 Conditional functions

        print_summary()

    finally:
        shutil.rmtree(f".nemo/{DB_NAME}", ignore_errors=True)
        print("\n[Cleanup] Database removed.")


if __name__ == "__main__":
    main()

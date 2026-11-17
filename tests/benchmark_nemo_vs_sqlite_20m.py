"""
benchmark_nemo_vs_sqlite_20m.py
================================
Head-to-head benchmark: Nemo (buffer_pool_mb=512) vs SQLite (WAL mode)
at 20 million rows across all major operation categories.

Operations covered
------------------
  Bulk INSERT     — executemany in configurable batch sizes
  Single INSERT   — one row at a time (sample up to 100 k)
  SELECT scan     — full-table sequential scan
  Point lookup    — WHERE id = ? (indexed)
  Filtered query  — WHERE + ORDER BY + LIMIT
  UPDATE          — set a column value on N rows
  DELETE          — delete every other row (N/2 rows)
  FIFO POP        — Nemo-native pop() / SQLite emulated pop
  COUNT(*)        — O(1) for Nemo, O(N) for SQLite
  TRUNCATE        — Nemo truncate() vs SQLite DELETE FROM

Run:
    python tests/benchmark_nemo_vs_sqlite_20m.py

Output:
    Detailed per-operation timing table with rows/sec, speedup factor,
    and a final summary banner.
"""

from __future__ import annotations

import os
import sys
import time
import shutil
import sqlite3
import random
import string
import statistics
from typing import Callable, List, Tuple

# ── make sure the package is importable from the project root ─────────────────
_PACKAGE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if _PACKAGE not in sys.path:
    sys.path.insert(0, _PACKAGE)

import nemo

# ═════════════════════════════════════════════════════════════════════════════
# Configuration
# ═════════════════════════════════════════════════════════════════════════════
TOTAL_ROWS        = 20_000_000   # 20 million rows total
BULK_BATCH_SIZE   = 100_000      # rows per executemany call
SINGLE_ROW_SAMPLE = 100_000      # single-row inserts (subset; 20M one-by-one is too slow)
QUERY_LIMIT       = 1_000        # rows to fetch in filtered queries
DELETE_EVERY_N    = 10           # delete 1 in every N rows (2 M deletes)
POP_COUNT         = 500          # rows to pop in queue test
UPDATE_SAMPLE     = 1_000_000    # rows updated in UPDATE bench
BUFFER_POOL_MB    = 512          # Nemo buffer pool

NEMO_DB_NAME      = "bench_nemo_vs_sqlite_20m"
SQLITE_DB_PATH    = "bench_sqlite_20m.db"

SEP  = "=" * 90
SEP2 = "-" * 90

# ═════════════════════════════════════════════════════════════════════════════
# Data generation helpers
# ═════════════════════════════════════════════════════════════════════════════
_RNG = random.Random(42)

_REGIONS   = ["North", "South", "East", "West", "Central"]
_STATUSES  = ["active", "inactive", "pending", "suspended", "verified"]
_PRODUCTS  = ["Laptop", "Phone", "Tablet", "Monitor", "Keyboard", "Mouse", "SSD", "GPU"]

def _rand_str(n: int = 8) -> str:
    return "".join(_RNG.choices(string.ascii_lowercase, k=n))

def _make_row(i: int) -> Tuple:
    """Generate one benchmark row."""
    return (
        i,                                               # id
        f"user_{_rand_str(6)}_{i}",                     # username  VARCHAR
        f"user{i}@{_rand_str(5)}.com",                  # email     VARCHAR
        round(_RNG.uniform(0.0, 999_999.99), 2),        # balance   DOUBLE
        _RNG.randint(0, 1),                              # is_active BOOLEAN (0/1)
        _RNG.choice(_REGIONS),                           # region    VARCHAR
        _RNG.choice(_STATUSES),                          # status    VARCHAR
        _RNG.randint(1, 10_000),                         # score     INTEGER
        _RNG.choice(_PRODUCTS),                          # product   VARCHAR
        "2026-01-01T00:00:00Z",                          # created_at TIMESTAMP
    )

# ═════════════════════════════════════════════════════════════════════════════
# Timing utilities
# ═════════════════════════════════════════════════════════════════════════════
class Timer:
    def __init__(self):
        self._t = 0.0
    def __enter__(self):
        self._start = time.perf_counter()
        return self
    def __exit__(self, *_):
        self._t = time.perf_counter() - self._start
    @property
    def elapsed(self) -> float:
        return self._t

def rows_per_sec(n: int, elapsed: float) -> str:
    if elapsed <= 0:
        return "N/A"
    rps = n / elapsed
    if rps >= 1_000_000:
        return f"{rps/1_000_000:.2f} M/s"
    if rps >= 1_000:
        return f"{rps/1_000:.1f} K/s"
    return f"{rps:.1f} /s"

def fmt_time(elapsed: float) -> str:
    if elapsed >= 60:
        return f"{elapsed/60:.1f} min"
    return f"{elapsed:.3f} s"

# ═════════════════════════════════════════════════════════════════════════════
# Results collector
# ═════════════════════════════════════════════════════════════════════════════
class BenchResult:
    def __init__(self, label: str, nemo_t: float, sqlite_t: float, n: int):
        self.label    = label
        self.nemo_t   = nemo_t
        self.sqlite_t = sqlite_t
        self.n        = n

    @property
    def speedup(self) -> float:
        if self.sqlite_t <= 0 or self.nemo_t <= 0:
            return 0.0
        return self.sqlite_t / self.nemo_t

    def __str__(self) -> str:
        winner = "Nemo" if self.nemo_t < self.sqlite_t else "SQLite"
        sp = self.speedup
        sp_str = f"{sp:.2f}x" if sp > 0 else "N/A"
        return (
            f"  {self.label:<35} "
            f"| Nemo  {fmt_time(self.nemo_t):>10}  {rows_per_sec(self.n, self.nemo_t):>12} "
            f"| SQLite {fmt_time(self.sqlite_t):>10}  {rows_per_sec(self.n, self.sqlite_t):>12} "
            f"| {winner:>6} ×{sp_str}"
        )

results: List[BenchResult] = []

# ═════════════════════════════════════════════════════════════════════════════
# DDL helpers
# ═════════════════════════════════════════════════════════════════════════════
CREATE_NEMO = """
CREATE TABLE IF NOT EXISTS events (
    id         BIGINT PRIMARY KEY,
    username   VARCHAR NOT NULL,
    email      VARCHAR,
    balance    DOUBLE  DEFAULT 0.0,
    is_active  BOOLEAN DEFAULT true,
    region     VARCHAR,
    status     VARCHAR,
    score      INTEGER DEFAULT 0,
    product    VARCHAR,
    created_at TIMESTAMP
);
"""

CREATE_SQLITE = """
CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY,
    username   TEXT    NOT NULL,
    email      TEXT,
    balance    REAL    DEFAULT 0.0,
    is_active  INTEGER DEFAULT 1,
    region     TEXT,
    status     TEXT,
    score      INTEGER DEFAULT 0,
    product    TEXT,
    created_at TEXT
);
"""

INSERT_SQL = """
INSERT INTO events (id,username,email,balance,is_active,region,status,score,product,created_at)
VALUES (?,?,?,?,?,?,?,?,?,?);
"""

# ═════════════════════════════════════════════════════════════════════════════
# Setup / teardown
# ═════════════════════════════════════════════════════════════════════════════
def cleanup():
    shutil.rmtree(f".nemo/{NEMO_DB_NAME}", ignore_errors=True)
    if os.path.exists(SQLITE_DB_PATH):
        try:
            os.remove(SQLITE_DB_PATH)
        except OSError:
            pass

def open_nemo():
    db = nemo.connect(NEMO_DB_NAME, buffer_pool_mb=BUFFER_POOL_MB)
    conn = db.connect_sync()
    return db, conn

def open_sqlite():
    con = sqlite3.connect(SQLITE_DB_PATH)
    con.execute("PRAGMA journal_mode = WAL;")
    con.execute("PRAGMA synchronous  = NORMAL;")
    con.execute("PRAGMA cache_size   = -131072;")   # 128 MB SQLite page cache
    con.execute("PRAGMA temp_store   = MEMORY;")
    con.execute("PRAGMA mmap_size    = 536870912;") # 512 MB mmap
    return con

# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 1: Bulk INSERT (executemany) ──────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_bulk_insert():
    print(f"\n{'━'*90}")
    print(f"  [1] BULK INSERT — {TOTAL_ROWS:,} rows via executemany (batch={BULK_BATCH_SIZE:,})")
    print(f"{'━'*90}")

    # ── Nemo ──
    cleanup()
    db, conn = open_nemo()
    conn.execute(CREATE_NEMO)

    with Timer() as t:
        offset = 0
        while offset < TOTAL_ROWS:
            end    = min(offset + BULK_BATCH_SIZE, TOTAL_ROWS)
            batch  = [_make_row(i) for i in range(offset + 1, end + 1)]
            conn.executemany(INSERT_SQL, batch)
            offset = end
            if offset % 2_000_000 == 0:
                pct = offset / TOTAL_ROWS * 100
                print(f"    Nemo   {offset:>12,} rows  ({pct:.0f}%)  "
                      f"[elapsed: {time.perf_counter():.1f}s]")

    nemo_t = t.elapsed
    print(f"  → Nemo  bulk insert done in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(TOTAL_ROWS, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    con.execute(CREATE_SQLITE)

    with Timer() as t:
        offset = 0
        while offset < TOTAL_ROWS:
            end   = min(offset + BULK_BATCH_SIZE, TOTAL_ROWS)
            batch = [_make_row(i) for i in range(offset + 1, end + 1)]
            con.executemany(INSERT_SQL, batch)
            con.commit()
            offset = end
            if offset % 2_000_000 == 0:
                pct = offset / TOTAL_ROWS * 100
                print(f"    SQLite {offset:>12,} rows  ({pct:.0f}%)")

    sqlite_t = t.elapsed
    print(f"  → SQLite bulk insert done in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(TOTAL_ROWS, sqlite_t)})")
    con.close()

    r = BenchResult("Bulk INSERT (executemany)", nemo_t, sqlite_t, TOTAL_ROWS)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 2: Single-row INSERT ──────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_single_insert():
    N = SINGLE_ROW_SAMPLE
    print(f"\n{'━'*90}")
    print(f"  [2] SINGLE-ROW INSERT — {N:,} rows one at a time")
    print(f"{'━'*90}")

    # Re-use existing data; create a separate table so IDs don't collide
    CREATE_SINGLE_N = CREATE_NEMO.replace("events", "singles")
    CREATE_SINGLE_S = CREATE_SQLITE.replace("events", "singles")
    INSERT_SINGLE   = INSERT_SQL.replace("events", "singles")
    batch           = [_make_row(i) for i in range(TOTAL_ROWS + 1, TOTAL_ROWS + N + 1)]

    # ── Nemo ──
    db, conn = open_nemo()
    conn.execute(CREATE_SINGLE_N)
    with Timer() as t:
        for row in batch:
            conn.execute(INSERT_SINGLE, row)
    nemo_t = t.elapsed
    print(f"  → Nemo  single insert {N:,} rows in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(N, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    con.execute(CREATE_SINGLE_S)
    with Timer() as t:
        for row in batch:
            con.execute(INSERT_SINGLE, row)
        con.commit()
    sqlite_t = t.elapsed
    print(f"  → SQLite single insert {N:,} rows in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(N, sqlite_t)})")
    con.close()

    r = BenchResult(f"Single-row INSERT ({N//1000}K)", nemo_t, sqlite_t, N)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 3: Full-table sequential scan ──────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_full_scan():
    print(f"\n{'━'*90}")
    print(f"  [3] FULL SEQUENTIAL SCAN — COUNT(*) + iterate all {TOTAL_ROWS:,} rows")
    print(f"{'━'*90}")

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        res = conn.execute("SELECT COUNT(*) FROM events;")
        cnt = int(str(list(res)[0][0]))
    nemo_t = t.elapsed
    print(f"  → Nemo  COUNT(*)={cnt:,}  in {fmt_time(nemo_t)}  ({rows_per_sec(cnt, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        cur = con.execute("SELECT COUNT(*) FROM events;")
        cnt = cur.fetchone()[0]
    sqlite_t = t.elapsed
    print(f"  → SQLite COUNT(*)={cnt:,}  in {fmt_time(sqlite_t)}  ({rows_per_sec(cnt, sqlite_t)})")
    con.close()

    r = BenchResult("Full scan COUNT(*)", nemo_t, sqlite_t, TOTAL_ROWS)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 4: Point lookup by primary key ─────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_point_lookup():
    LOOKUPS = 10_000
    ids     = [_RNG.randint(1, TOTAL_ROWS) for _ in range(LOOKUPS)]
    print(f"\n{'━'*90}")
    print(f"  [4] POINT LOOKUP — {LOOKUPS:,} random WHERE id=? queries")
    print(f"{'━'*90}")

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        for rid in ids:
            conn.execute(f"SELECT * FROM events WHERE id = {rid};").fetchone()
    nemo_t = t.elapsed
    print(f"  → Nemo  {LOOKUPS:,} point lookups in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(LOOKUPS, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        for rid in ids:
            con.execute("SELECT * FROM events WHERE id=?", (rid,)).fetchone()
    sqlite_t = t.elapsed
    print(f"  → SQLite {LOOKUPS:,} point lookups in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(LOOKUPS, sqlite_t)})")
    con.close()

    r = BenchResult(f"Point lookup ({LOOKUPS//1000}K queries)", nemo_t, sqlite_t, LOOKUPS)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 5: Filtered query with ORDER BY + LIMIT ────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_filtered_query():
    REPS = 500
    regions = _RNG.choices(_REGIONS, k=REPS)
    print(f"\n{'━'*90}")
    print(f"  [5] FILTERED QUERY — {REPS} × (WHERE region=? ORDER BY score DESC LIMIT {QUERY_LIMIT})")
    print(f"{'━'*90}")

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        for region in regions:
            res = conn.execute(
                f"SELECT id, username, score FROM events "
                f"WHERE region = '{region}' ORDER BY score DESC LIMIT {QUERY_LIMIT};"
            )
            list(res)
    nemo_t = t.elapsed
    print(f"  → Nemo  {REPS} filtered queries in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(REPS * QUERY_LIMIT, nemo_t)} rows/s)")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        for region in regions:
            cur = con.execute(
                f"SELECT id, username, score FROM events "
                f"WHERE region=? ORDER BY score DESC LIMIT {QUERY_LIMIT}",
                (region,)
            )
            cur.fetchall()
    sqlite_t = t.elapsed
    print(f"  → SQLite {REPS} filtered queries in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(REPS * QUERY_LIMIT, sqlite_t)} rows/s)")
    con.close()

    r = BenchResult(f"Filtered + ORDER BY + LIMIT", nemo_t, sqlite_t, REPS * QUERY_LIMIT)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 6: SIMD Aggregates ─────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_aggregates():
    print(f"\n{'━'*90}")
    print(f"  [6] SIMD AGGREGATES — SUM / AVG / MIN / MAX / COUNT on {TOTAL_ROWS:,} rows")
    print(f"{'━'*90}")

    AGG_SQL_N = "SELECT SUM(balance), AVG(balance), MIN(score), MAX(score), COUNT(*) FROM events;"
    AGG_SQL_S = AGG_SQL_N

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        res = conn.execute(AGG_SQL_N)
        row = list(res)[0]
    nemo_t = t.elapsed
    print(f"  → Nemo  SUM={float(str(row[0])):,.2f}  AVG={float(str(row[1])):,.4f}  "
          f"MIN={row[2]}  MAX={row[3]}  COUNT={row[4]}")
    print(f"  → Nemo  in {fmt_time(nemo_t)}  ({rows_per_sec(TOTAL_ROWS, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        cur = con.execute(AGG_SQL_S)
        row = cur.fetchone()
    sqlite_t = t.elapsed
    print(f"  → SQLite SUM={row[0]:,.2f}  AVG={row[1]:,.4f}  "
          f"MIN={row[2]}  MAX={row[3]}  COUNT={row[4]}")
    print(f"  → SQLite in {fmt_time(sqlite_t)}  ({rows_per_sec(TOTAL_ROWS, sqlite_t)})")
    con.close()

    r = BenchResult("SIMD SUM/AVG/MIN/MAX/COUNT", nemo_t, sqlite_t, TOTAL_ROWS)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 7: GROUP BY aggregate ──────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_group_by():
    print(f"\n{'━'*90}")
    print(f"  [7] GROUP BY — region, status (5 × 5 = 25 groups) over {TOTAL_ROWS:,} rows")
    print(f"{'━'*90}")

    GROUP_SQL = (
        "SELECT region, status, COUNT(*), SUM(balance), AVG(score) "
        "FROM events GROUP BY region, status ORDER BY region, status;"
    )

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        res = conn.execute(GROUP_SQL)
        rows = list(res)
    nemo_t = t.elapsed
    print(f"  → Nemo  {len(rows)} groups in {fmt_time(nemo_t)}  ({rows_per_sec(TOTAL_ROWS, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        cur = con.execute(GROUP_SQL)
        rows = cur.fetchall()
    sqlite_t = t.elapsed
    print(f"  → SQLite {len(rows)} groups in {fmt_time(sqlite_t)}  ({rows_per_sec(TOTAL_ROWS, sqlite_t)})")
    con.close()

    r = BenchResult("GROUP BY + SUM/AVG", nemo_t, sqlite_t, TOTAL_ROWS)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 8: UPDATE ───────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_update():
    N = UPDATE_SAMPLE
    print(f"\n{'━'*90}")
    print(f"  [8] UPDATE — {N:,} rows WHERE score < 5000 SET balance += 100")
    print(f"{'━'*90}")

    UPDATE_SQL = "UPDATE events SET balance = balance + 100 WHERE score < 5000;"

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        conn.execute(UPDATE_SQL)
    nemo_t = t.elapsed
    print(f"  → Nemo  update in {fmt_time(nemo_t)}  "
          f"(~{rows_per_sec(N, nemo_t)} rows/s)")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        con.execute(UPDATE_SQL)
        con.commit()
    sqlite_t = t.elapsed
    print(f"  → SQLite update in {fmt_time(sqlite_t)}  "
          f"(~{rows_per_sec(N, sqlite_t)} rows/s)")
    con.close()

    r = BenchResult(f"UPDATE ~{N//1_000_000}M rows", nemo_t, sqlite_t, N)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 9: DELETE ───────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_delete():
    DEL_N = TOTAL_ROWS // DELETE_EVERY_N   # 2 M rows
    print(f"\n{'━'*90}")
    print(f"  [9] DELETE — every {DELETE_EVERY_N}th row  (~{DEL_N:,} rows)")
    print(f"{'━'*90}")

    DELETE_SQL = f"DELETE FROM events WHERE id % {DELETE_EVERY_N} = 0;"

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        conn.execute(DELETE_SQL)
    nemo_t = t.elapsed
    print(f"  → Nemo  delete ~{DEL_N:,} rows in {fmt_time(nemo_t)}  ({rows_per_sec(DEL_N, nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        con.execute(DELETE_SQL)
        con.commit()
    sqlite_t = t.elapsed
    print(f"  → SQLite delete ~{DEL_N:,} rows in {fmt_time(sqlite_t)}  ({rows_per_sec(DEL_N, sqlite_t)})")
    con.close()

    r = BenchResult(f"DELETE ~{DEL_N//1_000_000}M rows", nemo_t, sqlite_t, DEL_N)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 10: FIFO POP ────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_fifo_pop():
    N = POP_COUNT
    print(f"\n{'━'*90}")
    print(f"  [10] FIFO POP — {N} atomic pops from head")
    print(f"{'━'*90}")

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        popped = conn.pop("events", count=N, direction="fifo")
    nemo_t = t.elapsed
    print(f"  → Nemo  popped {len(popped)} rows in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(len(popped), nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite — emulated pop (SELECT + DELETE in a loop) ──
    con = open_sqlite()
    popped_sqlite = 0
    with Timer() as t:
        for _ in range(N):
            cur = con.execute(
                "SELECT id FROM events ORDER BY id ASC LIMIT 1;"
            )
            row = cur.fetchone()
            if row:
                con.execute("DELETE FROM events WHERE id=?", (row[0],))
                popped_sqlite += 1
        con.commit()
    sqlite_t = t.elapsed
    print(f"  → SQLite emulated pop {popped_sqlite} rows in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(popped_sqlite, sqlite_t)})")
    con.close()

    r = BenchResult(f"FIFO POP ({N} rows)", nemo_t, sqlite_t, N)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 11: LIFO POP ────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_lifo_pop():
    N = POP_COUNT
    print(f"\n{'━'*90}")
    print(f"  [11] LIFO POP — {N} atomic pops from tail")
    print(f"{'━'*90}")

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        popped = conn.pop("events", count=N, direction="lifo")
    nemo_t = t.elapsed
    print(f"  → Nemo  popped {len(popped)} rows in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(len(popped), nemo_t)})")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    popped_sqlite = 0
    with Timer() as t:
        for _ in range(N):
            cur = con.execute(
                "SELECT id FROM events ORDER BY id DESC LIMIT 1;"
            )
            row = cur.fetchone()
            if row:
                con.execute("DELETE FROM events WHERE id=?", (row[0],))
                popped_sqlite += 1
        con.commit()
    sqlite_t = t.elapsed
    print(f"  → SQLite emulated pop {popped_sqlite} rows in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(popped_sqlite, sqlite_t)})")
    con.close()

    r = BenchResult(f"LIFO POP ({N} rows)", nemo_t, sqlite_t, N)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 12: Instant COUNT(*) ───────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_instant_count():
    REPS = 1_000
    print(f"\n{'━'*90}")
    print(f"  [12] INSTANT COUNT(*) — {REPS} consecutive COUNT(*) calls")
    print(f"{'━'*90}")

    # ── Nemo ──
    db, conn = open_nemo()
    with Timer() as t:
        for _ in range(REPS):
            conn.count("events")
    nemo_t = t.elapsed
    print(f"  → Nemo  {REPS} conn.count() calls in {fmt_time(nemo_t)}  "
          f"({rows_per_sec(REPS, nemo_t)} calls/s)")
    conn.close()
    db.close()

    # ── SQLite ──
    con = open_sqlite()
    with Timer() as t:
        for _ in range(REPS):
            con.execute("SELECT COUNT(*) FROM events;").fetchone()
    sqlite_t = t.elapsed
    print(f"  → SQLite {REPS} COUNT(*) calls in {fmt_time(sqlite_t)}  "
          f"({rows_per_sec(REPS, sqlite_t)} calls/s)")
    con.close()

    r = BenchResult(f"COUNT(*) × {REPS}", nemo_t, sqlite_t, REPS)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 13: TRUNCATE / DELETE FROM ─────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_truncate():
    print(f"\n{'━'*90}")
    print(f"  [13] TRUNCATE — instant O(1) wipe vs SQLite DELETE FROM")
    print(f"{'━'*90}")

    # Count remaining rows
    db, conn = open_nemo()
    remaining_n = int(str(list(conn.execute("SELECT COUNT(*) FROM events;"))[0][0]))
    with Timer() as t:
        conn.truncate("events")
    nemo_t = t.elapsed
    after = int(str(list(conn.execute("SELECT COUNT(*) FROM events;"))[0][0]))
    print(f"  → Nemo  truncated {remaining_n:,} rows → {after}  in {fmt_time(nemo_t)}")
    conn.close()
    db.close()

    con = open_sqlite()
    cur = con.execute("SELECT COUNT(*) FROM events;")
    sqlite_remaining = cur.fetchone()[0]
    with Timer() as t:
        con.execute("DELETE FROM events;")
        con.commit()
    sqlite_t = t.elapsed
    print(f"  → SQLite DELETE FROM {sqlite_remaining:,} rows in {fmt_time(sqlite_t)}")
    con.close()

    r = BenchResult(f"TRUNCATE / DELETE FROM", nemo_t, sqlite_t, max(remaining_n, sqlite_remaining))
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── BENCHMARK 14: Mixed concurrent workload (DISTINCT + WINDOW) ───────────────
# ═════════════════════════════════════════════════════════════════════════════
def bench_window_distinct():
    # Re-populate after truncate
    print(f"\n{'━'*90}")
    print(f"  [14] WINDOW + DISTINCT — re-populating 1M rows then ROW_NUMBER() + DISTINCT")
    print(f"{'━'*90}")
    REPOP = 1_000_000
    # Nemo
    db, conn = open_nemo()
    conn.execute(CREATE_NEMO)
    batch = [_make_row(i) for i in range(1, REPOP + 1)]
    conn.executemany(INSERT_SQL, batch)

    with Timer() as t:
        res = conn.execute(
            "SELECT id, username, region, ROW_NUMBER() OVER (ORDER BY score DESC) rn "
            "FROM events LIMIT 500;"
        )
        list(res)
    nemo_window = t.elapsed

    with Timer() as t:
        res = conn.execute(
            "SELECT DISTINCT region, status FROM events ORDER BY region, status;"
        )
        list(res)
    nemo_distinct = t.elapsed
    nemo_t = nemo_window + nemo_distinct
    print(f"  → Nemo  window {fmt_time(nemo_window)} | distinct {fmt_time(nemo_distinct)}")
    conn.close()
    db.close()

    # SQLite
    con = open_sqlite()
    con.execute(CREATE_SQLITE)
    con.executemany(INSERT_SQL, batch)
    con.commit()

    with Timer() as t:
        cur = con.execute(
            "SELECT id, username, region, ROW_NUMBER() OVER (ORDER BY score DESC) rn "
            "FROM events LIMIT 500;"
        )
        cur.fetchall()
    sqlite_window = t.elapsed

    with Timer() as t:
        cur = con.execute(
            "SELECT DISTINCT region, status FROM events ORDER BY region, status;"
        )
        cur.fetchall()
    sqlite_distinct = t.elapsed
    sqlite_t = sqlite_window + sqlite_distinct
    print(f"  → SQLite window {fmt_time(sqlite_window)} | distinct {fmt_time(sqlite_distinct)}")
    con.close()

    r = BenchResult("WINDOW + DISTINCT (1M rows)", nemo_t, sqlite_t, REPOP)
    results.append(r)
    print(f"\n{r}")


# ═════════════════════════════════════════════════════════════════════════════
# ── FINAL SUMMARY TABLE ───────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def print_summary():
    print(f"\n\n{SEP}")
    print("   BENCHMARK SUMMARY  —  Nemo (512 MB buffer pool) vs SQLite (WAL, 128 MB cache)")
    print(f"   {TOTAL_ROWS:,} row dataset  |  machine-generated uniform random data")
    print(SEP)
    hdr = (
        f"  {'Operation':<35} "
        f"| {'Nemo time':>10}  {'Nemo rps':>12} "
        f"| {'SQLite time':>11}  {'SQLite rps':>12} "
        f"| Winner"
    )
    print(hdr)
    print(SEP2)

    nemo_wins = 0
    sqlite_wins = 0
    speedups = []

    for r in results:
        print(r)
        if r.nemo_t < r.sqlite_t:
            nemo_wins += 1
        else:
            sqlite_wins += 1
        if r.speedup > 0:
            speedups.append(r.speedup)

    print(SEP2)
    if speedups:
        geo_mean = statistics.geometric_mean(speedups)
        print(f"\n  Nemo wins:   {nemo_wins}/{len(results)} benchmarks")
        print(f"  SQLite wins: {sqlite_wins}/{len(results)} benchmarks")
        print(f"  Geometric mean speedup (Nemo / SQLite): {geo_mean:.2f}x")
    print(SEP)

    print("\n  Buffer pool: 512 MB   |   SQLite cache: 128 MB mmap + 128 MB page cache")
    print("  Dataset:     20 M rows of 10 columns (VARCHAR, DOUBLE, BOOLEAN, INTEGER, TIMESTAMP)")
    print("  Nemo storage layout:  .nemo/bench_nemo_vs_sqlite_20m/")
    print("  SQLite file:          bench_sqlite_20m.db")


# ═════════════════════════════════════════════════════════════════════════════
# ── MAIN ──────────────────────────────────────────────────────────────────────
# ═════════════════════════════════════════════════════════════════════════════
def main():
    print(SEP)
    print("   NEMO vs SQLite — 20 Million Row Comprehensive Benchmark")
    print(f"   Nemo buffer_pool_mb={BUFFER_POOL_MB}  |  Total rows: {TOTAL_ROWS:,}")
    print(SEP)

    cleanup()

    try:
        bench_bulk_insert()       # 1 — 20 M bulk
        bench_single_insert()     # 2 — 100 K one-by-one
        bench_full_scan()         # 3 — COUNT(*) scan
        bench_point_lookup()      # 4 — 10 K random id lookups
        bench_filtered_query()    # 5 — WHERE + ORDER BY + LIMIT
        bench_aggregates()        # 6 — SIMD SUM/AVG/MIN/MAX
        bench_group_by()          # 7 — GROUP BY 25 groups
        bench_update()            # 8 — UPDATE ~10 M rows
        bench_delete()            # 9 — DELETE ~2 M rows
        bench_fifo_pop()          # 10 — FIFO atomic pop
        bench_lifo_pop()          # 11 — LIFO atomic pop
        bench_instant_count()     # 12 — repeated COUNT(*)
        bench_truncate()          # 13 — TRUNCATE vs DELETE FROM
        bench_window_distinct()   # 14 — WINDOW + DISTINCT (1 M re-pop)

        print_summary()

    finally:
        cleanup()
        if os.path.exists(SQLITE_DB_PATH):
            try:
                os.remove(SQLITE_DB_PATH)
            except OSError:
                pass
        print("\n[Cleanup] Temporary databases removed.")


if __name__ == "__main__":
    main()

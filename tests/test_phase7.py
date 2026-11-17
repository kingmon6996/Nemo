"""Phase 7 — SIMD aggregate validation.

Final regression suite for the completed aggregate phase. It verifies that
SUM, AVG, MIN, MAX, and COUNT return numerically correct results regardless of
whether the SIMD path (AVX2/SSE4.1) or the scalar fallback is active.

Pattern: db = nemo.connect(name), conn = db.connect()
"""

import uuid
import shutil
import os
import nemo


def _fresh(prefix: str = "p7") -> str:
    return f"{prefix}_{uuid.uuid4().hex[:8]}"


def _cleanup(name: str) -> None:
    path = os.path.join(".nemo", name)
    if os.path.exists(path):
        shutil.rmtree(path, ignore_errors=True)


# ---------------------------------------------------------------------------
# Test 1 — SUM of integers
# ---------------------------------------------------------------------------
def test_sum_integers():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE nums (id INTEGER, val INTEGER)")
        for i in range(1, 101):
            conn.execute(f"INSERT INTO nums VALUES ({i}, {i})")
        res = conn.execute("SELECT SUM(val) FROM nums")
        rows = list(res)
        assert len(rows) == 1
        got = float(str(rows[0][0]))
        assert abs(got - 5050.0) < 1e-6, f"Expected SUM=5050, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 2 — AVG of integers
# ---------------------------------------------------------------------------
def test_avg_integers():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE vals (id INTEGER, x INTEGER)")
        for i in range(1, 11):
            conn.execute(f"INSERT INTO vals VALUES ({i}, {i * 10})")
        res = conn.execute("SELECT AVG(x) FROM vals")
        rows = list(res)
        assert len(rows) == 1
        got = float(str(rows[0][0]))
        assert abs(got - 55.0) < 1e-6, f"Expected AVG=55, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 3 — MIN value
# ---------------------------------------------------------------------------
def test_min_value():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE measures (id INTEGER, reading REAL)")
        values = [3.14, 2.71, 1.41, 1.73, 2.23]
        for i, v in enumerate(values, 1):
            conn.execute(f"INSERT INTO measures VALUES ({i}, {v})")
        res = conn.execute("SELECT MIN(reading) FROM measures")
        rows = list(res)
        assert len(rows) == 1
        got = float(str(rows[0][0]))
        assert abs(got - 1.41) < 1e-5, f"Expected MIN=1.41, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 4 — MAX value
# ---------------------------------------------------------------------------
def test_max_value():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE temps (id INTEGER, temp REAL)")
        for i, t in enumerate([10.5, 22.3, 18.0, 35.7, 12.1], 1):
            conn.execute(f"INSERT INTO temps VALUES ({i}, {t})")
        res = conn.execute("SELECT MAX(temp) FROM temps")
        rows = list(res)
        assert len(rows) == 1
        got = float(str(rows[0][0]))
        assert abs(got - 35.7) < 1e-5, f"Expected MAX=35.7, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 5 — SUM of floats (precision check)
# ---------------------------------------------------------------------------
def test_sum_floats():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE prices (id INTEGER, amount REAL)")
        total = 0.0
        for i in range(1, 21):
            v = round(i * 1.5, 2)
            total += v
            conn.execute(f"INSERT INTO prices VALUES ({i}, {v})")
        res = conn.execute("SELECT SUM(amount) FROM prices")
        rows = list(res)
        got = float(str(rows[0][0]))
        assert abs(got - total) < 0.01, f"Expected SUM~{total}, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 6 — AVG of floats
# ---------------------------------------------------------------------------
def test_avg_floats():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE scores2 (id INTEGER, score REAL)")
        vals = [1.0, 2.0, 3.0, 4.0, 5.0]
        for i, v in enumerate(vals, 1):
            conn.execute(f"INSERT INTO scores2 VALUES ({i}, {v})")
        res = conn.execute("SELECT AVG(score) FROM scores2")
        rows = list(res)
        got = float(str(rows[0][0]))
        assert abs(got - 3.0) < 1e-6, f"Expected AVG=3.0, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 7 — Large dataset SUM (exercises SIMD vectorization)
# ---------------------------------------------------------------------------
def test_large_sum_simd():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE bignum (id INTEGER, v INTEGER)")
        N = 1000
        expected = N * (N + 1) // 2
        rows_batch = [(i, i) for i in range(1, N + 1)]
        conn.begin()
        conn.executemany("INSERT INTO bignum VALUES (?, ?)", rows_batch)
        conn.commit()
        res = conn.execute("SELECT SUM(v) FROM bignum")
        rows = list(res)
        got = float(str(rows[0][0]))
        assert abs(got - expected) < 1.0, f"Expected SUM={expected}, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 8 — SUM and AVG in one query
# ---------------------------------------------------------------------------
def test_sum_and_avg_combined():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE data (id INTEGER, x REAL)")
        for i in range(1, 6):
            conn.execute(f"INSERT INTO data VALUES ({i}, {float(i)})")
        # SUM(1..5) = 15, AVG = 3
        res_sum = conn.execute("SELECT SUM(x) FROM data")
        res_avg = conn.execute("SELECT AVG(x) FROM data")
        sum_val = float(str(list(res_sum)[0][0]))
        avg_val = float(str(list(res_avg)[0][0]))
        assert abs(sum_val - 15.0) < 1e-6, f"SUM wrong: {sum_val}"
        assert abs(avg_val - 3.0) < 1e-6, f"AVG wrong: {avg_val}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 9 — Aggregate on negative values
# ---------------------------------------------------------------------------
def test_aggregate_negative_values():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE negs (id INTEGER, n INTEGER)")
        vals = [-5, -3, -1, 0, 2, 4]
        for i, v in enumerate(vals, 1):
            conn.execute(f"INSERT INTO negs VALUES ({i}, {v})")
        res_sum = conn.execute("SELECT SUM(n) FROM negs")
        res_min = conn.execute("SELECT MIN(n) FROM negs")
        res_max = conn.execute("SELECT MAX(n) FROM negs")
        sum_val = float(str(list(res_sum)[0][0]))
        min_val = float(str(list(res_min)[0][0]))
        max_val = float(str(list(res_max)[0][0]))
        assert abs(sum_val - (-3.0)) < 1e-6, f"SUM wrong: {sum_val}"
        assert abs(min_val - (-5.0)) < 1e-6, f"MIN wrong: {min_val}"
        assert abs(max_val - 4.0) < 1e-6, f"MAX wrong: {max_val}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


# ---------------------------------------------------------------------------
# Test 10 — COUNT(*) baseline (not SIMD but confirms agg dispatch works)
# ---------------------------------------------------------------------------
def test_count_star():
    name = _fresh()
    try:
        db = nemo.connect(name)
        conn = db.connect()
        conn.execute("CREATE TABLE things (id INTEGER, label TEXT)")
        for i in range(1, 26):
            conn.execute(f"INSERT INTO things VALUES ({i}, 'item_{i}')")
        res = conn.execute("SELECT COUNT(*) FROM things")
        rows = list(res)
        got = int(str(rows[0][0]))
        assert got == 25, f"Expected COUNT=25, got {got}"
        conn.close()
        db.close()
    finally:
        _cleanup(name)


if __name__ == "__main__":
    tests = [
        test_sum_integers,
        test_avg_integers,
        test_min_value,
        test_max_value,
        test_sum_floats,
        test_avg_floats,
        test_large_sum_simd,
        test_sum_and_avg_combined,
        test_aggregate_negative_values,
        test_count_star,
    ]
    passed = 0
    for t in tests:
        try:
            t()
            print(f"  PASS  {t.__name__}")
            passed += 1
        except Exception as exc:
            print(f"  FAIL  {t.__name__}: {exc}")
    print(f"\n{passed}/{len(tests)} tests passed")


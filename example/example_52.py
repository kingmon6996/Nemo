# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Nemo Example 52: Branchless Expressions & Predicates
=============================================================================
Demonstrates:
  1. Searched & Simple CASE WHEN expressions (vectorized blend selection).
  2. SIMD and hash-accelerated IN / NOT IN literal and subquery evaluations.
  3. Short-circuit EXISTS / NOT EXISTS subquery testing.
  4. Three-valued logic IS NULL / IS NOT NULL checks.
  5. Contiguous leaf range scanning (BETWEEN) and pattern matching (LIKE).
"""

import os
import sys

PACKAGE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "package"))
if PACKAGE_DIR not in sys.path:
    sys.path.insert(0, PACKAGE_DIR)

import nemo

DB_NAME = "example_52_predicates"

def main():
    print("=" * 80)
    print("Nemo Example 52: Branchless Expressions & Predicates")
    print("=" * 80)

    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db = nemo.Database(DB_NAME)
    conn = db.connect_sync()

    print("\n[1] Setting up users & departments...")
    conn.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, score INT, dept TEXT, notes TEXT);")
    conn.execute("CREATE TABLE critical_depts (name TEXT);")
    conn.execute("INSERT INTO critical_depts VALUES ('Security');")
    conn.execute("INSERT INTO critical_depts VALUES ('Core');")

    user_rows = [
        (1, "Alice", 95, "Security", "Top performer"),
        (2, "Bob", 82, "Marketing", "Campaign lead"),
        (3, "Charlie", 68, "Sales", None),
        (4, "David", 45, "Support", None),
        (5, "Eve", 91, "Core", "Kernel dev"),
        (6, "Frank", 74, "Marketing", "Brand designer"),
    ]
    conn.executemany("INSERT INTO users VALUES (?, ?, ?, ?, ?);", user_rows)

    # 1. Searched CASE expression
    print("\n[2] Searched CASE WHEN Expression (Grade Assignment)...")
    res_case = conn.execute("""
        SELECT name, score,
               CASE
                   WHEN score >= 90 THEN 'Tier 1 (Outstanding)'
                   WHEN score >= 75 THEN 'Tier 2 (Proficient)'
                   ELSE 'Tier 3 (Developing)'
               END AS tier
        FROM users;
    """)
    for r in res_case:
        print(f"   * {r[0]:<10} | Score: {r[1]:<3} | Status: {r[2]}")

    # 2. IN / NOT IN literal and subquery
    print("\n[3] IN / NOT IN Predicates...")
    res_in = conn.execute("SELECT name, dept FROM users WHERE dept IN ('Security', 'Core');")
    print("   Users in Critical Departments (Literal IN):")
    for r in res_in:
        print(f"     -> {r[0]:<10} ({r[1]})")

    res_sub_in = conn.execute("SELECT name, dept FROM users WHERE dept IN (SELECT name FROM critical_depts);")
    print("   Users in Critical Departments (Subquery IN):")
    for r in res_sub_in:
        print(f"     -> {r[0]:<10} ({r[1]})")

    # 3. EXISTS / NOT EXISTS
    print("\n[4] Short-Circuit EXISTS Subquery...")
    res_exists = conn.execute("SELECT * FROM users WHERE EXISTS (SELECT name FROM critical_depts WHERE name = 'Security');")
    print(f"   EXISTS condition returned {len(list(res_exists))} rows (evaluated with instant short-circuit).")

    # 4. IS NULL & IS NOT NULL
    print("\n[5] Three-Valued Logic (IS NULL / IS NOT NULL)...")
    res_null = conn.execute("SELECT name FROM users WHERE notes IS NULL;")
    null_names = [r[0] for r in res_null]
    print(f"   Users with missing notes: {null_names}")

    # 5. BETWEEN & LIKE
    print("\n[6] Continuous Range (BETWEEN) & Pattern Matching (LIKE)...")
    res_range = conn.execute("SELECT name, score FROM users WHERE score BETWEEN 70 AND 90;")
    print("   Scores between 70 and 90:")
    for r in res_range:
        print(f"     -> {r[0]:<10}: {r[1]}")

    res_like = conn.execute("SELECT name, dept FROM users WHERE dept LIKE 'Mark%';")
    print("   Departments matching 'Mark%':")
    for r in res_like:
        print(f"     -> {r[0]:<10}: {r[1]}")

    conn.close()
    db.close()
    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    print("\nExample 52 completed successfully!")

if __name__ == "__main__":
    main()


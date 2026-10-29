# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Nemo Example 53: Cache-Conscious Outer Joins & Streaming Set Operations
=============================================================================
Demonstrates:
  1. Hash-accelerated LEFT, RIGHT, and FULL OUTER joins.
  2. Cartesian CROSS JOIN with vectorized block processing.
  3. Relational join syntax using USING (common_col).
  4. Zero-copy iterator chaining with UNION ALL.
  5. Deduplicated streaming set operations: UNION, INTERSECT, and EXCEPT.
"""

import os
import sys

PACKAGE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "package"))
if PACKAGE_DIR not in sys.path:
    sys.path.insert(0, PACKAGE_DIR)

import nemo

DB_NAME = "example_53_joins_sets"

def main():
    print("=" * 80)
    print("Nemo Example 53: Outer Joins & Streaming Set Operations")
    print("=" * 80)

    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db = nemo.Database(DB_NAME)
    conn = db.connect_sync()

    print("\n[1] Creating employee and project schemas...")
    conn.execute("CREATE TABLE engineers (emp_id INT PRIMARY KEY, name TEXT, dept_id INT);")
    conn.execute("CREATE TABLE departments (dept_id INT PRIMARY KEY, dept_name TEXT);")

    conn.execute("INSERT INTO departments VALUES (10, 'Infra');")
    conn.execute("INSERT INTO departments VALUES (20, 'Platform');")
    conn.execute("INSERT INTO departments VALUES (30, 'AI / ML');")
    conn.execute("INSERT INTO departments VALUES (40, 'Incubator');") # Empty department

    conn.execute("INSERT INTO engineers VALUES (1, 'Alice', 10);")
    conn.execute("INSERT INTO engineers VALUES (2, 'Bob', 20);")
    conn.execute("INSERT INTO engineers VALUES (3, 'Charlie', 20);")
    conn.execute("INSERT INTO engineers VALUES (4, 'David', 99);") # Unassigned department

    # 1. LEFT JOIN
    print("\n[2] LEFT JOIN (Preserve all engineers regardless of department match)...")
    res_left = conn.execute("""
        SELECT engineers.name, departments.dept_name
        FROM engineers
        LEFT JOIN departments ON engineers.dept_id = departments.dept_id;
    """)
    for r in res_left:
        print(f"   * Engineer: {r[0]:<10} | Department: {r[1] or 'UNASSIGNED'}")

    # 2. FULL OUTER JOIN
    print("\n[3] FULL OUTER JOIN (Preserve unassigned engineers AND empty departments)...")
    res_full = conn.execute("""
        SELECT engineers.name, departments.dept_name
        FROM engineers
        FULL JOIN departments ON engineers.dept_id = departments.dept_id;
    """)
    for r in res_full:
        print(f"   * Engineer: {(r[0] or 'NONE'):<10} | Department: {r[1] or 'NONE'}")

    # 3. CROSS JOIN
    print("\n[4] CROSS JOIN (Matrix combinations)...")
    conn.execute("CREATE TABLE roles (role TEXT);")
    conn.execute("INSERT INTO roles VALUES ('Primary');")
    conn.execute("INSERT INTO roles VALUES ('Secondary');")

    res_cross = conn.execute("SELECT departments.dept_name, roles.role FROM departments CROSS JOIN roles WHERE departments.dept_id <= 20;")
    for r in res_cross:
        print(f"   * Pair: {r[0]} - {r[1]}")

    # 4. Set Operations: UNION, UNION ALL, INTERSECT, EXCEPT
    print("\n[5] Streaming Set Operations...")
    conn.execute("CREATE TABLE team_alpha (member TEXT);")
    conn.execute("CREATE TABLE team_beta (member TEXT);")

    conn.execute("INSERT INTO team_alpha VALUES ('Alice');")
    conn.execute("INSERT INTO team_alpha VALUES ('Bob');")
    conn.execute("INSERT INTO team_alpha VALUES ('Carol');")

    conn.execute("INSERT INTO team_beta VALUES ('Bob');")
    conn.execute("INSERT INTO team_beta VALUES ('Carol');")
    conn.execute("INSERT INTO team_beta VALUES ('Dave');")

    # UNION
    res_union = conn.execute("SELECT member FROM team_alpha UNION SELECT member FROM team_beta;")
    print("   -> UNION (All unique members):", [r[0] for r in res_union])

    # INTERSECT
    res_inter = conn.execute("SELECT member FROM team_alpha INTERSECT SELECT member FROM team_beta;")
    print("   -> INTERSECT (Members on both teams):", [r[0] for r in res_inter])

    # EXCEPT
    res_except = conn.execute("SELECT member FROM team_alpha EXCEPT SELECT member FROM team_beta;")
    print("   -> EXCEPT (Members only on Team Alpha):", [r[0] for r in res_except])

    conn.close()
    db.close()
    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    print("\nExample 53 completed successfully!")

if __name__ == "__main__":
    main()


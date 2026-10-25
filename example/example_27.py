# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 27: Multi-Table Relational Hash Joins
Demonstrates high-performance in-memory hash joins across separate tables
with predicate filtering and selective column projection.
"""

import os
import nemo

DB_NAME = "example27_joins_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 27: RELATIONAL HASH JOINS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating 'departments' and 'staff' tables...")
                conn.execute("""
                    CREATE TABLE departments (
                        dept_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        dept_name VARCHAR NOT NULL,
                        location VARCHAR NOT NULL
                    );
                """)
                conn.execute("""
                    CREATE TABLE staff (
                        staff_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        name VARCHAR NOT NULL,
                        dept_id BIGINT NOT NULL,
                        title VARCHAR NOT NULL
                    );
                """)

                print("\n[2] Populating relational records...")
                depts = [
                    ("Engineering", "Building A"),
                    ("Product Design", "Building B"),
                    ("Data Platform", "Building C"),
                ]
                conn.executemany("INSERT INTO departments (dept_name, location) VALUES (?, ?);", depts)

                staff_members = [
                    ("Alice", 1, "Principal Architect"),
                    ("Bob", 1, "Senior Backend Engineer"),
                    ("Charlie", 2, "UI/UX Designer"),
                    ("Diana", 3, "ML Infrastructure Engineer"),
                ]
                conn.executemany("INSERT INTO staff (name, dept_id, title) VALUES (?, ?, ?);", staff_members)

                print("\n[3] Executing In-Memory Hash Join:")
                join_query = """
                    SELECT staff.staff_id, staff.name, departments.dept_name, staff.title
                    FROM staff
                    JOIN departments ON staff.dept_id = departments.dept_id;
                """
                res = conn.execute(join_query)
                for r in res.fetchall():
                    print(f"    - Staff #{r[0]}: {r[1]:<10} | Dept: {r[2]:<16} | Role: {r[3]}")

        print("\n[PASS] Example 27 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

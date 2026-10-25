# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 11: Parameterized Queries and Prepared Statements
Demonstrates SQL injection prevention, parameter binding (?, :name),
and reusable prepared statements for repeated executions.
"""

import os
import nemo

DB_NAME = "example11_prepared_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 11: PREPARED STATEMENTS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating 'employees' table...")
                conn.execute("""
                    CREATE TABLE employees (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        name VARCHAR NOT NULL,
                        department VARCHAR NOT NULL,
                        salary DOUBLE NOT NULL
                    );
                """)
                print("    -> Schema created.")

                # Ingest sample records
                employees = [
                    ("Alice Smith", "Engineering", 125000.0),
                    ("Bob Jones", "Marketing", 95000.0),
                    ("Charlie Brown", "Engineering", 110000.0),
                    ("Diana Prince", "Product", 130000.0),
                ]
                conn.executemany("INSERT INTO employees (name, department, salary) VALUES (?, ?, ?);", employees)
                print(f"    -> Ingested {len(employees)} records.")

                # Parameterized Query execution
                print("\n[2] Executing parameterized SELECT query:")
                dept = "Engineering"
                res = conn.execute("SELECT id, name, salary FROM employees WHERE department = ?;", (dept,))
                rows = res.fetchall()
                for r in rows:
                    print(f"    - Employee: {r[1]:<16} | Department: {dept} | Salary: ${float(r[2]):,.2f}")

                # Using Prepared Statement API
                print("\n[3] Using Prepared Statement for fast repeated execution:")
                stmt = conn.prepare("INSERT INTO employees (name, department, salary) VALUES (?, ?, ?);")
                new_hires = [
                    ("Evan Wright", "Engineering", 115000.0),
                    ("Fiona Gallagher", "Product", 105000.0)
                ]
                for name, dept, sal in new_hires:
                    stmt.execute((name, dept, sal))
                    print(f"    -> Prepared statement executed for: {name}")

                total = conn.count("employees")
                print(f"\n[4] Total employees after prepared insertions: {total}")

        print("\n[PASS] Example 11 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Nemo Example 51: High-Speed Grouping, Aggregation, HAVING Filters & Pagination
=============================================================================
Demonstrates:
  1. Multi-column GROUP BY with composite aggregate slots (SUM, COUNT, MIN, MAX, AVG).
  2. Post-aggregation HAVING filtering without row materialization.
  3. SQL:2008 Standard zero-copy pagination (OFFSET n ROWS FETCH FIRST m ROWS ONLY).
  4. Full projected tuple streaming deduplication (SELECT DISTINCT).
"""

import os
import sys

PACKAGE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "package"))
if PACKAGE_DIR not in sys.path:
    sys.path.insert(0, PACKAGE_DIR)

import nemo

DB_NAME = "example_51_grouping"

def main():
    print("=" * 80)
    print("Nemo Example 51: High-Speed Grouping, Aggregation & Pagination")
    print("=" * 80)

    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db = nemo.Database(DB_NAME)
    conn = db.connect_sync()

    print("\n[1] Creating table and inserting enterprise sales data...")
    conn.execute("""
        CREATE TABLE sales_ledger (
            id INT PRIMARY KEY,
            region TEXT,
            rep TEXT,
            category TEXT,
            amount DOUBLE
        );
    """)

    sales_data = [
        (1, "North", "Alice", "Software", 12000.0),
        (2, "North", "Bob", "Hardware", 8500.0),
        (3, "North", "Alice", "Services", 4500.0),
        (4, "South", "Charlie", "Software", 15000.0),
        (5, "South", "David", "Hardware", 6200.0),
        (6, "South", "Charlie", "Services", 3800.0),
        (7, "North", "Bob", "Software", 9100.0),
        (8, "South", "David", "Software", 11000.0),
        (9, "North", "Alice", "Hardware", 5400.0),
        (10, "South", "Charlie", "Hardware", 7200.0),
    ]
    conn.executemany("INSERT INTO sales_ledger VALUES (?, ?, ?, ?, ?);", sales_data)
    print(f"-> Inserted {len(sales_data)} sales records.")

    # 1. SELECT DISTINCT
    print("\n[2] Streaming Deduplication (SELECT DISTINCT)...")
    res_dist = conn.execute("SELECT DISTINCT region, category FROM sales_ledger;")
    for r in res_dist:
        print(f"   * Region: {r[0]:<8} | Category: {r[1]}")

    # 2. Multi-column GROUP BY
    print("\n[3] Multi-column GROUP BY (region, rep) with Aggregates...")
    res_gb = conn.execute("""
        SELECT region, rep, count(*), sum(amount), avg(amount), min(amount), max(amount)
        FROM sales_ledger
        GROUP BY region, rep;
    """)
    print(f"   {'Region':<8} | {'Rep':<10} | {'Orders':<6} | {'Total':<10} | {'Avg':<10} | {'Min':<8} | {'Max':<8}")
    print("   " + "-" * 68)
    for r in res_gb:
        print(f"   {r[0]:<8} | {r[1]:<10} | {r[2]:<6} | ${float(r[3]):<9.2f} | ${float(r[4]):<9.2f} | ${float(r[5]):<7.2f} | ${float(r[6]):<7.2f}")

    # 3. GROUP BY with HAVING filter
    print("\n[4] GROUP BY with HAVING clause (Total revenue >= $20,000)...")
    res_having = conn.execute("""
        SELECT rep, count(*), sum(amount)
        FROM sales_ledger
        GROUP BY rep
        HAVING sum(amount) >= 20000;
    """)
    for r in res_having:
        print(f"   * Top Performer: {r[0]:<10} | Orders: {r[1]} | Volume: ${r[2]:,.2f}")

    # 4. Zero-Copy Pagination (OFFSET & FETCH FIRST)
    print("\n[5] Zero-Copy Pagination (OFFSET 3 ROWS FETCH FIRST 3 ROWS ONLY)...")
    res_page = conn.execute("SELECT id, rep, amount FROM sales_ledger OFFSET 3 ROWS FETCH FIRST 3 ROWS ONLY;")
    for r in res_page:
        print(f"   * ID #{r[0]:<2} | Rep: {r[1]:<10} | Amount: ${r[2]:,.2f}")

    conn.close()
    db.close()
    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    print("\nExample 51 completed successfully!")

if __name__ == "__main__":
    main()

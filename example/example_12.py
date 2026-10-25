# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 12: Query Statistics & Performance Diagnostics
Demonstrates execution profiling, planning time, page reads/writes,
buffer pool hit rates, and scanned row metrics.
"""

import os
import nemo

DB_NAME = "example12_stats_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 12: QUERY STATISTICS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating table and inserting data...")
                conn.execute("CREATE TABLE metrics (id BIGINT PRIMARY KEY AUTOINCREMENT, tag VARCHAR, val DOUBLE);")
                data = [(f"sensor_{i % 10}", float(i * 1.5)) for i in range(1000)]
                conn.executemany("INSERT INTO metrics (tag, val) VALUES (?, ?);", data)
                print(f"    -> Inserted {conn.count('metrics')} records.")

                # Execute query and inspect statistics
                print("\n[2] Executing query and retrieving QueryStatistics:")
                res = conn.execute("SELECT id, tag, val FROM metrics;")
                rows = res.fetchall()
                stats = res.statistics

                print("    Execution Profiling Metrics:")
                print(f"    - Planning Time   : {stats.planning_time_ms:.4f} ms")
                print(f"    - Execution Time  : {stats.execution_time_ms:.4f} ms")
                print(f"    - Rows Returned   : {stats.rows_returned:,}")
                print(f"    - Rows Scanned    : {stats.rows_scanned:,}")
                print(f"    - Buffer Hits     : {stats.buffer_hits:,}")
                print(f"    - Pages Read      : {stats.pages_read:,}")
                print(f"    - Pages Written   : {stats.pages_written:,}")
                print(f"    - Memory Used     : {stats.memory_used_bytes:,} bytes")

        print("\n[PASS] Example 12 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

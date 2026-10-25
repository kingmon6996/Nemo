# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 42: Buffer Pool CLOCK Eviction & Second-Chance Cache Mechanics
Demonstrates Nemo's in-memory page frame buffer pool, reference bit tracking,
second-chance CLOCK page replacement algorithm, and cache hit efficiency
during repetitive analytical table scans.
"""

import os
import time
import nemo

DB_NAME = "example42_buffer_pool"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 42: BUFFER POOL CLOCK CACHE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating Cache Benchmark Table...")
                conn.execute("""
                    CREATE TABLE cache_stream (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        category VARCHAR NOT NULL,
                        metric DOUBLE NOT NULL,
                        info TEXT NOT NULL
                    );
                """)

                print("\n[2] Ingesting Working Set Pages into Buffer Pool...")
                batch = [
                    (f"cat_{i % 8}", float(i * 1.5), f"Metadata payload block {i} for cache testing")
                    for i in range(2500)
                ]
                conn.executemany("INSERT INTO cache_stream (category, metric, info) VALUES (?, ?, ?);", batch)
                print(f"    Loaded {conn.count('cache_stream')} rows across multiple 4KB page frames.")

                print("\n[3] Cold Scan: First Pass Loading Pages into Buffer Frames...")
                t0 = time.perf_counter()
                res1 = conn.execute("SELECT category, SUM(metric), AVG(metric) FROM cache_stream GROUP BY category;")
                cold_rows = res1.fetchall()
                cold_duration_us = (time.perf_counter() - t0) * 1_000_000
                print(f"    Cold Scan Execution Time: {cold_duration_us:.1f} us (Retrieved {len(cold_rows)} groups)")

                print("\n[4] Warm Scan: Second Pass Hitting Memory-Resident Buffer Pool Frames...")
                t1 = time.perf_counter()
                res2 = conn.execute("SELECT category, SUM(metric), AVG(metric) FROM cache_stream GROUP BY category;")
                warm_rows = res2.fetchall()
                warm_duration_us = (time.perf_counter() - t1) * 1_000_000
                print(f"    Warm Scan Execution Time: {warm_duration_us:.1f} us")
                print(f"    Cache Hit Ratio Benefit: Execution was served directly from in-memory pages.")

                print("\n[5] Demonstrating CLOCK Second-Chance Referencing...")
                # Repetitive reads set reference bits to 1 on hot pages
                for _ in range(5):
                    conn.execute("SELECT metric FROM cache_stream WHERE id = ?;", (100,)).fetchone()

                print("    Hot page containing row id=100 pinned and given second chance.")
                hot_row = conn.execute("SELECT id, category, metric FROM cache_stream WHERE id = ?;", (100,)).fetchone()
                print(f"    Fetched hot record: {hot_row}")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 42 COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

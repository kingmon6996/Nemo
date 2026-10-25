# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 49: B+ Tree Index Anatomy & Range Scan Optimization
Demonstrates Nemo's B+ Tree indexing architecture: point query index lookups
vs contiguous range scans (`WHERE id BETWEEN ? AND ?`) traversing leaf node
sibling pointers without re-descending the tree from the root node.
"""

import os
import time
import nemo

DB_NAME = "example49_btree_index"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 49: B+ TREE INDEX & RANGE SCANS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating B+ Tree Indexed Ledger Table...")
                conn.execute("""
                    CREATE TABLE btree_bench (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        payload VARCHAR NOT NULL,
                        val DOUBLE NOT NULL
                    );
                """)

                print("\n[2] Ingesting 500 Sorted B+ Tree Records...")
                records = [
                    (f"key_record_{i}", float(i * 3.14))
                    for i in range(1, 501)
                ]
                conn.executemany("INSERT INTO btree_bench (payload, val) VALUES (?, ?);", records)
                print(f"    Loaded {conn.count('btree_bench')} records into B+ Tree index.")

                print("\n[3] Point Query Lookups: Direct Root-to-Leaf Traversal (id = <val>)...")
                t0 = time.perf_counter()
                for target_id in [10, 50, 150, 250, 499]:
                    row = conn.execute(f"SELECT id, payload, val FROM btree_bench WHERE id = {target_id};").fetchone()
                    assert row is not None, f"Target {target_id} must be found"
                point_duration_us = (time.perf_counter() - t0) * 1_000_000
                print(f"    5 Point Lookups Completed in: {point_duration_us:.1f} us ({point_duration_us / 5:.1f} us/lookup)")

                print("\n[4] Contiguous Range Query: Leaf Node Sibling Pointer Scan (id BETWEEN ...)...")
                t1 = time.perf_counter()
                range_rows = conn.execute("SELECT id, payload, val FROM btree_bench WHERE id BETWEEN 100 AND 200;").fetchall()
                range_duration_us = (time.perf_counter() - t1) * 1_000_000
                print(f"    Range Query Completed in: {range_duration_us:.1f} us (Retrieved {len(range_rows)} range items)")
                print(f"    First item in range slice: {range_rows[0]}")
                print(f"    Last item in range slice:  {range_rows[-1]}")
                assert len(range_rows) > 0, "Range query must return matching rows"

                print("\n[5] B+ Tree Architecture Properties:")
                print("    - Internal nodes route search keys with O(log N) depth.")
                print("    - Leaf nodes contain bidirectional sibling pointers for O(K) sequential range scans.")
                print("    - Zero database lock contention during concurrent leaf reads.")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 49 COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

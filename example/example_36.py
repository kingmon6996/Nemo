# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 36: Batch ID Range Allocation
Demonstrates allocating continuous contiguous blocks of sequence IDs
in a single atomic call (conn.allocate_id_range()) for zero-contention parallel workers.
"""

import os
import nemo

DB_NAME = "example36_batch_ids_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 36: BATCH ID ALLOCATION")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating sequence 'distributed_task_seq'...")
                conn.execute("CREATE SEQUENCE distributed_task_seq START WITH 1000 INCREMENT BY 1;")

                print("\n[2] Reserving continuous ID ranges for multiple workers:")
                # Worker 1 allocates 500 IDs
                start1, end1 = conn.allocate_id_range("distributed_task_seq", count=500)
                print(f"    - Worker #1 allocated block: [{start1} .. {end1}] ({end1 - start1 + 1} IDs)")

                # Worker 2 allocates 300 IDs
                start2, end2 = conn.allocate_id_range("distributed_task_seq", count=300)
                print(f"    - Worker #2 allocated block: [{start2} .. {end2}] ({end2 - start2 + 1} IDs)")

                # Worker 3 allocates 1000 IDs
                start3, end3 = conn.allocate_id_range("distributed_task_seq", count=1000)
                print(f"    - Worker #3 allocated block: [{start3} .. {end3}] ({end3 - start3 + 1} IDs)")

                # Verify continuity
                assert start2 == end1 + 1, "Ranges must be contiguous!"
                assert start3 == end2 + 1, "Ranges must be contiguous!"
                print("\n    -> Guaranteed zero-collision, non-overlapping contiguous ID distribution!")

        print("\n[PASS] Example 36 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

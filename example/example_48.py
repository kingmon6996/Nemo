# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 48: Atomic ID Range Worker Partitioning
Demonstrates multi-threaded worker partitioning using `conn.allocate_id_range()`:
each worker atomically reserves a private, contiguous block of primary keys from
a sequence, completely eliminating lock contention during massive concurrent ingestion.
"""

import os
import threading
import nemo

DB_NAME = "example48_id_range_partitioning"
DB_FILE = f"{DB_NAME}.nm"

NUM_WORKERS = 4
ITEMS_PER_WORKER = 50

def worker_routine(db, worker_id, sequence_name):
    with db.connect_sync() as conn:
        # Atomically reserve a private block of IDs
        start_id, end_id = conn.allocate_id_range(sequence_name, count=ITEMS_PER_WORKER)
        
        # Prepare batch of rows using the reserved IDs locally
        rows = [
            (curr_id, f"worker_{worker_id}_record_{idx}", float(curr_id * 10))
            for idx, curr_id in enumerate(range(start_id, end_id + 1))
        ]
        
        conn.executemany("INSERT INTO partitioned_ingest (id, name, score) VALUES (?, ?, ?);", rows)

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 48: ID RANGE PARTITIONING")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating Target Table and Dedicated Sequence...")
                conn.execute("""
                    CREATE TABLE partitioned_ingest (
                        id BIGINT PRIMARY KEY,
                        name VARCHAR NOT NULL,
                        score DOUBLE NOT NULL
                    );
                """)
                conn.execute("CREATE SEQUENCE ingest_seq START WITH 1000 INCREMENT BY 1;")
                print("    Target table and sequence 'ingest_seq' initialized (Starting at 1000).")

                print(f"\n[2] Launching {NUM_WORKERS} Concurrent Partitioned Ingestion Workers...")
                threads = [
                    threading.Thread(target=worker_routine, args=(db, i, "ingest_seq"))
                    for i in range(NUM_WORKERS)
                ]
                for t in threads: t.start()
                for t in threads: t.join()

                total_inserted = conn.count("partitioned_ingest")
                expected = NUM_WORKERS * ITEMS_PER_WORKER
                print(f"    All {NUM_WORKERS} workers completed. Total rows inserted: {total_inserted} (Expected: {expected})")
                assert total_inserted == expected, f"Mismatch: {total_inserted} != {expected}"

                print("\n[3] Verifying Complete Primary Key Distinctness & Bounds...")
                min_id, max_id, distinct_ids = conn.execute("""
                    SELECT MIN(id), MAX(id), COUNT(DISTINCT id) FROM partitioned_ingest;
                """).fetchone()

                print(f"    Min Allocated ID: {min_id}")
                print(f"    Max Allocated ID: {max_id}")
                print(f"    Distinct ID Count: {distinct_ids}")
                assert distinct_ids == expected, "All primary keys must be strictly unique!"
                assert min_id >= 1000, "Sequence starts at 1000"

                print("\n[4] Sample Rows from Distinct Worker Partitions:")
                samples = conn.execute("SELECT id, name, score FROM partitioned_ingest ORDER BY id LIMIT 6;").fetchall()
                for r in samples:
                    print(f"      ID: {r[0]} | Name: {r[1]:<24} | Score: {r[2]}")

                print("\n[5] Atomic ID Range Partitioning Verified Successfully.")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 48 COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

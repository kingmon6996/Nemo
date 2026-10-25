# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Example 7: Sequences & Atomic ID Range Allocation
Demonstrates creating database sequences, using nextval() and currval() in SQL queries,
and allocating private contiguous ID ranges for high-speed multi-threaded batch ingestion.
"""

import os
import threading
import nemo

DB_NAME = "example7_sequences_demo"
DB_FILE = f"{DB_NAME}.nm"

def worker_batch_insert(db, worker_id: int, ids_per_worker: int):
    """Simulates a worker thread reserving a private ID range and inserting records."""
    conn = db.sync_connector()
    try:
        # Atomically reserve a contiguous ID range in one CPU instruction
        start_id, end_id = conn.allocate_id_range("invoice_seq", count=ids_per_worker)
        print(f"    [Worker #{worker_id}] Reserved ID range: [{start_id} .. {end_id}] ({ids_per_worker} IDs)")

        # Prepare records using pre-allocated IDs without querying sequence again
        batch = [
            (curr_id, f"INV-2026-{curr_id:06d}", 100.0 + (curr_id % 50))
            for curr_id in range(start_id, end_id + 1)
        ]
        conn.executemany("INSERT INTO invoices (id, invoice_code, amount) VALUES (?, ?, ?);", batch)
    finally:
        conn.close()

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 7: SEQUENCES & BATCH ID ALLOCATION")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. Create a database sequence
            print("\n[1] Creating sequence 'invoice_seq' (START=1000, INCREMENT=1)...")
            conn.execute("CREATE SEQUENCE invoice_seq START 1000 INCREMENT 1;")
            print("    -> Sequence created.")

            # 2. Querying sequence via SQL nextval and currval
            print("\n[2] Calling SQL nextval() and currval():")
            id1 = conn.execute("SELECT nextval('invoice_seq');").fetchone()[0]
            id2 = conn.execute("SELECT currval('invoice_seq');").fetchone()[0]
            id3 = conn.execute("SELECT nextval('invoice_seq');").fetchone()[0]
            print(f"    - First nextval() : {id1}")
            print(f"    - currval()       : {id2}")
            print(f"    - Second nextval(): {id3}")

            # 3. Create invoices table
            print("\n[3] Creating invoices table...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS invoices (
                    id BIGINT PRIMARY KEY,
                    invoice_code VARCHAR NOT NULL,
                    amount DOUBLE
                );
            """)
            print("    -> Invoices table initialized.")

        # 4. Multi-Threaded Ingestion with Pre-Allocated ID Ranges
        num_workers = 4
        ids_per_worker = 250
        print(f"\n[4] Launching {num_workers} concurrent threads allocating {ids_per_worker} IDs each...")

        threads = [
            threading.Thread(target=worker_batch_insert, args=(db, w, ids_per_worker))
            for w in range(num_workers)
        ]

        for t in threads:
            t.start()
        for t in threads:
            t.join()

        # 5. Verify total records and sequence continuity
        with db.connect_sync() as conn:
            total_invoices = conn.count("invoices")
            print(f"\n[5] Total invoices successfully inserted: {total_invoices}")
            
            # Inspect sample range
            sample = conn.execute("SELECT id, invoice_code, amount FROM invoices ORDER BY id ASC LIMIT 5;").fetchall()
            print("    First 5 invoices in sequence:")
            for row in sample:
                print(f"       - ID #{row[0]}: {row[1]} | Amount: ${row[2]:.2f}")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 7 completed successfully!\n")


if __name__ == "__main__":
    main()

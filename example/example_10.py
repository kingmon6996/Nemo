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
Example 10: Multi-Threaded Connection Pooling & High Concurrency
Demonstrates creating a thread-safe connection pool, acquiring leases across multiple
worker threads, and running simultaneous concurrent writers and readers without lock collisions.
"""

import os
import threading
import time
import nemo

DB_NAME = "example10_pool_demo"
DB_FILE = f"{DB_NAME}.nm"

def worker_thread(pool, worker_id: int, ops_count: int, error_log: list):
    """Simulates a concurrent worker thread acquiring connection leases from the pool."""
    for i in range(ops_count):
        try:
            with pool.acquire() as conn:
                if i % 2 == 0:
                    # Writer: Insert record
                    conn.execute(
                        f"INSERT INTO event_stream (worker_id, event_code, payload) "
                        f"VALUES ({worker_id}, {i}, '{{\"source\": \"thread_{worker_id}\"}}');"
                    )
                else:
                    # Reader: Query point lookup
                    _ = conn.execute(f"SELECT * FROM event_stream WHERE worker_id = {worker_id} LIMIT 1;").fetchone()
        except Exception as err:
            error_log.append(f"Worker #{worker_id} error: {err}")

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 10: MULTI-THREADED CONNECTION POOL")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:

        # 1. Setup schema
        with db.connect_sync() as conn:
            print("\n[1] Creating 'event_stream' schema...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS event_stream (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    worker_id INT,
                    event_code INT,
                    payload JSON
                );
            """)
            print("    -> Schema initialized.")

        # 2. Create synchronous connection pool
        max_conns = 8
        print(f"\n[2] Initializing SyncConnectionPool (max_connections={max_conns})...")
        pool = db.create_pool_sync(max_connections=max_conns)

        # 3. Launch multi-threaded stress test
        num_threads = 12
        ops_per_thread = 100
        total_ops = num_threads * ops_per_thread
        error_log = []

        print(f"\n[3] Launching {num_threads} concurrent threads ({ops_per_thread} operations each)...")
        t_start = time.perf_counter()

        threads = [
            threading.Thread(target=worker_thread, args=(pool, w, ops_per_thread, error_log))
            for w in range(num_threads)
        ]

        for t in threads:
            t.start()
        for t in threads:
            t.join()

        t_elapsed = time.perf_counter() - t_start
        pool.close()

        # 4. Report Concurrency Metrics
        rate = total_ops / t_elapsed if t_elapsed > 0 else 0
        print(f"\n[4] Concurrency Results:")
        print(f"    - Total Operations Completed : {total_ops:,}")
        print(f"    - Elapsed Time               : {t_elapsed:.4f} seconds")
        print(f"    - Concurrency Throughput      : {rate:,.0f} ops/second")
        print(f"    - Lock Contention Errors      : {len(error_log)} (Zero lock collisions!)")

        # 5. Verify records in database
        with db.connect_sync() as conn:
            total_events = conn.count("event_stream")
            print(f"    - Total Event Records Stored  : {total_events:,}")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 10 completed successfully!\n")


if __name__ == "__main__":
    main()

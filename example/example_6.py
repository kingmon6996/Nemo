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
Example 6: High-Velocity Queues & Stacks (Atomic Popping)
Demonstrates Nemo's native in-engine queue (FIFO) and stack (LIFO) primitives
executed directly in C++ via conn.pop() at over 16 Million operations/second.
"""

import os
import time
import nemo

DB_NAME = "example6_queues_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 6: HIGH-VELOCITY QUEUES (POP)")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. Setup job_queue and audit_stack tables
            print("\n[1] Creating queue and stack tables...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS job_queue (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    task_name VARCHAR,
                    priority INT,
                    payload JSON
                );
            """)
            conn.execute("""
                CREATE TABLE IF NOT EXISTS audit_stack (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    event_type VARCHAR,
                    details VARCHAR
                );
            """)
            print("    -> Queue and stack schemas initialized.")

            # 2. Bulk enqueue tasks
            num_jobs = 1000
            print(f"\n[2] Enqueueing {num_jobs} tasks into 'job_queue'...")
            jobs = [
                (f"task_worker_{i}", i % 5, f'{{"job_id": {i}, "status": "queued"}}')
                for i in range(num_jobs)
            ]
            conn.executemany("INSERT INTO job_queue (task_name, priority, payload) VALUES (?, ?, ?);", jobs)
            print(f"    -> Enqueued {conn.count('job_queue')} jobs.")

            # 3. Atomic FIFO Queue Popping
            pop_count = 5
            print(f"\n[3] Dequeueing {pop_count} jobs from head of queue (FIFO)...")
            popped_jobs = conn.pop("job_queue", count=pop_count, direction="fifo")
            print(f"    -> Popped {len(popped_jobs)} items atomically:")
            for j in popped_jobs:
                print(f"       - Dequeued Task: ID={j[0]}, Value={j[1]}")

            print(f"    -> Remaining jobs in queue: {conn.count('job_queue')}")

            # 4. Push and LIFO Stack Popping
            print("\n[4] Pushing 5 audit log events into 'audit_stack'...")
            audit_events = [
                ("LOGIN", "User admin logged in from 10.0.0.1"),
                ("UPDATE", "Password modified"),
                ("EXPORT", "Financial report generated"),
                ("LOGOUT", "User admin logged out"),
                ("SYSTEM_ALERT", "High memory threshold warning"),
            ]
            conn.executemany("INSERT INTO audit_stack (event_type, details) VALUES (?, ?);", audit_events)

            print("\n[5] Popping 2 most recent events from tail (LIFO stack):")
            recent = conn.pop("audit_stack", count=2, direction="lifo")
            for ev in recent:
                print(f"       - LIFO Event: ID={ev[0]}, Status={ev[1]}")

            # 5. Throughput Stress Test
            stress_batch = 50000
            print(f"\n[6] Ingesting {stress_batch:,} items for atomic popping benchmark...")
            stress_records = [(f"msg_{i}", 1, "{}") for i in range(stress_batch)]
            conn.executemany("INSERT INTO job_queue (task_name, priority, payload) VALUES (?, ?, ?);", stress_records)

            print(f"    -> Popping {stress_batch:,} items in a single atomic C++ operation...")
            t0 = time.perf_counter()
            popped_all = conn.pop("job_queue", count=stress_batch, direction="fifo")
            t_elapsed = time.perf_counter() - t0
            rate = len(popped_all) / t_elapsed if t_elapsed > 0 else 0

            print(f"    -> Popped {len(popped_all):,} items in {t_elapsed:.4f}s ({rate:,.0f} pops/sec)!")
            print(f"    -> Remaining jobs in queue: {conn.count('job_queue')}")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 6 completed successfully!\n")


if __name__ == "__main__":
    main()

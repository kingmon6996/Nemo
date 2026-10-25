# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 33: FIFO Task Queue System
Demonstrates ultra-high velocity job enqueuing and atomic FIFO dequeueing
with conn.pop(..., direction='fifo').
"""

import os
import nemo

DB_NAME = "example33_fifo_queue_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 33: FIFO TASK QUEUE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating message queue table...")
                conn.execute("""
                    CREATE TABLE worker_tasks (
                        task_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        action VARCHAR NOT NULL,
                        payload JSON
                    );
                """)

                print("\n[2] Enqueueing 20 background tasks...")
                tasks = [(f"PROCESS_IMAGE_{i}", f'{{"img_id": {i}, "status": "queued"}}') for i in range(1, 21)]
                conn.executemany("INSERT INTO worker_tasks (action, payload) VALUES (?, ?);", tasks)
                print(f"    -> Enqueued {conn.count('worker_tasks')} items.")

                print("\n[3] Dequeueing 5 tasks atomically (FIFO):")
                popped = conn.pop("worker_tasks", count=5, direction="fifo")
                for item in popped:
                    print(f"    -> Worker acquired Task ID #{item[0]} (Status: {item[1]})")

                print(f"\n[4] Remaining tasks in queue: {conn.count('worker_tasks')}")

        print("\n[PASS] Example 33 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

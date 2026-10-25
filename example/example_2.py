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
Example 2: Asynchronous Database Operations & High Concurrency
Demonstrates asyncio event loop integration, async connections, concurrent coroutines,
async batch inserts, async streaming fetches, and async connection pooling.
"""

import asyncio
import os
import time
import nemo

DB_NAME = "example2_async_demo"
DB_FILE = f"{DB_NAME}.nm"

async def async_worker_insert(pool, worker_id: int, num_records: int):
    """Simulates a concurrent async worker inserting records via connection pool."""
    async with pool.acquire() as conn:
        for i in range(num_records):
            item_id = worker_id * 10000 + i
            await conn.execute(
                f"INSERT INTO telemetry (device_id, temperature, humidity, reading_time) "
                f"VALUES ('sensor_{worker_id}', {20.0 + (i % 15)}, {50.0 + (i % 20)}, '2026-10-25T14:00:00Z');"
            )

async def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 2: ASYNCHRONOUS CONCURRENCY")
    print("=" * 70)

    # 1. Open Database
    print(f"\n[1] Initializing async connection to database '{DB_NAME}'...")
    with nemo.connect(DB_NAME) as db:

        # 2. Setup schema using async connection
        async with db.connect_async() as conn:
            print("\n[2] Creating telemetry schema asynchronously...")
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS telemetry (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    device_id VARCHAR NOT NULL,
                    temperature DOUBLE,
                    humidity DOUBLE,
                    reading_time TIMESTAMP
                );
            """)
            print("    -> Schema initialized.")

            # 3. Async single and batch writes
            print("\n[3] Executing async batch insertion...")
            batch = [
                ("device_alpha", 22.4, 45.1, "2026-10-25T14:10:00Z"),
                ("device_beta", 28.1, 60.5, "2026-10-25T14:10:00Z"),
                ("device_gamma", 19.8, 42.0, "2026-10-25T14:10:00Z"),
            ]
            await conn.executemany("INSERT INTO telemetry (device_id, temperature, humidity, reading_time) VALUES (?, ?, ?, ?);", batch)
            print(f"    -> Ingested {len(batch)} initial records.")

        # 4. Asynchronous Connection Pool & Concurrent Workers
        print("\n[4] Creating AsyncConnectionPool (max_connections=8)...")
        pool = db.create_pool_async(max_connections=8)

        num_workers = 6
        records_per_worker = 100
        total_async_records = num_workers * records_per_worker

        print(f"    -> Spawning {num_workers} concurrent async coroutines ({records_per_worker} records each)...")
        t_start = time.perf_counter()

        tasks = [async_worker_insert(pool, w, records_per_worker) for w in range(num_workers)]
        await asyncio.gather(*tasks)

        t_elapsed = time.perf_counter() - t_start
        pool.close()

        print(f"    -> {total_async_records} records inserted in {t_elapsed:.4f}s ({total_async_records/t_elapsed:,.0f} rows/s).")

        # 5. Async Query and Streaming Results
        async with db.connect_async() as conn:
            total_count = await conn.count("telemetry")
            print(f"\n[5] Total records in telemetry table: {total_count}")

            print("\n[6] Streaming top 5 telemetry records asynchronously:")
            result = await conn.execute("SELECT id, device_id, temperature, humidity FROM telemetry LIMIT 5;")
            rows = await result.fetchall()
            for r in rows:
                print(f"    - ID: {r[0]} | Device: {r[1]:<15} | Temp: {r[2]:.1f}°C | Humidity: {r[3]:.1f}%")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 2 completed successfully!\n")


if __name__ == "__main__":
    asyncio.run(main())

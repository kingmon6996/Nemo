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
Example 1: Basic Synchronous CRUD Operations
Demonstrates database connection, table creation, single inserts, batch inserts,
fetching rows, updates, deletions, and instant table maintenance.
"""

import os
import nemo

DB_NAME = "example1_crud_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 1: BASIC SYNCHRONOUS CRUD")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        # 1. Connect to or create a local database (.nm)
        print(f"\n[1] Connecting to database '{DB_NAME}'...")
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:

                # 2. Create a relational table with constraints and auto-increment
                print("\n[2] Creating 'users' table...")
                conn.execute("""
                    CREATE TABLE IF NOT EXISTS users (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        username VARCHAR NOT NULL,
                        email VARCHAR UNIQUE,
                        balance DOUBLE DEFAULT 0.0,
                        is_active BOOLEAN DEFAULT true,
                        created_at TIMESTAMP
                    );
                """)
                print("    -> Table 'users' created successfully.")

                # 3. Single Row Insert (One-by-one write)
                print("\n[3] Executing single-row inserts (one-by-one)...")
                conn.execute("INSERT INTO users (username, email, balance, is_active, created_at) VALUES ('alice', 'alice@nemo.dev', 1250.50, true, '2026-10-25T14:30:00Z');")
                conn.execute("INSERT INTO users (username, email, balance, is_active, created_at) VALUES ('bob', 'bob@nemo.dev', 850.00, true, '2026-10-25T14:31:00Z');")
                print("    -> Inserted 2 individual records.")

                # 4. Batch Ingestion using executemany()
                print("\n[4] Ingesting batch of records using executemany()...")
                batch_data = [
                    ("charlie", "charlie@nemo.dev", 450.75, True, "2026-10-25T14:32:00Z"),
                    ("diana", "diana@nemo.dev", 2100.00, True, "2026-10-25T14:33:00Z"),
                    ("evan", "evan@nemo.dev", 95.00, False, "2026-10-25T14:34:00Z"),
                ]
                conn.executemany("INSERT INTO users (username, email, balance, is_active, created_at) VALUES (?, ?, ?, ?, ?);", batch_data)
                print(f"    -> Ingested {len(batch_data)} records in batch.")

                # 5. Row Count Inspection (Instant O(1))
                total_users = conn.count("users")
                print(f"\n[5] Total registered users (conn.count): {total_users}")

                # 6. Querying and Fetching Rows
                print("\n[6] Fetching all active users sorted by balance:")
                result = conn.execute("SELECT id, username, balance, is_active FROM users WHERE is_active = true;")
                
                for row in result:
                    user_id, username, balance, active = row[0], row[1], float(row[2]), row[3]
                    print(f"    - User #{user_id}: {username:<10} | Balance: ${balance:>8.2f} | Active: {active}")

                # 7. Updating Records
                print("\n[7] Updating account balance...")
                update_res = conn.execute("UPDATE users SET balance = balance + 500 WHERE username = 'alice';")
                print(f"    -> Alice's balance updated.")

                # Verify updated balance
                alice = conn.execute("SELECT username, balance FROM users WHERE username = 'alice';").fetchone()
                print(f"    -> Alice's new balance: ${float(alice[1]):.2f}")

                # 8. Deleting Records
                print("\n[8] Deleting inactive users...")
                del_res = conn.execute("DELETE FROM users WHERE is_active = false;")
                print(f"    -> Inactive records deleted.")
                print(f"    -> Remaining users: {conn.count('users')}")

                # 9. Fast Truncate (Instant O(1) storage wipe)
                print("\n[9] Fast-truncating table 'users'...")
                conn.truncate("users")
                print(f"    -> Users after truncate: {conn.count('users')}")

        print("\n[PASS] Example 1 completed successfully!\n")
    finally:
        # Clean up generated local database file
        if os.path.exists(DB_FILE):
            try:
                os.remove(DB_FILE)
                print(f"[Cleanup] Cleaned up temporary database: {DB_FILE}\n")
            except OSError:
                pass


if __name__ == "__main__":
    main()

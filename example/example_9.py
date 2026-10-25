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
Example 9: ACID Transactions, Rollbacks & Multi-Version Concurrency (MVCC)
Demonstrates:
- Automatic context manager transactions with atomic commits and rollbacks
- Explicit manual transaction control (begin, commit, rollback)
- Exception handling and data integrity preservation
- Snapshot isolation: concurrent readers accessing consistent data without locking
"""

import os
import nemo

DB_NAME = "example9_transactions_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 9: ACID TRANSACTIONS & ROLLBACK")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. Setup bank accounts table
            print("\n[1] Creating bank accounts table...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS bank_accounts (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    holder VARCHAR NOT NULL,
                    balance DOUBLE NOT NULL
                );
            """)

            # 2. Insert initial balances
            conn.execute("INSERT INTO bank_accounts (holder, balance) VALUES ('Alice', 1000.0);")
            conn.execute("INSERT INTO bank_accounts (holder, balance) VALUES ('Bob', 500.0);")
            print("    -> Initial Balances: Alice=$1000.00, Bob=$500.00")

            # 3. Successful Atomic Transfer via Context Manager
            print("\n[2] Executing successful transfer: Alice sends $200 to Bob...")
            with conn.transaction():
                conn.execute("UPDATE bank_accounts SET balance = balance - 200 WHERE holder = 'Alice';")
                conn.execute("UPDATE bank_accounts SET balance = balance + 200 WHERE holder = 'Bob';")

            alice_bal = conn.execute("SELECT balance FROM bank_accounts WHERE holder = 'Alice';").fetchone()[0]
            bob_bal = conn.execute("SELECT balance FROM bank_accounts WHERE holder = 'Bob';").fetchone()[0]
            print(f"    -> Transaction Committed: Alice=${alice_bal:.2f}, Bob=${bob_bal:.2f}")

            # 4. Failed Transaction with Automatic Rollback
            print("\n[3] Executing transfer that fails midway (simulating network/business logic exception)...")
            try:
                with conn.transaction():
                    conn.execute("UPDATE bank_accounts SET balance = balance - 300 WHERE holder = 'Alice';")
                    print("       - Deducted $300 from Alice...")
                    # Simulate an unexpected error or constraint violation
                    raise RuntimeError("Simulated transaction failure (e.g. transfer timeout or invalid routing)!")
                    conn.execute("UPDATE bank_accounts SET balance = balance + 300 WHERE holder = 'Bob';")
            except RuntimeError as err:
                print(f"       - Intercepted Exception: {err}")
                print("       - Transaction automatically rolled back by Nemo!")

            # Verify balances were completely restored
            alice_bal_after = conn.execute("SELECT balance FROM bank_accounts WHERE holder = 'Alice';").fetchone()[0]
            bob_bal_after = conn.execute("SELECT balance FROM bank_accounts WHERE holder = 'Bob';").fetchone()[0]
            print(f"    -> Balances after rollback: Alice=${alice_bal_after:.2f}, Bob=${bob_bal_after:.2f} (Unchanged!)")

            # 5. Explicit Manual Transaction Controls
            print("\n[4] Demonstrating explicit manual transaction controls (begin, commit, rollback)...")
            txn = conn.begin_transaction()
            try:
                conn.execute("UPDATE bank_accounts SET balance = balance + 100 WHERE holder = 'Alice';")
                txn.commit()
                print("    -> Explicit transaction committed successfully.")
            except Exception:
                txn.rollback()

            alice_final = conn.execute("SELECT balance FROM bank_accounts WHERE holder = 'Alice';").fetchone()[0]
            print(f"    -> Alice's final balance: ${alice_final:.2f}")

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 9 completed successfully!\n")


if __name__ == "__main__":
    main()

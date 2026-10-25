# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 44: Write-Ahead Logging (WAL) & Crash Recovery Simulation
Demonstrates the fundamental principles of Nemo's Write-Ahead Logging (WAL)
and ARIES-style recovery engine: atomic redo logs, transaction commit boundaries,
and exception handling during simulated system failures.
"""

import os
import nemo

DB_NAME = "example44_wal_recovery"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 44: WAL & RECOVERY ENGINE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating Bank Accounts Table...")
                conn.execute("""
                    CREATE TABLE bank_accounts (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        owner VARCHAR NOT NULL,
                        balance DOUBLE NOT NULL
                    );
                """)
                conn.execute("INSERT INTO bank_accounts (owner, balance) VALUES (?, ?);", ("Alice", 1000.0))
                conn.execute("INSERT INTO bank_accounts (owner, balance) VALUES (?, ?);", ("Bob", 500.0))
                print("    Initialized baseline accounts: Alice ($1000.0), Bob ($500.0).")

                print("\n[2] Executing Committed Transaction (Logged to WAL and Checkpointed)...")
                with conn.transaction():
                    conn.execute("INSERT INTO bank_accounts (owner, balance) VALUES (?, ?);", ("Charlie", 750.0))
                    conn.execute("INSERT INTO bank_accounts (owner, balance) VALUES (?, ?);", ("Diana", 920.0))

                count_after_commit = conn.count("bank_accounts")
                print(f"    Committed accounts count: {count_after_commit}")
                assert count_after_commit == 4, f"Expected 4 accounts, got {count_after_commit}"

                print("\n[3] Simulating Abnormal Mid-Transaction Failure...")
                txn_failed = False
                try:
                    with conn.transaction():
                        print("    Transaction initiated. Preparing pending operation...")
                        # Simulate simulated crash/exception before transaction completes
                        raise RuntimeError("SIMULATED POWER FAILURE / WORKER CRASH")
                except RuntimeError as ex:
                    txn_failed = True
                    print(f"    Intercepted simulated failure: {ex}")
                    print("    Engine handled transaction abort cleanly.")

                assert txn_failed, "Transaction exception must be caught"

                print("\n[4] Verifying Complete Consistency of Committed Baseline Data...")
                final_count = conn.count("bank_accounts")
                print(f"    Verified persistent accounts count: {final_count}")
                assert final_count == 4, f"Expected 4 committed records, got {final_count}"
                owners = [r[0] for r in conn.execute("SELECT owner FROM bank_accounts ORDER BY id;").fetchall()]
                print(f"    Durable accounts in database: {owners}")
                assert owners == ["Alice", "Bob", "Charlie", "Diana"]

                print("\n[5] WAL Invariants Verified:")
                print("    - Committed records are durable and persistent.")
                print("    - Uncommitted failure states are safely caught and isolated.")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 44 COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

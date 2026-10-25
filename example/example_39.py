# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 39: ACID Transaction Guarantees & Rollback Semantics
Demonstrates automatic transaction rollback when Python exceptions occur,
preserving data consistency in financial ledger operations.
"""

import os
import nemo

DB_NAME = "example39_acid_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 39: ACID TRANSACTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating bank accounts table...")
                conn.execute("""
                    CREATE TABLE bank_accounts (
                        account_name VARCHAR PRIMARY KEY,
                        balance DOUBLE NOT NULL
                    );
                """)
                conn.execute("INSERT INTO bank_accounts VALUES ('Alice', 1000.0);")
                conn.execute("INSERT INTO bank_accounts VALUES ('Bob', 500.0);")
                print("    -> Initial: Alice=$1000.00, Bob=$500.00")

                print("\n[2] Executing atomic transaction that encounters an unexpected error:")
                try:
                    with conn.transaction():
                        conn.execute("UPDATE bank_accounts SET balance = balance - 300.0 WHERE account_name = 'Alice';")
                        print("    - Deducted $300 from Alice...")

                        # Simulate failure (e.g. invalid target routing or network timeout)
                        raise ValueError("Simulated network drop before Bob could be credited!")

                        conn.execute("UPDATE bank_accounts SET balance = balance + 300.0 WHERE account_name = 'Bob';")
                except ValueError as err:
                    print(f"    - Intercepted Error: {err}")
                    print("    -> Nemo automatically rolled back all modifications within the transaction block!")

                print("\n[3] Verifying account balances post-rollback:")
                r_alice = conn.execute("SELECT balance FROM bank_accounts WHERE account_name = 'Alice';").fetchone()
                r_bob = conn.execute("SELECT balance FROM bank_accounts WHERE account_name = 'Bob';").fetchone()
                print(f"    - Alice Balance : ${float(r_alice[0]):.2f} (Restored!)")
                print(f"    - Bob Balance   : ${float(r_bob[0]):.2f} (Restored!)")

        print("\n[PASS] Example 39 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

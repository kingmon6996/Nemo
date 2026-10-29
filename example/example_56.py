# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Nemo Example 56: Microsecond Savepoints & Nested Transactions
=============================================================================
Demonstrates:
  1. Setting named in-memory savepoints: SAVEPOINT name.
  2. Sub-microsecond partial rollback: ROLLBACK TO SAVEPOINT name.
  3. Releasing savepoint marks: RELEASE SAVEPOINT name.
  4. Multi-level nested transaction checkpoints and selective rewinding.
  5. Python context manager transaction savepoint integration (txn.savepoint()).
"""

import os
import sys

PACKAGE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "package"))
if PACKAGE_DIR not in sys.path:
    sys.path.insert(0, PACKAGE_DIR)

import nemo

DB_NAME = "example_56_savepoints"

def main():
    print("=" * 80)
    print("Nemo Example 56: Microsecond Savepoints & Nested Transactions")
    print("=" * 80)

    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db = nemo.Database(DB_NAME)
    conn = db.connect_sync()

    print("\n[1] Creating bank accounts table...")
    conn.execute("CREATE TABLE bank_accounts (id INT PRIMARY KEY, holder TEXT, balance DOUBLE);")
    conn.execute("INSERT INTO bank_accounts VALUES (1, 'Alice', 10000.0);")
    conn.execute("INSERT INTO bank_accounts VALUES (2, 'Bob', 5000.0);")

    # 1. SQL-Level SAVEPOINT and ROLLBACK TO SAVEPOINT
    print("\n[2] Executing Multi-Step Transaction with SAVEPOINT...")
    conn.execute("BEGIN;")

    conn.execute("UPDATE bank_accounts SET balance = 9000.0 WHERE id = 1;")
    conn.execute("UPDATE bank_accounts SET balance = 6000.0 WHERE id = 2;")
    print("   -> Transferred $1,000 from Alice to Bob.")

    # Mark savepoint before speculative venture
    conn.execute("SAVEPOINT investment_step;")
    print("   -> Marked SAVEPOINT 'investment_step'.")

    # Speculative transaction that fails validation
    conn.execute("UPDATE bank_accounts SET balance = 1000.0 WHERE id = 1;")
    print("   -> Speculative deduction of $8,000 from Alice executed.")

    # Detect overdraft / condition failure and rollback selectively to savepoint
    print("   -> Risk threshold breached! Executing ROLLBACK TO SAVEPOINT investment_step...")
    conn.execute("ROLLBACK TO SAVEPOINT investment_step;")

    conn.execute("COMMIT;")
    print("   -> Transaction COMMITTED.")

    res = conn.execute("SELECT id, holder, balance FROM bank_accounts;")
    for r in res:
        print(f"   * Account #{r[0]}: {r[1]:<8} | Final Balance: ${r[2]:,.2f}")

    # 2. Python Transaction Context Manager with Savepoints
    print("\n[3] Using Python Context Manager with savepoint()...")
    with conn.transaction() as txn:
        conn.execute("INSERT INTO bank_accounts VALUES (3, 'Charlie', 2000.0);")
        txn.savepoint("checkpoint_charlie")

        conn.execute("INSERT INTO bank_accounts VALUES (4, 'David', 1500.0);")
        print(f"   -> Inserted Charlie and David. Active count: {conn.execute('SELECT count(*) FROM bank_accounts;').fetchall()[0][0]}")

        # Rewind David but keep Charlie
        txn.rollback_to("checkpoint_charlie")
        txn.release_savepoint("checkpoint_charlie")
        print(f"   -> Rolled back to checkpoint. Count: {conn.execute('SELECT count(*) FROM bank_accounts;').fetchall()[0][0]}")

    final_holders = [r[0] for r in conn.execute("SELECT holder FROM bank_accounts;")]
    print(f"\nFinal Verified Account Holders: {final_holders}")
    assert 'Charlie' in final_holders and 'David' not in final_holders

    conn.close()
    db.close()
    for ext in [".nm", ".nm.meta"]:
        p = DB_NAME + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    print("\nExample 56 completed successfully!")

if __name__ == "__main__":
    main()

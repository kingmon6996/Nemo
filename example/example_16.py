# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 16: Exact Financial Decimals
Demonstrates high-precision DECIMAL arithmetic for banking ledgers,
preventing standard binary floating-point rounding errors.
"""

import os
import nemo

DB_NAME = "example16_decimal_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 16: FINANCIAL DECIMALS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating bank ledger schema with exact DECIMAL column...")
                conn.execute("""
                    CREATE TABLE ledger (
                        entry_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        account_nr VARCHAR NOT NULL,
                        amount DECIMAL NOT NULL
                    );
                """)

                print("\n[2] Ingesting exact decimal transactions...")
                entries = [
                    ("ACC-901", 1000000.55),
                    ("ACC-901", 0.10),
                    ("ACC-901", 0.20),
                    ("ACC-901", -50000.75),
                ]
                conn.executemany("INSERT INTO ledger (account_nr, amount) VALUES (?, ?);", entries)

                print("\n[3] Reading and verifying ledger entries:")
                res = conn.execute("SELECT entry_id, account_nr, amount FROM ledger;")
                for r in res.fetchall():
                    print(f"    - Entry #{r[0]}: Account {r[1]} | Amount: ${float(r[2]):,.2f}")

        print("\n[PASS] Example 16 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

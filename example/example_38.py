# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 38: High-Level Pushdown Query Extraction
Demonstrates using conn.extract() for concise storage-level filtering,
selective column projection, and result ordering.
"""

import os
import nemo

DB_NAME = "example38_extract_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 38: PUSHDOWN QUERY EXTRACTION")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating customer accounts table...")
                conn.execute("""
                    CREATE TABLE accounts (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        email VARCHAR NOT NULL,
                        tier VARCHAR NOT NULL,
                        score INTEGER NOT NULL
                    );
                """)

                users = [
                    ("alice@example.com", "enterprise", 95),
                    ("bob@example.com", "standard", 60),
                    ("charlie@example.com", "enterprise", 88),
                    ("diana@example.com", "pro", 78),
                    ("evan@example.com", "standard", 55),
                ]
                conn.executemany("INSERT INTO accounts (email, tier, score) VALUES (?, ?, ?);", users)
                print(f"    -> Ingested {len(users)} accounts.")

                print("\n[2] Executing conn.extract() with storage-level filter pushdown:")
                filtered_rows = conn.extract(
                    table="accounts",
                    columns=["email", "score"],
                    where="score >= 75",
                    limit=5
                )

                print(f"    -> Extracted {len(filtered_rows)} qualifying accounts:")
                for r in filtered_rows:
                    print(f"       - Account: {r[0]:<22} | Score: {r[1]}")

        print("\n[PASS] Example 38 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

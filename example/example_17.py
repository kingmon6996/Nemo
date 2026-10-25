# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 17: Text, Unicode & Raw Binary Blobs
Demonstrates CHAR, VARCHAR, TEXT, and BYTEA binary blobs for storing
cryptographic signatures, hashes, and internationalized Unicode.
"""

import os
import nemo

DB_NAME = "example17_blobs_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 17: TEXT & BINARY BLOBS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating table with text and raw BYTEA column...")
                conn.execute("""
                    CREATE TABLE binary_store (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        file_name VARCHAR NOT NULL,
                        sha256_hash BYTEA,
                        description TEXT
                    );
                """)

                print("\n[2] Inserting Unicode and binary payload...")
                payload = [
                    ("image.png", r"\x89504e470d0a1a0a", "High-res logo with metadata: Alpha / Beta / Gamma"),
                    ("archive.tar.gz", r"\x1f8b080000000000", "Gzip compressed archive: package data"),
                ]
                conn.executemany("INSERT INTO binary_store (file_name, sha256_hash, description) VALUES (?, ?, ?);", payload)

                print("\n[3] Reading and verifying binary store records:")
                res = conn.execute("SELECT id, file_name, sha256_hash, description FROM binary_store;")
                for r in res.fetchall():
                    print(f"    - File #{r[0]}: {r[1]:<16} | Hash: {r[2]} | Note: {r[3]}")

        print("\n[PASS] Example 17 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

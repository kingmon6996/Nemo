# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 19: Semi-Structured JSON & JSONB Documents
Demonstrates storing, indexing, and querying semi-structured documents
with typed JSON and binary JSONB columns.
"""

import os
import nemo

DB_NAME = "example19_json_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 19: JSON & JSONB DOCUMENTS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating document repository table...")
                conn.execute("""
                    CREATE TABLE customer_profiles (
                        user_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        username VARCHAR NOT NULL,
                        profile_doc JSON,
                        metadata_doc JSONB
                    );
                """)

                print("\n[2] Ingesting rich JSON and JSONB documents...")
                profiles = [
                    ("alice", '{"theme": "dark", "notifications": {"email": true, "sms": false}}', '{"plan": "enterprise", "credits": 500}'),
                    ("bob", '{"theme": "light", "notifications": {"email": false, "sms": true}}', '{"plan": "free", "credits": 50}'),
                ]
                conn.executemany("INSERT INTO customer_profiles (username, profile_doc, metadata_doc) VALUES (?, ?, ?);", profiles)

                print("\n[3] Reading and inspecting JSON documents:")
                res = conn.execute("SELECT user_id, username, profile_doc, metadata_doc FROM customer_profiles;")
                for r in res.fetchall():
                    print(f"    - User #{r[0]} ({r[1]}):")
                    print(f"        Profile Doc  : {r[2]}")
                    print(f"        Metadata Doc : {r[3]}")

        print("\n[PASS] Example 19 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

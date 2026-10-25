# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 20: Complex Collections: ARRAY & DICT
Demonstrates native ordered dynamic arrays and key-value dictionaries
directly embedded in table rows.
"""

import os
import nemo

DB_NAME = "example20_collections_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 20: ARRAY & DICT COLLECTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating table with ARRAY and DICT columns...")
                conn.execute("""
                    CREATE TABLE cluster_nodes (
                        node_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        node_name VARCHAR NOT NULL,
                        ports ARRAY,
                        config_tags DICT
                    );
                """)

                print("\n[2] Ingesting arrays and dictionary mappings...")
                records = [
                    ("worker-01", "[8080, 8081, 9092]", '{"zone": "us-east-1a", "instance": "c6g.xlarge"}'),
                    ("worker-02", "[8080, 8082, 9092]", '{"zone": "us-east-1b", "instance": "c6g.xlarge"}'),
                ]
                conn.executemany("INSERT INTO cluster_nodes (node_name, ports, config_tags) VALUES (?, ?, ?);", records)

                print("\n[3] Reading and verifying structured collections:")
                res = conn.execute("SELECT node_id, node_name, ports, config_tags FROM cluster_nodes;")
                for r in res.fetchall():
                    print(f"    - Node #{r[0]}: {r[1]:<12} | Ports: {r[2]} | Config: {r[3]}")

        print("\n[PASS] Example 20 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

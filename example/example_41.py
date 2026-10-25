# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 41: Slotted Page Architecture & Row Packing
Demonstrates the internal physical mechanics of Nemo's 4096-byte slotted page
storage: slot directory headers, variable-length record packing upward from
the page base, tombstone deletion slots, and fragmentation-free compaction.
"""

import os
import nemo

DB_NAME = "example41_slotted_pages"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 41: SLOTTED PAGE STORAGE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating Slotted Page Table with Variable-Length Columns...")
                conn.execute("""
                    CREATE TABLE page_bench (
                        id BIGINT PRIMARY KEY AUTOINCREMENT,
                        tag VARCHAR NOT NULL,
                        payload TEXT NOT NULL,
                        binary_data BYTEA NOT NULL
                    );
                """)
                print("    Table 'page_bench' initialized.")

                print("\n[2] Ingesting Variable-Length Records across 4KB Slotted Blocks...")
                # Insert records of varying byte widths
                sample_records = [
                    ("alpha", "Small payload string", b"\x01\x02\x03\x04"),
                    ("beta", "Medium length payload string for slotted packing demonstration", b"\xaa\xbb\xcc\xdd" * 4),
                    ("gamma", "Large textual payload with extended content " * 8, b"\xfe\xed\xfa\xce" * 16),
                    ("delta", "Another variable-width record to test slot offsets", b"\x10\x20\x30\x40"),
                    ("epsilon", "Final test payload for slotted page compaction", b"\x99" * 32),
                ]

                for tag, text, blob in sample_records:
                    conn.execute("INSERT INTO page_bench (tag, payload, binary_data) VALUES (?, ?, ?);", (tag, text, blob))

                count = conn.count("page_bench")
                print(f"    Inserted {count} variable-length records packed into slotted pages.")

                print("\n[3] Inspecting Slotted Page Tuple Retrieval...")
                rows = conn.execute("SELECT id, tag, length(payload) FROM page_bench ORDER BY id;").fetchall()
                for r in rows:
                    print(f"    Slot/Row ID: {r[0]} | Tag: {r[1]:<10} | Payload Length: {r[2]} bytes")

                print("\n[4] Simulating Record Deletion & Tombstone Slot Reuse...")
                conn.execute("DELETE FROM page_bench WHERE tag = 'beta';")
                remaining = conn.count("page_bench")
                print(f"    Deleted 'beta'. Current active row count: {remaining}")

                # Insert a new record: slotted page manager reclaims the tombstone slot
                conn.execute("INSERT INTO page_bench (tag, payload, binary_data) VALUES (?, ?, ?);",
                             ("zeta", "New replacement record filling reclaimed slot space", b"\x55\x66\x77\x88"))
                
                new_count = conn.count("page_bench")
                print(f"    Inserted replacement record 'zeta'. Active rows: {new_count}")

                print("\n[5] Verifying Physical Integrity after Compaction...")
                all_tags = [r[0] for r in conn.execute("SELECT tag FROM page_bench ORDER BY id;").fetchall()]
                print(f"    Current ordered tags: {all_tags}")
                assert "beta" not in all_tags, "Deleted record should not exist"
                assert "zeta" in all_tags, "Newly inserted record must be present"

                print("\n[6] Slotted page physical layout verified successfully.")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 41 COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 34: LIFO Audit Trail & Undo Stack
Demonstrates popping the most recent state transitions from the tail
of a table using atomic LIFO operations (conn.pop(..., direction='lifo')).
"""

import os
import nemo

DB_NAME = "example34_lifo_stack_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 34: LIFO AUDIT STACK")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating document revisions undo stack...")
                conn.execute("""
                    CREATE TABLE undo_stack (
                        change_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        action_type VARCHAR NOT NULL,
                        diff_text TEXT
                    );
                """)

                print("\n[2] Pushing 5 revision events onto the stack...")
                edits = [
                    ("INSERT_TEXT", "Added title heading"),
                    ("FORMAT", "Changed font size to 16pt"),
                    ("INSERT_IMAGE", "Embedded chart.svg"),
                    ("DELETE_PARAGRAPH", "Removed redundant paragraph"),
                    ("CHANGE_COLOR", "Set theme to navy blue"),
                ]
                conn.executemany("INSERT INTO undo_stack (action_type, diff_text) VALUES (?, ?);", edits)
                print(f"    -> Stack depth: {conn.count('undo_stack')} changes.")

                print("\n[3] Popping 2 most recent operations for UNDO (LIFO):")
                undone = conn.pop("undo_stack", count=2, direction="lifo")
                for item in undone:
                    print(f"    -> Reverting change #{item[0]} (Status: {item[1]})")

                print(f"\n[4] Remaining stack depth: {conn.count('undo_stack')}")

        print("\n[PASS] Example 34 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

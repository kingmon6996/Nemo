# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 18: Temporal Types & Timestamps
Demonstrates DATE, TIME, TIMESTAMP, TIMESTAMPTZ, and INTERVAL types
for time-series and event logging systems.
"""

import os
import nemo

DB_NAME = "example18_temporal_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 18: TEMPORAL TYPES")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating temporal event schedule schema...")
                conn.execute("""
                    CREATE TABLE event_schedule (
                        event_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        title VARCHAR NOT NULL,
                        event_date DATE,
                        event_time TIME,
                        created_at TIMESTAMP,
                        duration INTERVAL
                    );
                """)

                print("\n[2] Ingesting temporal records...")
                events = [
                    ("Quarterly Planning", "2026-11-01", "09:00:00", "2026-10-25T14:00:00Z", "02:30:00"),
                    ("Keynote Presentation", "2026-11-02", "14:30:00", "2026-10-25T14:05:00Z", "01:00:00"),
                ]
                conn.executemany("INSERT INTO event_schedule (title, event_date, event_time, created_at, duration) VALUES (?, ?, ?, ?, ?);", events)

                print("\n[3] Reading and verifying temporal records:")
                res = conn.execute("SELECT event_id, title, event_date, event_time, created_at, duration FROM event_schedule;")
                for r in res.fetchall():
                    print(f"    - Event #{r[0]}: {r[1]:<22} | Date: {r[2]} | Time: {r[3]} | Duration: {r[5]}")

        print("\n[PASS] Example 18 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

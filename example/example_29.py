# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 29: Window Functions & Analytical Ranking
Demonstrates calculating ROW_NUMBER() and RANK() partitioned or
ordered across table datasets.
"""

import os
import nemo

DB_NAME = "example29_window_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 29: WINDOW FUNCTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating player scores leaderboard table...")
                conn.execute("""
                    CREATE TABLE leaderboards (
                        player_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        gamertag VARCHAR NOT NULL,
                        score INTEGER NOT NULL
                    );
                """)

                players = [
                    ("Viper", 9850),
                    ("Apex", 12400),
                    ("Shadow", 7600),
                    ("Nova", 12400),
                    ("Ghost", 5100),
                ]
                conn.executemany("INSERT INTO leaderboards (gamertag, score) VALUES (?, ?);", players)
                print(f"    -> Ingested {len(players)} players.")

                print("\n[2] Executing Window Ranking Query:")
                window_query = """
                    SELECT gamertag, score,
                           ROW_NUMBER() OVER (ORDER BY score DESC) as pos,
                           RANK() OVER (ORDER BY score DESC) as leaderboard_rank
                    FROM leaderboards;
                """
                res = conn.execute(window_query)
                for r in res.fetchall():
                    print(f"    - Rank #{r[3]}: Player {r[0]:<10} | Score: {r[1]:>6} | Row: {r[2]}")

        print("\n[PASS] Example 29 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

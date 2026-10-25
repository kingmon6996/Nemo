# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Example 5: Analytical SQL, Hash Joins, SIMD Aggregation & CBO Optimizer
Demonstrates:
- High-speed In-Memory Hash Joins across multiple tables with selective column projection
- Vectorized SIMD Aggregations (AVX2/NEON) computing COUNT, SUM, AVG, MIN, MAX
- Window ranking functions: ROW_NUMBER() and RANK() OVER (ORDER BY)
- Cost-Based Optimizer (CBO) inspection via ANALYZE TABLE and EXPLAIN
"""

import os
import nemo

DB_NAME = "example5_analytics_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 5: HASH JOINS, SIMD & ANALYTICS")
    print("=" * 70)

    with nemo.connect(DB_NAME) as db:
        with db.connect_sync() as conn:

            # 1. Setup multi-table schema for joins
            print("\n[1] Creating relational tables: 'accounts' and 'orders'...")
            conn.execute("""
                CREATE TABLE IF NOT EXISTS accounts (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    username VARCHAR NOT NULL,
                    balance DOUBLE,
                    tier VARCHAR
                );
            """)
            conn.execute("""
                CREATE TABLE IF NOT EXISTS orders (
                    id BIGINT PRIMARY KEY AUTOINCREMENT,
                    account_id BIGINT,
                    symbol VARCHAR,
                    price DOUBLE,
                    qty INT
                );
            """)
            print("    -> Tables created.")

            # 2. Populate test dataset
            print("\n[2] Populating accounts and orders data...")
            accounts_data = [
                ("alice", 15000.0, "vip"),
                ("bob", 2400.0, "standard"),
                ("charlie", 38000.0, "enterprise"),
                ("diana", 7200.0, "standard"),
            ]
            conn.executemany("INSERT INTO accounts (username, balance, tier) VALUES (?, ?, ?);", accounts_data)

            orders_data = [
                (1, "AAPL", 185.50, 50),
                (1, "MSFT", 420.00, 20),
                (2, "GOOGL", 175.25, 15),
                (3, "NVDA", 120.00, 200),
                (3, "TSLA", 215.00, 40),
                (4, "AMZN", 180.10, 30),
            ]
            conn.executemany("INSERT INTO orders (account_id, symbol, price, qty) VALUES (?, ?, ?, ?);", orders_data)
            print(f"    -> Ingested {len(accounts_data)} accounts and {len(orders_data)} orders.")

            # 3. In-Memory Hash Join with Selective Column Projection
            print("\n[3] Executing In-Memory Hash Join (orders JOIN accounts):")
            join_res = conn.execute("""
                SELECT orders.id, accounts.username, orders.symbol, orders.price
                FROM orders
                JOIN accounts ON orders.account_id = accounts.id
                LIMIT 10;
            """)
            for row in join_res:
                print(f"    - Order #{row[0]}: User {row[1]:<10} bought {row[2]:<6} @ ${row[3]:.2f}")

            # 4. Vectorized SIMD Aggregations
            print("\n[4] Computing Vectorized SIMD Aggregates on 'accounts':")
            agg = conn.execute("""
                SELECT COUNT(*), SUM(balance), AVG(balance), MIN(balance), MAX(balance)
                FROM accounts;
            """).fetchone()
            print(f"    - Total Accounts : {agg[0]}")
            print(f"    - Total Balance  : ${agg[1]:,.2f}")
            print(f"    - Average Balance: ${agg[2]:,.2f}")
            print(f"    - Minimum Balance: ${agg[3]:,.2f}")
            print(f"    - Maximum Balance: ${agg[4]:,.2f}")

            # 5. Window Functions (ROW_NUMBER & RANK)
            print("\n[5] Calculating Window Ranking Functions:")
            win_res = conn.execute("""
                SELECT id, username, balance,
                       ROW_NUMBER() OVER (ORDER BY balance DESC) AS rn,
                       RANK() OVER (ORDER BY balance DESC) AS rk
                FROM accounts;
            """)
            for r in win_res:
                print(f"    - Rank #{r[3]}: {r[1]:<10} | Balance: ${r[2]:>9.2f} (ROW_NUMBER: {r[3]}, RANK: {r[4]})")

            # 6. Cost-Based Optimizer (CBO) & EXPLAIN
            print("\n[6] Cost-Based Optimizer Table Statistics & Physical Plan:")
            conn.analyze("accounts")
            plan = conn.explain("SELECT AVG(balance) FROM accounts WHERE balance > 5000;")
            print("    Generated Physical Plan:\n" + "\n".join("      " + line for line in plan.splitlines()))

    # Clean up local database file
    if os.path.exists(DB_FILE):
        try:
            os.remove(DB_FILE)
            print(f"\n[Cleanup] Cleaned up temporary database: {DB_FILE}")
        except OSError:
            pass

    print("\n[PASS] Example 5 completed successfully!\n")


if __name__ == "__main__":
    main()

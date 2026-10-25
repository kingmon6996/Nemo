# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 50: Comprehensive Master Architecture Showcase
The definitive showcase demonstrating Nemo's complete architectural suite:
multi-format schemas, connection pooling, ACID transactions, in-memory hash joins,
SIMD vector aggregations, atomic FIFO queue popping, direct columnar extraction,
and instantaneous table truncation in a single comprehensive workflow.
"""

import os
import nemo

DB_NAME = "example50_master_showcase"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 50: MASTER ARCHITECTURE SHOWCASE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            print("\n[1] Initializing Multi-Tenant Schemas and Sequences...")
            with db.connect_sync() as conn:
                # 1. Accounts
                conn.execute("""
                    CREATE TABLE accounts (
                        acc_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        username VARCHAR NOT NULL UNIQUE,
                        balance DOUBLE NOT NULL,
                        tags ARRAY NOT NULL,
                        profile JSON NOT NULL
                    );
                """)
                # 2. Activity logs
                conn.execute("""
                    CREATE TABLE activity_log (
                        log_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        acc_id BIGINT NOT NULL,
                        action VARCHAR NOT NULL,
                        amount DOUBLE NOT NULL
                    );
                """)
                # 3. High-velocity task queue
                conn.execute("""
                    CREATE TABLE task_queue (
                        task_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        payload VARCHAR NOT NULL
                    );
                """)
                # 4. Sequence
                conn.execute("CREATE SEQUENCE txn_ref_seq START WITH 5000 INCREMENT BY 5;")
                print("    Schemas and sequence 'txn_ref_seq' successfully initialized.")

            print("\n[2] Performing Ingestion across Synchronous Connection Pool...")
            pool = db.create_pool_sync(max_connections=4)
            with pool.acquire() as conn:
                accounts_data = [
                    ("alice", 1500.0, [1, 2, 3], '{"role": "admin", "tier": "gold"}'),
                    ("bob", 2400.0, [4, 5], '{"role": "user", "tier": "silver"}'),
                    ("charlie", 800.0, [1, 6], '{"role": "user", "tier": "bronze"}'),
                    ("diana", 4200.0, [2, 7, 8], '{"role": "lead", "tier": "platinum"}'),
                ]
                conn.executemany("""
                    INSERT INTO accounts (username, balance, tags, profile)
                    VALUES (?, ?, ?, ?);
                """, accounts_data)
                print(f"    Ingested {conn.count('accounts')} master account records.")

                # Ingest activity records
                activity_data = [
                    (1, "deposit", 500.0),
                    (1, "transfer_out", 100.0),
                    (2, "deposit", 1200.0),
                    (3, "payment", 50.0),
                    (4, "dividend", 350.0),
                    (4, "transfer_in", 100.0),
                ]
                conn.executemany("INSERT INTO activity_log (acc_id, action, amount) VALUES (?, ?, ?);", activity_data)
                print(f"    Ingested {conn.count('activity_log')} activity logs.")

            print("\n[3] Executing ACID Transaction Transfer with Savepoint Rollback...")
            with pool.acquire() as conn:
                with conn.transaction():
                    conn.execute("UPDATE accounts SET balance = balance - 300.0 WHERE username = 'alice';")
                    conn.execute("UPDATE accounts SET balance = balance + 300.0 WHERE username = 'bob';")
                    seq_ref = conn.execute("SELECT nextval('txn_ref_seq');").fetchone()[0]
                    conn.execute("INSERT INTO activity_log (acc_id, action, amount) VALUES (?, ?, ?);", (1, f"txn_ref_{seq_ref}", 300.0))
                print(f"    Committed $300 transfer from Alice to Bob (Seq Reference: {seq_ref}).")

            print("\n[4] Executing Relational In-Memory Hash Join...")
            with pool.acquire() as conn:
                join_sql = """
                    SELECT a.username, l.action, l.amount, a.balance
                    FROM activity_log l
                    JOIN accounts a ON l.acc_id = a.acc_id
                    WHERE l.amount >= 100.0
                    ORDER BY l.amount DESC;
                """
                matched = conn.execute(join_sql).fetchall()
                print(f"    Hash Join matched {len(matched)} high-value activity records:")
                for r in matched[:4]:
                    print(f"      User: {r[0]:<10} | Action: {r[1]:<16} | Amount: ${r[2]:,.2f} | Current Bal: ${r[3]:,.2f}")

            print("\n[5] Executing SIMD Vector Aggregations...")
            with pool.acquire() as conn:
                simd_stats = conn.execute("""
                    SELECT 
                        COUNT(balance),
                        SUM(balance),
                        AVG(balance),
                        MIN(balance),
                        MAX(balance)
                    FROM accounts;
                """).fetchone()
                print(f"    Account Capital: Total = ${simd_stats[1]:,.2f} | Mean = ${simd_stats[2]:,.2f} | Max = ${simd_stats[4]:,.2f}")

            print("\n[6] High-Velocity Atomic FIFO Queue Popping...")
            with pool.acquire() as conn:
                queue_items = [(f"queue_payload_task_{i}",) for i in range(1, 11)]
                conn.executemany("INSERT INTO task_queue (payload) VALUES (?);", queue_items)
                print(f"    Enqueued {conn.count('task_queue')} tasks.")

                # Atomically pop 5 tasks from head (FIFO)
                popped = conn.pop("task_queue", count=5, direction="fifo")
                print(f"    Atomically popped {len(popped)} tasks in a single lock-free operation:")
                for task_id, payload in popped:
                    print(f"      Popped Task ID: {task_id} -> {payload}")
                print(f"    Remaining queue tasks: {conn.count('task_queue')}")

            print("\n[7] Direct High-Speed Columnar Analytical Extraction...")
            with pool.acquire() as conn:
                col_data = conn.extract_columnar("accounts", ["acc_id", "username", "balance"])
                print("    Direct Columnar Arrays extracted:")
                print(f"      Usernames: {col_data.get('username')}")
                print(f"      Balances:  {col_data.get('balance')}")

            print("\n[8] Instantaneous O(1) Table Truncation...")
            with pool.acquire() as conn:
                deleted_rows = conn.truncate("task_queue")
                print(f"    Instantly truncated 'task_queue' in O(1) time. Remaining rows: {conn.count('task_queue')}")

            pool.close()
            print("\n[9] Pool cleanly released.")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 50 MASTER SHOWCASE COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

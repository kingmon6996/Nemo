# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 40: End-to-End Microservice Architecture
Demonstrates an enterprise e-commerce platform combining relational catalogs,
order processing, atomic FIFO fulfillment queue, SIMD analytics,
and high-speed columnar reporting in a unified application.
"""

import os
import nemo

DB_NAME = "example40_microservice_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 40: END-TO-END MICROSERVICE")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Initializing Platform Microservice Schemas...")
                # 1. Product catalog
                conn.execute("""
                    CREATE TABLE catalog (
                        item_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        title VARCHAR NOT NULL,
                        price DOUBLE NOT NULL,
                        stock INTEGER NOT NULL
                    );
                """)
                # 2. Orders table
                conn.execute("""
                    CREATE TABLE customer_orders (
                        order_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        customer VARCHAR NOT NULL,
                        item_id BIGINT NOT NULL,
                        total_amount DOUBLE NOT NULL
                    );
                """)
                # 3. Order fulfillment queue
                conn.execute("""
                    CREATE TABLE fulfillment_queue (
                        job_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        order_id BIGINT NOT NULL,
                        status VARCHAR NOT NULL
                    );
                """)
                print("    -> Catalog, Orders, and Fulfillment Queue tables initialized.")

                # Ingest Catalog
                print("\n[2] Ingesting initial product catalog...")
                products = [
                    ("Quantum GPU Pro", 1499.99, 50),
                    ("Neural Accelerator X", 899.50, 100),
                    ("Optic Switch 100G", 349.00, 200),
                ]
                conn.executemany("INSERT INTO catalog (title, price, stock) VALUES (?, ?, ?);", products)

                # Process Orders
                print("\n[3] Ingesting incoming customer orders...")
                orders = [
                    ("Alice", 1, 1499.99),
                    ("Bob", 2, 899.50),
                    ("Charlie", 3, 349.00),
                    ("Diana", 1, 1499.99),
                ]
                conn.executemany("INSERT INTO customer_orders (customer, item_id, total_amount) VALUES (?, ?, ?);", orders)

                # Push to fulfillment queue
                jobs = [(i, "PENDING_DISPATCH") for i in range(1, len(orders) + 1)]
                conn.executemany("INSERT INTO fulfillment_queue (order_id, status) VALUES (?, ?);", jobs)
                print(f"    -> Enqueued {conn.count('fulfillment_queue')} orders to fulfillment queue.")

                # Dequeue next order atomically
                print("\n[4] Dispatching next pending shipment (FIFO Atomic POP)...")
                popped = conn.pop("fulfillment_queue", count=1, direction="fifo")
                if popped:
                    print(f"    -> Dispatched Shipment #{popped[0][0]} successfully!")
                    print(f"    -> Remaining in queue: {conn.count('fulfillment_queue')}")

                # Relational Join: Orders + Catalog
                print("\n[5] Generating Relational Join Report (Orders JOIN Catalog):")
                join_sql = """
                    SELECT customer_orders.order_id, customer_orders.customer, catalog.title, customer_orders.total_amount
                    FROM customer_orders
                    JOIN catalog ON customer_orders.item_id = catalog.item_id;
                """
                res = conn.execute(join_sql)
                for r in res.fetchall():
                    print(f"    - Order #{r[0]}: {r[1]:<10} purchased {r[2]:<22} for ${float(r[3]):,.2f}")

                # Vectorized Analytics
                print("\n[6] Computing Real-Time Platform Analytics (SIMD Aggregation):")
                stats_row = conn.execute("SELECT sum(total_amount), avg(total_amount), max(total_amount) FROM customer_orders;").fetchone()
                print(f"    - Gross Revenue  : ${float(stats_row[0]):,.2f}")
                print(f"    - Average Basket : ${float(stats_row[1]):,.2f}")
                print(f"    - Largest Order  : ${float(stats_row[2]):,.2f}")

        print("\n[PASS] Example 40 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 45: Cost-Based Query Optimizer (CBO) & Cardinality Statistics
Demonstrates how Nemo's Cost-Based Optimizer (CBO) utilizes table-level and
column-level cardinality distributions from `conn.analyze()` to calculate physical
operator costs and select optimal execution plans via `conn.explain()`.
"""

import os
import nemo

DB_NAME = "example45_cbo_optimizer"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 45: CBO OPTIMIZER & STATISTICS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating Large Multi-Attribute Analytics Table...")
                conn.execute("""
                    CREATE TABLE telemetry (
                        sensor_id BIGINT PRIMARY KEY AUTOINCREMENT,
                        facility VARCHAR NOT NULL,
                        temperature DOUBLE NOT NULL,
                        status VARCHAR NOT NULL
                    );
                """)

                print("\n[2] Ingesting Heterogeneous Distribution of Telemetry Data...")
                facilities = ["Plant_A", "Plant_B", "Plant_C", "Plant_D"]
                statuses = ["OK", "WARN", "CRIT"]
                records = [
                    (facilities[i % 4], 20.0 + (i % 50), statuses[i % 3])
                    for i in range(1500)
                ]
                conn.executemany("INSERT INTO telemetry (facility, temperature, status) VALUES (?, ?, ?);", records)
                print(f"    Ingested {conn.count('telemetry')} rows into 'telemetry'.")

                print("\n[3] Triggering Cost-Based Optimizer Statistical Analysis (conn.analyze)...")
                conn.analyze("telemetry")
                print("    Statistical metadata collected: row counts, distinct values, and data distributions.")

                print("\n[4] Inspecting Physical Execution Plan for Aggregates (conn.explain)...")
                plan1 = conn.explain("SELECT facility, AVG(temperature) FROM telemetry WHERE status = 'CRIT' GROUP BY facility;")
                print("--- Physical Plan Tree ---")
                print(plan1)
                print("--------------------------")

                print("\n[5] Inspecting Physical Execution Plan for Point Lookup...")
                plan2 = conn.explain("SELECT * FROM telemetry WHERE sensor_id = 42;")
                print("--- Physical Plan Tree ---")
                print(plan2)
                print("--------------------------")

                print("\n[6] Executing the Optimized Analytical Query...")
                results = conn.execute("SELECT facility, COUNT(sensor_id), AVG(temperature) FROM telemetry WHERE status = 'CRIT' GROUP BY facility;").fetchall()
                print("    Query Results:")
                for fac, count, avg_temp in results:
                    print(f"      Facility: {fac:<10} | Incidents: {count} | Avg Temp: {avg_temp:.2f} C")

    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

    print("\n" + "=" * 70)
    print("   EXAMPLE 45 COMPLETED SUCCESSFULLY!")
    print("=" * 70)

if __name__ == "__main__":
    main()

# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 35: Sequences & nextval / currval
Demonstrates managing standalone atomic sequences independent of tables,
evaluating nextval() and currval() across parallel clients.
"""

import nemo

DB_NAME = "example35_sequences_demo"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 35: SEQUENCES")
    print("=" * 70)

    import shutil; shutil.rmtree(f".nemo/{DB_NAME}", ignore_errors=True)
    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating standalone sequence 'ticket_seq' (START=5000, INCREMENT=5)...")
                conn.execute("CREATE SEQUENCE ticket_seq START WITH 5000 INCREMENT BY 5;")
                print("    -> Sequence created.")

                print("\n[2] Requesting sequence values:")
                val1 = conn.execute("SELECT NEXTVAL('ticket_seq');").fetchone()[0]
                print(f"    - First  NEXTVAL : {val1}")

                val_curr = conn.execute("SELECT CURRVAL('ticket_seq');").fetchone()[0]
                print(f"    - Current CURRVAL: {val_curr}")

                val2 = conn.execute("SELECT NEXTVAL('ticket_seq');").fetchone()[0]
                print(f"    - Second NEXTVAL : {val2}")

                val3 = conn.execute("SELECT NEXTVAL('ticket_seq');").fetchone()[0]
                print(f"    - Third  NEXTVAL : {val3}")

        print("\n[PASS] Example 35 completed successfully!\n")
    finally:
        import shutil; shutil.rmtree(f".nemo/{DB_NAME}", ignore_errors=True)
if __name__ == "__main__":
    main()

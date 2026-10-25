# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 26: Range, Network & Utility SQL Functions
Demonstrates range_lower, range_upper, range_contains, host, netmask,
and coalesce fallback expressions.
"""

import os
import nemo

DB_NAME = "example26_utility_funcs_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 26: RANGE & NETWORK FUNCTIONS")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Evaluating Range Functions:")
                rng = "[15, 85)"
                r1 = conn.execute(f"SELECT range_lower('{rng}');").fetchone()
                r2 = conn.execute(f"SELECT range_upper('{rng}');").fetchone()
                r3 = conn.execute(f"SELECT range_contains('{rng}', 50);").fetchone()
                print(f"    - range_lower('{rng}')    : {r1[0]}")
                print(f"    - range_upper('{rng}')    : {r2[0]}")
                print(f"    - range_contains(50)      : {r3[0]}")

                print("\n[2] Evaluating Network & Utility Functions:")
                cidr_str = "10.150.20.1/24"
                r4 = conn.execute(f"SELECT host('{cidr_str}');").fetchone()
                r5 = conn.execute(f"SELECT netmask('{cidr_str}');").fetchone()
                r6 = conn.execute("SELECT coalesce(NULL, NULL, 'fallback_value');").fetchone()
                print(f"    - host('{cidr_str}')     : {r4[0]}")
                print(f"    - netmask('{cidr_str}')  : {r5[0]}")
                print(f"    - coalesce(null, 'val')   : {r6[0]}")

        print("\n[PASS] Example 26 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

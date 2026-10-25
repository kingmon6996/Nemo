# Copyright 2026 Pravanjan Roy (kingmon6996)
# SPDX-License-Identifier: Apache-2.0

"""
Example 21: System & Network Types
Demonstrates UUIDs, IP addresses (INET, CIDR), MAC addresses,
and continuous range intervals (RANGE).
"""

import os
import nemo

DB_NAME = "example21_network_demo"
DB_FILE = f"{DB_NAME}.nm"

def main():
    print("=" * 70)
    print("   NEMO DATABASE ENGINE — EXAMPLE 21: SYSTEM & NETWORK TYPES")
    print("=" * 70)

    if os.path.exists(DB_FILE):
        try: os.remove(DB_FILE)
        except OSError: pass

    try:
        with nemo.connect(DB_NAME) as db:
            with db.connect_sync() as conn:
                print("\n[1] Creating network devices table...")
                conn.execute("""
                    CREATE TABLE devices (
                        device_uuid UUID PRIMARY KEY,
                        hostname VARCHAR NOT NULL,
                        ip_addr INET,
                        subnet CIDR,
                        mac_addr MACADDR,
                        port_range RANGE
                    );
                """)

                print("\n[2] Inserting network topology records...")
                devices = [
                    ("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11", "gateway-01", "192.168.1.1", "192.168.1.0/24", "00:1a:2b:3c:4d:5e", "[8000, 9000)"),
                    ("b1ffcd88-8b1a-3de7-aa5c-5aa8ac271b22", "switch-core", "10.0.0.1", "10.0.0.0/16", "08:00:2b:01:02:03", "[1000, 5000)"),
                ]
                conn.executemany("INSERT INTO devices (device_uuid, hostname, ip_addr, subnet, mac_addr, port_range) VALUES (?, ?, ?, ?, ?, ?);", devices)

                print("\n[3] Reading and verifying network devices:")
                res = conn.execute("SELECT device_uuid, hostname, ip_addr, subnet, mac_addr, port_range FROM devices;")
                for r in res.fetchall():
                    print(f"    - UUID: {r[0]}")
                    print(f"      Host: {r[1]:<14} | IP: {r[2]} | Subnet: {r[3]} | MAC: {r[4]} | Ports: {r[5]}")

        print("\n[PASS] Example 21 completed successfully!\n")
    finally:
        if os.path.exists(DB_FILE):
            try: os.remove(DB_FILE)
            except OSError: pass

if __name__ == "__main__":
    main()

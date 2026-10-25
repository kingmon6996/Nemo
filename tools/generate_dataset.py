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

import argparse
import random
import uuid

def generate_csv(rows, filename):
    with open(filename, 'w') as f:
        f.write("id,name,age,email,is_active\n")
        for i in range(1, rows + 1):
            name = f"User_{i}_{random.randint(100, 999)}"
            age = random.randint(18, 90)
            email = f"user{i}@example.com"
            active = random.choice(["TRUE", "FALSE"])
            f.write(f"{i},{name},{age},{email},{active}\n")
            
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rows", type=int, default=1000)
    parser.add_argument("--out", type=str, default="dataset.csv")
    args = parser.parse_args()
    generate_csv(args.rows, args.out)
    print(f"Generated {args.rows} rows to {args.out}")

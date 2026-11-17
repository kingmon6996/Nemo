import nemo
import os
import sys
import shutil

def test_group_by_having():
    print("==================================================")
    print("Testing 7.1.1: Group By, Having, Aggregations & Pagination")
    print("==================================================")

    db_dir = os.path.join(".nemo", "test_gbh_db")
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

    for ext in [".nm", ".nm.meta"]:
        p = "test_gbh_db" + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db_inst = nemo.Database("test_gbh_db")
    db = db_inst.connect_sync()

    # Create table
    db.execute("CREATE TABLE orders (id INT, customer TEXT, status TEXT, total DOUBLE);")
    db.execute("INSERT INTO orders VALUES (1, 'Alice', 'completed', 150.0);")
    db.execute("INSERT INTO orders VALUES (2, 'Bob', 'completed', 200.0);")
    db.execute("INSERT INTO orders VALUES (3, 'Alice', 'pending', 50.0);")
    db.execute("INSERT INTO orders VALUES (4, 'Charlie', 'completed', 300.0);")
    db.execute("INSERT INTO orders VALUES (5, 'Bob', 'completed', 250.0);")
    db.execute("INSERT INTO orders VALUES (6, 'Alice', 'completed', 100.0);")
    db.execute("INSERT INTO orders VALUES (7, 'David', 'cancelled', 75.0);")

    # 1. Single Key Group By with Aggregations
    print("[1] Single-key GROUP BY with SUM, COUNT, MIN, MAX, AVG...")
    res = db.execute("SELECT customer, count(*), sum(total), min(total), max(total) FROM orders GROUP BY customer;")
    rows = {r[0]: (r[1], r[2], r[3], r[4]) for r in res}
    assert rows['Alice'] == (3, 300.0, 50.0, 150.0)
    assert rows['Bob'] == (2, 450.0, 200.0, 250.0)
    assert rows['Charlie'] == (1, 300.0, 300.0, 300.0)
    assert rows['David'] == (1, 75.0, 75.0, 75.0)
    print(" PASS: Single-key GROUP BY")

    # 2. Multi-column Group By
    print("[2] Multi-column GROUP BY (customer, status)...")
    res = db.execute("SELECT customer, status, sum(total), count(*) FROM orders GROUP BY customer, status;")
    multi_rows = {(r[0], r[1]): (r[2], r[3]) for r in res}
    assert multi_rows[('Alice', 'completed')] == (250.0, 2)
    assert multi_rows[('Alice', 'pending')] == (50.0, 1)
    assert multi_rows[('Bob', 'completed')] == (450.0, 2)
    assert multi_rows[('Charlie', 'completed')] == (300.0, 1)
    assert multi_rows[('David', 'cancelled')] == (75.0, 1)
    print(" PASS: Multi-column GROUP BY")

    # 3. HAVING filter on COUNT and SUM
    print("[3] GROUP BY with HAVING clause...")
    res_having = db.execute("SELECT customer, sum(total) FROM orders GROUP BY customer HAVING count(*) > 1;")
    having_dict = {r[0]: r[1] for r in res_having}
    assert set(having_dict.keys()) == {'Alice', 'Bob'}

    res_having2 = db.execute("SELECT customer, sum(total) FROM orders GROUP BY customer HAVING sum(total) >= 300;")
    having_dict2 = {r[0]: r[1] for r in res_having2}
    assert set(having_dict2.keys()) == {'Alice', 'Bob', 'Charlie'}
    print(" PASS: HAVING clause")

    # 4. SELECT DISTINCT
    print("[4] SELECT DISTINCT...")
    res_distinct = db.execute("SELECT DISTINCT status FROM orders;")
    statuses = sorted([r[0] for r in res_distinct])
    assert statuses == ['cancelled', 'completed', 'pending']
    print(" PASS: SELECT DISTINCT")

    # 5. Pagination: LIMIT / OFFSET & FETCH FIRST
    print("[5] Pagination (OFFSET / FETCH FIRST)...")
    res_page = db.execute("SELECT id FROM orders LIMIT 3 OFFSET 2;")
    assert [r[0] for r in res_page] == [3, 4, 5]

    res_ff = db.execute("SELECT id FROM orders OFFSET 1 ROWS FETCH FIRST 2 ROWS ONLY;")
    assert [r[0] for r in res_ff] == [2, 3]
    print(" PASS: Pagination")

    print("ALL GROUP BY & HAVING TESTS PASSED!")
    db_inst.close()
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

if __name__ == "__main__":
    test_group_by_having()


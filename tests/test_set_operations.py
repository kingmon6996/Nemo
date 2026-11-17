import nemo
import os
import sys
import shutil

def test_set_operations():
    print("==================================================")
    print("Testing 7.1.3: Set Operations (UNION, UNION ALL, INTERSECT, EXCEPT)")
    print("==================================================")

    db_dir = os.path.join(".nemo", "test_sets_db")
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

    for ext in [".nm", ".nm.meta"]:
        p = "test_sets_db" + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db_inst = nemo.Database("test_sets_db")
    db = db_inst.connect_sync()

    db.execute("CREATE TABLE inventory_a (item TEXT, qty INT);")
    db.execute("INSERT INTO inventory_a VALUES ('Apple', 10);")
    db.execute("INSERT INTO inventory_a VALUES ('Banana', 20);")
    db.execute("INSERT INTO inventory_a VALUES ('Cherry', 30);")

    db.execute("CREATE TABLE inventory_b (item TEXT, qty INT);")
    db.execute("INSERT INTO inventory_b VALUES ('Banana', 20);")
    db.execute("INSERT INTO inventory_b VALUES ('Cherry', 30);")
    db.execute("INSERT INTO inventory_b VALUES ('Date', 40);")

    # 1. UNION ALL (Preserves duplicates)
    print("[1] UNION ALL...")
    res_uall = db.execute("SELECT item FROM inventory_a UNION ALL SELECT item FROM inventory_b;")
    items_uall = [r[0] for r in res_uall]
    assert len(items_uall) == 6
    assert items_uall.count('Banana') == 2
    assert items_uall.count('Cherry') == 2
    print(" PASS: UNION ALL")

    # 2. UNION (Deduplicated)
    print("[2] UNION...")
    res_union = db.execute("SELECT item FROM inventory_a UNION SELECT item FROM inventory_b;")
    items_union = sorted([r[0] for r in res_union])
    assert items_union == ['Apple', 'Banana', 'Cherry', 'Date']
    print(" PASS: UNION")

    # 3. INTERSECT (Set intersection)
    print("[3] INTERSECT...")
    res_inter = db.execute("SELECT item FROM inventory_a INTERSECT SELECT item FROM inventory_b;")
    items_inter = sorted([r[0] for r in res_inter])
    assert items_inter == ['Banana', 'Cherry']
    print(" PASS: INTERSECT")

    # 4. EXCEPT (Set difference)
    print("[4] EXCEPT...")
    res_except = db.execute("SELECT item FROM inventory_a EXCEPT SELECT item FROM inventory_b;")
    items_except = [r[0] for r in res_except]
    assert items_except == ['Apple']
    print(" PASS: EXCEPT")

    print("ALL SET OPERATIONS TESTS PASSED!")
    db_inst.close()
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

if __name__ == "__main__":
    test_set_operations()


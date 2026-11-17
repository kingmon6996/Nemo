import nemo
import os
import sys
import shutil

def test_savepoints():
    print("==================================================")
    print("Testing 7.1.6: Microsecond Savepoints & Nested Transactions")
    print("==================================================")

    db_dir = os.path.join(".nemo", "test_sp_db")
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

    for ext in [".nm", ".nm.meta"]:
        p = "test_sp_db" + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db_inst = nemo.Database("test_sp_db")
    db = db_inst.connect_sync()

    db.execute("CREATE TABLE accounts (id INT PRIMARY KEY, name TEXT, balance DOUBLE);")
    db.execute("INSERT INTO accounts VALUES (1, 'Alice', 1000.0);")
    db.execute("INSERT INTO accounts VALUES (2, 'Bob', 500.0);")

    # 1. Basic SAVEPOINT and ROLLBACK TO SAVEPOINT
    print("[1] SAVEPOINT and ROLLBACK TO SAVEPOINT...")
    db.execute("BEGIN;")
    db.execute("INSERT INTO accounts VALUES (3, 'Charlie', 250.0);")
    db.execute("SAVEPOINT sp1;")
    db.execute("INSERT INTO accounts VALUES (4, 'David', 100.0);")
    assert db.execute("SELECT count(*) FROM accounts;").fetchall()[0][0] == 4

    db.execute("ROLLBACK TO SAVEPOINT sp1;")
    assert db.execute("SELECT count(*) FROM accounts;").fetchall()[0][0] == 3
    names = {r[1] for r in db.execute("SELECT id, name FROM accounts;").fetchall()}
    assert 'David' not in names
    assert 'Charlie' in names
    db.execute("COMMIT;")
    print(" PASS: Basic savepoint rollback")

    # 2. Nested Multi-Level Savepoints
    print("[2] Nested Savepoints (level1 -> level2 -> level3)...")
    db.execute("BEGIN;")
    db.execute("SAVEPOINT l1;")
    db.execute("INSERT INTO accounts VALUES (5, 'Eve', 1200.0);")

    db.execute("SAVEPOINT l2;")
    db.execute("INSERT INTO accounts VALUES (6, 'Frank', 800.0);")

    db.execute("SAVEPOINT l3;")
    db.execute("INSERT INTO accounts VALUES (7, 'Grace', 950.0);")

    # Rollback to l3
    db.execute("ROLLBACK TO SAVEPOINT l3;")
    names = {r[1] for r in db.execute("SELECT id, name FROM accounts;").fetchall()}
    assert 'Grace' not in names
    assert 'Frank' in names

    # Rollback to l1
    db.execute("ROLLBACK TO SAVEPOINT l1;")
    names = {r[1] for r in db.execute("SELECT id, name FROM accounts;").fetchall()}
    assert 'Eve' not in names
    db.execute("COMMIT;")
    print(" PASS: Nested savepoints")

    # 3. RELEASE SAVEPOINT
    print("[3] RELEASE SAVEPOINT...")
    db.execute("BEGIN;")
    db.execute("INSERT INTO accounts VALUES (8, 'Henry', 300.0);")
    db.execute("SAVEPOINT sp_rel;")
    db.execute("INSERT INTO accounts VALUES (9, 'Isabella', 450.0);")
    db.execute("RELEASE SAVEPOINT sp_rel;")
    db.execute("COMMIT;")
    names = {r[1] for r in db.execute("SELECT id, name FROM accounts;").fetchall()}
    assert 'Henry' in names
    assert 'Isabella' in names
    print(" PASS: RELEASE SAVEPOINT")

    # 4. Transaction context manager with savepoint
    print("[4] Python Transaction context manager savepoint...")
    with db.transaction() as tx:
        db.execute("INSERT INTO accounts VALUES (10, 'Jack', 400.0);")
        tx.savepoint("temp_sp")
        db.execute("INSERT INTO accounts VALUES (11, 'Karen', 600.0);")
        tx.rollback_to("temp_sp")
        tx.release_savepoint("temp_sp")

    names = {r[1] for r in db.execute("SELECT id, name FROM accounts;").fetchall()}
    assert 'Jack' in names
    assert 'Karen' not in names
    print(" PASS: Python Context Manager Savepoint")

    print("ALL SAVEPOINTS TESTS PASSED!")
    db_inst.close()
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

if __name__ == "__main__":
    test_savepoints()


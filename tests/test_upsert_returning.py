import nemo
import os
import sys
import shutil

def test_upsert_returning():
    print("==================================================")
    print("Testing 7.1.4: Extended DML, UPSERT & RETURNING")
    print("==================================================")

    db_dir = os.path.join(".nemo", "test_upsert_db")
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

    for ext in [".nm", ".nm.meta"]:
        p = "test_upsert_db" + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db_inst = nemo.Database("test_upsert_db")
    try:
        db = db_inst.connect_sync()

        # 1. INSERT INTO ... SELECT
        print("[1] INSERT INTO ... SELECT...")
        db.execute("CREATE TABLE source_items (id INT, name TEXT, price DOUBLE);")
        db.execute("INSERT INTO source_items VALUES (1, 'Widget', 19.99);")
        db.execute("INSERT INTO source_items VALUES (2, 'Gadget', 49.99);")

        db.execute("CREATE TABLE archive_items (id INT, name TEXT, price DOUBLE);")
        res = db.execute("INSERT INTO archive_items SELECT * FROM source_items;")
        assert res.affected_rows == 2
        res_arch = db.execute("SELECT name FROM archive_items ORDER BY id;")
        assert [r[0] for r in res_arch] == ['Widget', 'Gadget']
        print(" PASS: INSERT INTO ... SELECT")

        # 2. INSERT ... RETURNING
        print("[2] INSERT ... RETURNING * and specific cols...")
        db.execute("CREATE TABLE accounts (id INT PRIMARY KEY, email TEXT, balance DOUBLE);")
        res_ret = db.execute("INSERT INTO accounts VALUES (10, 'alice@example.com', 500.0) RETURNING *;")
        assert res_ret.has_rows
        rows = res_ret.fetchall()
        assert rows[0] == (10, 'alice@example.com', 500.0)

        res_ret2 = db.execute("INSERT INTO accounts VALUES (20, 'bob@example.com', 300.0) RETURNING email, balance;")
        assert res_ret2.has_rows
        assert res_ret2.fetchall()[0] == ('bob@example.com', 300.0)
        print(" PASS: INSERT ... RETURNING")

        # 3. ON CONFLICT DO NOTHING
        print("[3] ON CONFLICT (id) DO NOTHING...")
        res_nothing = db.execute("INSERT INTO accounts VALUES (10, 'duplicate@example.com', 999.0) ON CONFLICT (id) DO NOTHING;")
        assert res_nothing.affected_rows == 0
        cur_email = db.execute("SELECT email FROM accounts WHERE id = 10;").fetchall()[0][0]
        assert cur_email == 'alice@example.com'
        print(" PASS: ON CONFLICT DO NOTHING")

        # 4. ON CONFLICT DO UPDATE SET (UPSERT)
        print("[4] ON CONFLICT (id) DO UPDATE SET balance = EXCLUDED.balance...")
        res_upsert = db.execute("INSERT INTO accounts VALUES (10, 'alice@newdomain.com', 750.0) ON CONFLICT (id) DO UPDATE SET balance = EXCLUDED.balance, email = EXCLUDED.email RETURNING id, email, balance;")
        assert res_upsert.has_rows
        updated_row = res_upsert.fetchall()[0]
        assert updated_row == (10, 'alice@newdomain.com', 750.0)
        print(" PASS: ON CONFLICT DO UPDATE SET (UPSERT)")

        # 5. UPDATE ... RETURNING
        print("[5] UPDATE ... RETURNING...")
        res_upd = db.execute("UPDATE accounts SET balance = 1000.0 WHERE id = 20 RETURNING id, balance;")
        assert res_upd.fetchall()[0] == (20, 1000.0)
        print(" PASS: UPDATE ... RETURNING")

        # 6. DELETE ... RETURNING
        print("[6] DELETE ... RETURNING...")
        res_del = db.execute("DELETE FROM accounts WHERE id = 20 RETURNING id, email;")
        assert res_del.fetchall()[0] == (20, 'bob@example.com')
        print(" PASS: DELETE ... RETURNING")

        print("ALL UPSERT & RETURNING TESTS PASSED!")
    finally:
        db_inst.close()
        if os.path.exists(db_dir):
            shutil.rmtree(db_dir, ignore_errors=True)

if __name__ == "__main__":
    test_upsert_returning()


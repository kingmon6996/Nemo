import nemo
import os
import sys
import shutil

def test_alter_table():
    print("==================================================")
    print("Testing 7.1.5: Zero-Copy Schema Evolution, Views & Fast Indexing")
    print("==================================================")

    db_dir = os.path.join(".nemo", "test_alter_db")
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

    for ext in [".nm", ".nm.meta"]:
        p = "test_alter_db" + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db_inst = nemo.Database("test_alter_db")
    db = db_inst.connect_sync()

    # 1. ALTER TABLE ADD COLUMN
    print("[1] ALTER TABLE ADD COLUMN [IF NOT EXISTS] and DEFAULT...")
    db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT);")
    db.execute("INSERT INTO users VALUES (1, 'Alice');")
    db.execute("INSERT INTO users VALUES (2, 'Bob');")

    db.execute("ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT DEFAULT 'standard';")
    res = db.execute("SELECT id, name, role FROM users;")
    rows = res.fetchall()
    assert len(rows) == 2
    assert rows[0] == (1, 'Alice', 'standard')
    assert rows[1] == (2, 'Bob', 'standard')

    # Idempotent re-add
    db.execute("ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT DEFAULT 'standard';")
    print(" PASS: ADD COLUMN")

    # 2. ALTER TABLE RENAME COLUMN
    print("[2] ALTER TABLE RENAME COLUMN...")
    db.execute("ALTER TABLE users RENAME COLUMN role TO user_role;")
    res = db.execute("SELECT id, user_role FROM users WHERE id = 1;")
    assert res.fetchall()[0] == (1, 'standard')
    print(" PASS: RENAME COLUMN")

    # 3. ALTER TABLE DROP COLUMN
    print("[3] ALTER TABLE DROP COLUMN [IF EXISTS]...")
    db.execute("ALTER TABLE users DROP COLUMN IF EXISTS user_role;")
    res = db.execute("SELECT * FROM users;")
    assert [c.name for c in res.columns] == ['id', 'name']
    db.execute("ALTER TABLE users DROP COLUMN IF EXISTS nonexistent_col;")
    print(" PASS: DROP COLUMN")

    # 4. ALTER TABLE RENAME TO
    print("[4] ALTER TABLE RENAME TO...")
    db.execute("ALTER TABLE users RENAME TO members;")
    res = db.execute("SELECT count(*) FROM members;")
    assert res.fetchall()[0][0] == 2
    print(" PASS: RENAME TABLE")

    # 5. CREATE VIEW / DROP VIEW
    print("[5] CREATE / REPLACE / DROP VIEW...")
    db.execute("CREATE TABLE products (id INT, title TEXT, price DOUBLE, active BOOL);")
    db.execute("INSERT INTO products VALUES (1, 'Phone', 800.0, true);")
    db.execute("INSERT INTO products VALUES (2, 'Cable', 20.0, true);")
    db.execute("INSERT INTO products VALUES (3, 'Case', 15.0, false);")

    db.execute("CREATE VIEW live_products AS SELECT id, title, price FROM products WHERE active = true;")
    res_view = db.execute("SELECT * FROM live_products WHERE price > 50;")
    rows_view = res_view.fetchall()
    assert len(rows_view) == 1
    assert rows_view[0] == (1, 'Phone', 800.0)

    db.execute("CREATE OR REPLACE VIEW live_products AS SELECT id, title FROM products WHERE active = true;")
    res_view2 = db.execute("SELECT * FROM live_products;")
    assert len(res_view2.fetchall()) == 2

    db.execute("DROP VIEW IF EXISTS live_products;")
    print(" PASS: VIEWS")

    # 6. Secondary Indexes
    print("[6] CREATE INDEX [IF NOT EXISTS] and DROP INDEX...")
    db.execute("CREATE INDEX idx_products_title ON products (title);")
    db.execute("CREATE INDEX IF NOT EXISTS idx_products_title ON products (title);")
    db.execute("DROP INDEX IF EXISTS idx_products_title;")
    print(" PASS: INDEXES")

    print("ALL ALTER TABLE & VIEWS TESTS PASSED!")
    db_inst.close()
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

if __name__ == "__main__":
    test_alter_table()


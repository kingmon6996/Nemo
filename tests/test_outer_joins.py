import nemo
import os
import sys
import shutil

def test_outer_joins():
    print("==================================================")
    print("Testing 7.1.2: Cache-Conscious Joins (INNER, LEFT, RIGHT, FULL, CROSS)")
    print("==================================================")

    db_dir = os.path.join(".nemo", "test_joins_db")
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

    for ext in [".nm", ".nm.meta"]:
        p = "test_joins_db" + ext
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    db_inst = nemo.Database("test_joins_db")
    db = db_inst.connect_sync()

    db.execute("CREATE TABLE teams (team_id INT, team_name TEXT);")
    db.execute("INSERT INTO teams VALUES (1, 'Alpha');")
    db.execute("INSERT INTO teams VALUES (2, 'Bravo');")
    db.execute("INSERT INTO teams VALUES (3, 'Charlie');")
    db.execute("INSERT INTO teams VALUES (4, 'Delta');") # No members

    db.execute("CREATE TABLE members (member_id INT, member_name TEXT, team_id INT);")
    db.execute("INSERT INTO members VALUES (101, 'Alice', 1);")
    db.execute("INSERT INTO members VALUES (102, 'Bob', 1);")
    db.execute("INSERT INTO members VALUES (103, 'Carol', 2);")
    db.execute("INSERT INTO members VALUES (104, 'Dave', 99);") # No team match

    # 1. INNER JOIN
    print("[1] INNER JOIN...")
    res_inner = db.execute("SELECT members.member_name, teams.team_name FROM members JOIN teams ON members.team_id = teams.team_id;")
    rows_inner = list(res_inner)
    assert len(rows_inner) == 3
    assert {r[0] for r in rows_inner} == {'Alice', 'Bob', 'Carol'}
    print(" PASS: INNER JOIN")

    # 2. LEFT JOIN
    print("[2] LEFT JOIN...")
    res_left = db.execute("SELECT members.member_name, teams.team_name FROM members LEFT JOIN teams ON members.team_id = teams.team_id;")
    rows_left = list(res_left)
    assert len(rows_left) == 4
    dave_row = [r for r in rows_left if r[0] == 'Dave'][0]
    assert dave_row[1] is None or dave_row[1] == ''
    print(" PASS: LEFT JOIN")

    # 3. RIGHT JOIN
    print("[3] RIGHT JOIN...")
    res_right = db.execute("SELECT members.member_name, teams.team_name FROM members RIGHT JOIN teams ON members.team_id = teams.team_id;")
    rows_right = list(res_right)
    assert len(rows_right) >= 4
    delta_row = [r for r in rows_right if r[1] == 'Delta'][0]
    assert delta_row[0] is None or delta_row[0] == ''
    print(" PASS: RIGHT JOIN")

    # 4. FULL OUTER JOIN
    print("[4] FULL OUTER JOIN...")
    res_full = db.execute("SELECT members.member_name, teams.team_name FROM members FULL JOIN teams ON members.team_id = teams.team_id;")
    rows_full = list(res_full)
    names = {r[0] for r in rows_full if r[0]}
    teams = {r[1] for r in rows_full if r[1]}
    assert 'Dave' in names
    assert 'Delta' in teams
    print(" PASS: FULL OUTER JOIN")

    # 5. CROSS JOIN
    print("[5] CROSS JOIN...")
    db.execute("CREATE TABLE letters (letter TEXT);")
    db.execute("INSERT INTO letters VALUES ('X');")
    db.execute("INSERT INTO letters VALUES ('Y');")

    db.execute("CREATE TABLE numbers (num INT);")
    db.execute("INSERT INTO numbers VALUES (1);")
    db.execute("INSERT INTO numbers VALUES (2);")
    db.execute("INSERT INTO numbers VALUES (3);")

    res_cross = db.execute("SELECT letters.letter, numbers.num FROM letters CROSS JOIN numbers;")
    rows_cross = list(res_cross)
    assert len(rows_cross) == 6
    print(" PASS: CROSS JOIN")

    # 6. JOIN with USING
    print("[6] JOIN with USING...")
    res_using = db.execute("SELECT members.member_name, teams.team_name FROM members JOIN teams USING (team_id);")
    assert len(list(res_using)) == 3
    print(" PASS: JOIN with USING")

    print("ALL OUTER JOINS TESTS PASSED!")
    db_inst.close()
    if os.path.exists(db_dir):
        shutil.rmtree(db_dir, ignore_errors=True)

if __name__ == "__main__":
    test_outer_joins()


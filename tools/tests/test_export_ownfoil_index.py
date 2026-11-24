#!/usr/bin/env python3
"""Exercise export-ownfoil-index.py against an isolated Ownfoil 2.3 schema."""
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import unquote


EXPORT = Path(__file__).resolve().parents[1] / "export-ownfoil-index.py"
SERVER = "https://shop.example.test"
BASE = "010015100B514000"
PATCH = "010015100B514800"
DLC = "010015100B515001"
OTHER = "0100000000002000"
FILENAME = "Título 100% #01? [v0].nsp"


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.folder = Path(self.work.name)
        self.database = self.folder / "shop #1.db"
        con = sqlite3.connect(self.database)
        try:
            con.executescript("""
                CREATE TABLE files (
                    id INTEGER PRIMARY KEY, filename TEXT, filepath TEXT, size INTEGER, identified BOOLEAN, multicontent BOOLEAN);
                CREATE TABLE titles (id INTEGER PRIMARY KEY, title_id TEXT);
                CREATE TABLE apps (
                    id INTEGER PRIMARY KEY, title_id INTEGER, app_id TEXT, app_version TEXT, app_type TEXT, owned BOOLEAN);
                CREATE TABLE app_files (app_id INTEGER, file_id INTEGER, PRIMARY KEY(app_id, file_id));
            """)
            con.executemany("INSERT INTO titles VALUES (?,?)", [(1, BASE), (2, OTHER)])
            con.executemany("INSERT INTO apps VALUES (?,?,?,?,?,?)", [
                (1, 1, BASE, "0", "BASE", 1), (2, 1, PATCH, "131072", "UPDATE", 1),
                (3, 1, DLC, "0", "DLC", 1), (4, 2, OTHER, "0", "BASE", 1),
                (5, 1, "unknown", "0", "UPDATE", 1), (6, 1, PATCH, "-1", "UPDATE", 1)])
            for ident, name, apps in [
                (40809, FILENAME, [1]), (40810, "Update.nsz", [2]), (40811, "DLC.nsp", [3]),
                (40812, "Copy.xci", [1]), (40813, "Bundle.xci", [1, 4]), (40814, "Other.nsp", [4]),
                (40815, "Unknown.nsp", [5]), (40816, "No version.nsp", [6]), (40817, "No identity.nsp", [])]:
                con.execute("INSERT INTO files VALUES (?,?,?,?,?,?)", (ident, name, "/games/" + name, 99, 1, 0))
                con.executemany("INSERT INTO app_files VALUES (?,?)", [(app, ident) for app in apps])
            con.commit()
        finally:
            con.close()

    def invoke(self, *args, database=None, server=SERVER):
        return subprocess.run([sys.executable, EXPORT, "--db", database or self.database, "--server", server, *args],
                              capture_output=True, check=False)

    def test_filtered_exports_actual_links_and_leaves_database_untouched(self):
        digest = hashlib.sha256(self.database.read_bytes()).digest()
        response = self.invoke("--title", BASE.lower(), server=SERVER + "/")
        self.assertEqual(response.returncode, 0, response.stderr)
        data = json.loads(response.stdout)
        self.assertEqual((data["schema"], data["server"]), (1, SERVER))
        self.assertEqual([(r["kind"], r["app_id"], r["version"]) for r in data["files"]],
                         [("base", BASE, 0), ("update", PATCH, 131072), ("dlc", DLC, 0), ("base", BASE, 0)])
        self.assertTrue(all(r["title_id"] == BASE and r["size"] == 99 for r in data["files"]))
        route, fragment = data["files"][0]["url"].split("#", 1)
        self.assertEqual(route, "/api/get_game/40809")
        self.assertEqual(unquote(fragment), FILENAME)
        self.assertNotIn("?", fragment)
        self.assertNotIn(b"/games/", response.stdout)
        self.assertEqual(digest, hashlib.sha256(self.database.read_bytes()).digest())

    def test_full_export_and_failed_replacement(self):
        output = self.folder / "index.json"
        response = self.invoke("--output", output)
        self.assertEqual(response.returncode, 0, response.stderr)
        self.assertEqual(response.stdout, b"")
        original = output.read_bytes()
        files = json.loads(original)["files"]
        self.assertEqual(len(files), 5)
        self.assertEqual(files[-1]["title_id"], OTHER)
        self.assertNotEqual(self.invoke("--title", "0100000000003000", "--output", output).returncode, 0)
        self.assertEqual(output.read_bytes(), original)

    def test_rejects_bad_inputs(self):
        digest = self.database.read_bytes()
        for args in (("--title", "no"), ("--output", self.database), ("--output", str(self.database) + "-wal")):
            self.assertNotEqual(self.invoke(*args).returncode, 0)
        for url in ["http://shop.example.test", "https://user@shop.example.test", SERVER + "?token"]:
            self.assertNotEqual(self.invoke(server=url).returncode, 0)
        self.assertEqual(digest, self.database.read_bytes())
        missing = self.folder / "missing.db"
        self.assertNotEqual(self.invoke(database=missing).returncode, 0)
        self.assertFalse(missing.exists())


if __name__ == "__main__":
    unittest.main()


"""Tests of echo_clones.py on a temporary tree: link versus copy, the exclusions, the three juiced.ini keys, the adapter files,
the seeded userdata, a relink after the main file was replaced, the refusals and --dry-run. Nothing touches a game folder."""
import hashlib
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import echo_clones as ec

BIG_BYTES = ec.LINK_MIN_BYTES + 1
JUICED_INI = "[Debug]\r\nAllowMultipleSR2Windows=0\r\n[Misc]\r\nportable=0\r\nRichPresence=1\r\nOther=5\r\n"
ADAPTER_INI = "max_players=4\ntrace=1\ntransport=steam\n"


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode("latin-1"))


def snapshot(folder):
    """Every path under the folder with its size, time and content hash."""
    found = {}
    for base, dirs, files in os.walk(folder):
        found.update({str(Path(base) / d): None for d in dirs})
        for name in files:
            path = Path(base) / name
            found[str(path)] = (path.stat().st_size, path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
    return found


class CloneTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="cf_echo_clones_"))
        self.main = self.work / "game"
        self.root = self.work / "echo"
        self.saves = self.work / "saves"
        write(self.main / "SR2_pc.exe", b"x" * BIG_BYTES)
        write(self.main / "textures.vpp_pc", b"y" * BIG_BYTES)
        write(self.main / "small.txt", "small file")
        write(self.main / "Mods" / "mod.txt", "a mod")
        write(self.main / "dinput8.dll", b"d" * BIG_BYTES)
        write(self.main / "juiced.ini", JUICED_INI)
        write(self.main / "coop" / "sr2_adapter.ini", ADAPTER_INI)
        write(self.main / "coop" / "sr2_adapter.log", "log")
        write(self.main / "coop" / "scripts" / "a.lua", "a")
        write(self.main / "coop" / "scripts" / "sub" / "b.lua", "b")
        write(self.main / "userdata" / "main_save.dat", "the main folder's own")
        write(self.main / "Juiced" / "logs" / "j.log", "j")
        write(self.main / "Juiced" / "CrashDumps" / "dump.dmp", "dump")
        write(self.main / "Juiced" / "keep.txt", "kept")
        for name in ("debug.txt", "SR2_pc.exe.bak", "dinput8.dll.cfbak", "x.cfbuild", "x.cfown", "request.log"):
            write(self.main / name, "excluded")
        write(self.saves / "slot1.sav", "a save")
        write(self.saves / "settings.dat", "settings")
        self.out = []

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def sync(self, count=3, overrides=(), dry_run=False, verbose=False):
        return ec.sync(self.main, self.root, count, overrides, dry_run, verbose, self.saves, self.out.append)

    def guest(self, k):
        return self.root / f"guest{k}"

    def test_big_files_are_links_and_small_ones_copies(self):
        folders, errors = self.sync()
        self.assertEqual(errors, [])
        self.assertEqual(sorted(folders), [2, 3])
        for k in (2, 3):
            self.assertTrue(ec.same_file(self.main / "SR2_pc.exe", self.guest(k) / "SR2_pc.exe"))
            self.assertTrue(ec.same_file(self.main / "textures.vpp_pc", self.guest(k) / "textures.vpp_pc"))
            self.assertFalse(ec.same_file(self.main / "small.txt", self.guest(k) / "small.txt"))
            self.assertEqual((self.guest(k) / "small.txt").read_text(), "small file")
            self.assertEqual((self.guest(k) / "Mods" / "mod.txt").read_text(), "a mod")

    def test_the_adapter_dll_is_a_copy_even_when_it_is_big(self):
        self.sync()
        clone_dll = self.guest(2) / "dinput8.dll"
        self.assertEqual(clone_dll.read_bytes(), (self.main / "dinput8.dll").read_bytes())
        self.assertFalse(ec.same_file(self.main / "dinput8.dll", clone_dll))

    def test_exclusions_are_not_mirrored(self):
        self.sync()
        clone = self.guest(2)
        for gone in ("debug.txt", "SR2_pc.exe.bak", "dinput8.dll.cfbak", "x.cfbuild", "x.cfown", "request.log",
                     "coop/sr2_adapter.log", "Juiced/logs/j.log", "Juiced/CrashDumps/dump.dmp"):
            self.assertFalse((clone / gone).exists(), gone)
        self.assertEqual((clone / "Juiced" / "keep.txt").read_text(), "kept")
        self.assertTrue((clone / "Juiced" / "logs").is_dir())
        self.assertFalse((clone / "userdata" / "main_save.dat").exists())

    def test_juiced_ini_gets_the_three_keys_in_the_clone_only(self):
        before = (self.main / "juiced.ini").read_bytes()
        self.sync()
        text = (self.guest(2) / "juiced.ini").read_text(newline="")
        self.assertEqual(text, "[Debug]\r\nAllowMultipleSR2Windows=1\r\n[Misc]\r\nportable=1\r\nRichPresence=0\r\nOther=5\r\n")
        self.assertEqual((self.main / "juiced.ini").read_bytes(), before)

    def test_a_missing_juiced_key_is_added_under_its_section(self):
        write(self.main / "juiced.ini", "[Debug]\n[Misc]\nOther=5\n")
        self.sync()
        self.assertEqual((self.guest(2) / "juiced.ini").read_text(), "[Debug]\nAllowMultipleSR2Windows=1\n[Misc]\nRichPresence=0\nportable=1\nOther=5\n")

    def test_adapter_files_are_copied_and_scripts_are_an_exact_mirror(self):
        self.sync()
        clone = self.guest(2)
        self.assertEqual((clone / "coop" / "sr2_adapter.ini").read_text(), ADAPTER_INI)
        self.assertEqual((clone / "coop" / "scripts" / "sub" / "b.lua").read_text(), "b")
        write(clone / "coop" / "scripts" / "stale.lua", "left over")
        write(self.main / "coop" / "scripts" / "a.lua", "a changed")
        self.sync()
        self.assertFalse((clone / "coop" / "scripts" / "stale.lua").exists())
        self.assertEqual((clone / "coop" / "scripts" / "a.lua").read_text(), "a changed")

    def test_ini_override_changes_one_key_in_one_clone(self):
        self.sync(overrides=["guest3:max_players=3"])
        self.assertEqual((self.guest(3) / "coop" / "sr2_adapter.ini").read_text(), "max_players=3\ntrace=1\ntransport=steam\n")
        self.assertEqual((self.guest(2) / "coop" / "sr2_adapter.ini").read_text(), ADAPTER_INI)
        self.assertEqual((self.main / "coop" / "sr2_adapter.ini").read_text(), ADAPTER_INI)

    def test_bad_overrides_are_refused(self):
        for spec in ("guest9:max_players=3", "max_players=3", "guest2:=3"):
            with self.assertRaises(ec.CloneError, msg=spec):
                self.sync(overrides=[spec])

    def test_userdata_is_seeded_once(self):
        self.sync()
        saved = self.guest(2) / "userdata" / "slot1.sav"
        self.assertEqual(saved.read_text(), "a save")
        saved.write_text("the guest's own progress")
        write(self.saves / "slot1.sav", "a newer save in the user's folder")
        self.sync()
        self.assertEqual(saved.read_text(), "the guest's own progress")

    def test_a_second_run_changes_nothing(self):
        self.sync()
        before = snapshot(self.root)
        self.out.clear()
        self.sync()
        self.assertEqual(snapshot(self.root), before)
        self.assertEqual([line for line in self.out if not line.startswith("warning")], ["guest2: up to date", "guest3: up to date"])

    def test_a_replaced_main_file_is_relinked(self):
        self.sync()
        replacement = self.main / "textures.new"
        write(replacement, b"z" * BIG_BYTES)
        os.replace(replacement, self.main / "textures.vpp_pc")
        self.assertFalse(ec.same_file(self.main / "textures.vpp_pc", self.guest(2) / "textures.vpp_pc"))
        self.sync()
        self.assertTrue(ec.same_file(self.main / "textures.vpp_pc", self.guest(2) / "textures.vpp_pc"))
        self.assertEqual((self.guest(2) / "textures.vpp_pc").read_bytes()[:1], b"z")

    def test_a_changed_small_file_is_copied_and_never_written_through(self):
        self.sync()
        write(self.main / "small.txt", "small file, changed")
        self.sync()
        self.assertEqual((self.guest(2) / "small.txt").read_text(), "small file, changed")
        write(self.main / "textures.vpp_pc", b"y" * BIG_BYTES)
        os.link(self.main / "small.txt", self.guest(3) / "small.txt.hard")
        os.replace(self.guest(3) / "small.txt.hard", self.guest(3) / "small.txt")
        write(self.main / "small.txt", "small file, third version")
        self.sync()
        self.assertEqual((self.guest(3) / "small.txt").read_text(), "small file, third version")
        self.assertFalse(ec.same_file(self.main / "small.txt", self.guest(3) / "small.txt"))

    def test_a_dry_run_writes_nothing(self):
        main_before = snapshot(self.main)
        self.sync(dry_run=True)
        self.assertFalse(self.root.exists())
        self.assertEqual(snapshot(self.main), main_before)
        self.assertTrue(any("guest2:" in line and "link" in line for line in self.out))
        self.sync()
        write(self.main / "small.txt", "changed after the real run")
        before = snapshot(self.root)
        self.sync(dry_run=True)
        self.assertEqual(snapshot(self.root), before)

    def test_the_main_folder_is_never_written(self):
        before = snapshot(self.main)
        self.sync(overrides=["guest2:max_players=3"])
        self.sync()
        self.assertEqual(snapshot(self.main), before)

    def test_an_action_outside_the_clone_is_refused(self):
        with self.assertRaises(ec.CloneError):
            ec.apply_action(self.guest(2), ec.Action(ec.WRITE, self.main / "evil.txt", content=b"x"))
        self.assertFalse((self.main / "evil.txt").exists())

    def test_another_volume_is_refused(self):
        with mock.patch.object(ec, "same_volume", return_value=False):
            with self.assertRaises(ec.CloneError):
                self.sync()

    def test_a_root_inside_the_main_folder_is_refused(self):
        self.root = self.main / "echo"
        with self.assertRaises(ec.CloneError):
            self.sync()

    def test_a_main_ini_without_the_steam_transport_is_refused(self):
        write(self.main / "coop" / "sr2_adapter.ini", "max_players=4\ntransport=native\n")
        with self.assertRaises(ec.CloneError):
            self.sync()
        self.assertFalse(self.root.exists())

    def test_a_main_juiced_ini_without_multiple_windows_only_warns(self):
        write(self.main / "juiced.ini", "[Debug]\nAllowMultipleSR2Windows=0\n")
        self.sync()
        self.assertTrue(any(line.startswith("warning:") for line in self.out))
        self.assertEqual((self.main / "juiced.ini").read_text(), "[Debug]\nAllowMultipleSR2Windows=0\n")

    def test_a_file_a_game_holds_is_reported_and_the_rest_carries_on(self):
        real_copy = ec.shutil.copy2

        def copy(src, dst, *args, **kwargs):
            if Path(dst).name == "small.txt":
                raise PermissionError("in use")
            return real_copy(src, dst, *args, **kwargs)

        with mock.patch.object(ec.shutil, "copy2", copy):
            _folders, errors = self.sync(2)
        self.assertEqual(len(errors), 1)
        self.assertIn("small.txt", errors[0])
        self.assertEqual((self.guest(2) / "Mods" / "mod.txt").read_text(), "a mod")


class IniValueTest(unittest.TestCase):
    def test_in_place_keeps_the_line_ending_and_the_key_spelling(self):
        self.assertEqual(ec.set_ini_value("a=1\r\nPortable = 0\r\n", "portable", "1"), "a=1\r\nPortable = 1\r\n")

    def test_added_at_the_end_without_a_section(self):
        self.assertEqual(ec.set_ini_value("a=1", "b", "2"), "a=1\nb=2\n")

    def test_a_missing_section_is_added(self):
        self.assertEqual(ec.set_ini_value("a=1\n", "b", "2", "Misc"), "a=1\n[Misc]\nb=2\n")


if __name__ == "__main__":
    unittest.main()

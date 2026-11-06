#!/usr/bin/env python3
"""Host regression checks for the standalone ADB TAR/FMP2 recovery script."""
import importlib.util
from pathlib import Path
import io
import struct
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "fm1_adb", Path(__file__).with_name("FM1_ADB_Recuperacion_0389.py"))
fm1 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fm1)


class RecoveryHostTests(unittest.TestCase):
    def test_legacy_projects_and_samples_map_into_current_library(self):
        cases = {
            "files/project_2.fm1p": "files/FM1-SAMPLER/PROYECTOS/project_2.fm1p",
            "files/sample_old.wav": "files/FM1-SAMPLER/SAVED/RECORDINGS/sample_old.wav",
            "files/samples/Bass/Acoustic.wav": "files/FM1-SAMPLER/OFFICIAL/Bass/Acoustic.wav",
            "files/samples/Other Pack/snare.wav": "files/FM1-SAMPLER/PACKS/Other Pack/snare.wav",
            "files/FM1-SAMPLER/SAVED/LOOPS/live.wav":
                "files/FM1-SAMPLER/SAVED/LOOPS/live.wav",
        }
        for old, expected in cases.items():
            with self.subTest(old=old):
                self.assertEqual(fm1.map_old_to_current(old)[0], expected)

    def test_reject_traversal_and_symlink_in_tar(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "bad.tar"
            with tarfile.open(p, "w") as out:
                entry = tarfile.TarInfo("files/../../private")
                entry.size = 3
                out.addfile(entry, io.BytesIO(b"bad"))
            with self.assertRaises(fm1.FM1Error):
                fm1.inspect_tar(p)
            with tarfile.open(p, "w") as out:
                link = tarfile.TarInfo("files/escape")
                link.type = tarfile.SYMTYPE
                link.linkname = "../private"
                out.addfile(link)
            with self.assertRaises(fm1.FM1Error):
                fm1.inspect_tar(p)

    def test_current_files_win_over_legacy_duplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "mixed.tar"
            with tarfile.open(p, "w") as out:
                for name, body in [
                    ("files/project_2.fm1p", b"legacy"),
                    ("files/FM1-SAMPLER/PROYECTOS/project_2.fm1p", b"modern"),
                ]:
                    entry = tarfile.TarInfo(name)
                    entry.size = len(body)
                    out.addfile(entry, io.BytesIO(body))
            entries = fm1.inspect_tar(p)
            self.assertEqual(
                entries["files/FM1-SAMPLER/PROYECTOS/project_2.fm1p"][0],
                "files/FM1-SAMPLER/PROYECTOS/project_2.fm1p")

    def test_fmp2_magic_and_dry_run_no_device_access(self):
        with tempfile.TemporaryDirectory() as tmp:
            good = Path(tmp) / "project_2.fm1p"
            good.write_bytes(struct.pack("<4I", 0x32504D46, 2, 44100, 4)
                             + b"meta" + b"\0" * 64)
            fm1.check_fm1p(good)
            with patch.object(fm1, "check_device", side_effect=AssertionError("ADB called")):
                fm1.import_project(good, 1, fm1.PACKAGES["scenes"], False, False)
            good.write_bytes(b"not-a-project")
            with self.assertRaises(fm1.FM1Error):
                fm1.check_fm1p(good)

    def test_existing_remote_target_is_never_overwritten_by_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            local = Path(tmp) / "sample.wav"
            local.write_bytes(b"new wav")
            with patch.object(fm1, "remote_exists", return_value=True), \
                 patch.object(fm1, "adb", side_effect=AssertionError("ADB wrote to phone")):
                self.assertEqual(
                    fm1.copy_to_private(fm1.PACKAGES["scenes"], local,
                                        fm1.LIBRARY + "/SAVED/LOOPS/sample.wav"),
                    "skip")


if __name__ == "__main__":
    unittest.main(verbosity=2)

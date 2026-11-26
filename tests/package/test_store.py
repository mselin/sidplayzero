# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-223: the store's manifest, checked with the store's own check at its pinned commit. The store's
publishing tool is never run; what it would write is written here."""

import copy
import importlib.util
import json
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import pins, png  # noqa: E402

PERMISSIONS = ["camera", "microphone", "imu", "network", "additional_hardware", "background_service",
               "external_display"]


def png_size(path):
    return struct.unpack(">II", Path(path).read_bytes()[16:24])


def policy():
    spec = importlib.util.spec_from_file_location("store_meta_policy", pins.fetch("store_meta_policy"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def manifest():
    return json.loads((ROOT / "app-builder.json").read_text(encoding="utf-8"))


def publish(found, folder, files=None):
    """Writes into `folder` what the store's publishing tool makes of a manifest: meta.json, with the title
    from app_name, the icon as its bare file name and each screenshot as screenshots/ and its file name,
    and those files beside it. `files` gives other bytes for a path of the manifest."""
    files = files or {}
    store = found["store"]
    folder = Path(folder)
    (folder / "screenshots").mkdir(parents=True)

    def place(source, target):
        if source in files:
            target.write_bytes(files[source])
        else:
            shutil.copy(ROOT / source, target)

    meta = {key: store[key] for key in ("summary", "description", "categories", "license", "source_repo",
                                        "author", "share_code", "permissions")}
    meta["title"] = found["app_name"]
    meta["icon"] = Path(store["icon"]).name
    place(store["icon"], folder / meta["icon"])
    meta["screenshots"] = []
    for shot in store["screenshots"]:
        meta["screenshots"].append("screenshots/" + Path(shot).name)
        place(shot, folder / "screenshots" / Path(shot).name)
    (folder / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    return folder


class Store(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-store-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def errors(self, found, files=None, name="pool"):
        folder = publish(found, self.work / name, files)
        errors, _warnings = policy().check_meta(found["package_name"], folder, "-")
        return errors

    def picture(self, width, height=170):
        path = self.work / f"picture-{width}x{height}.png"
        png.write(path, width, height, [bytes(width * 3)] * height)
        return path.read_bytes()

    def with_screenshots(self):
        """The manifest; while it has no screenshots yet, with three stand-ins of the right size."""
        found, files = manifest(), {}
        if not found["store"]["screenshots"]:
            for number in (1, 2, 3):
                found["store"]["screenshots"].append(f"stand-in/{number}.png")
                files[f"stand-in/{number}.png"] = self.picture(320)
        return found, files

    def test_fields(self):
        """SPEC-223 the manifest names the app and the package, and its store section has each field of the table but the screenshots"""
        found = manifest()
        store = found["store"]
        self.assertEqual((found["app_name"], found["package_name"]), ("SidPlayZero", "sidplayzero"))
        self.assertRegex(found["version"], r"^\d+\.\d+\.\d+$")
        self.assertLessEqual(len(store["summary"]), 80)
        self.assertNotIn("\n", store["summary"])
        paragraphs = [p for p in store["description"].split("\n\n") if p.strip()]
        self.assertGreaterEqual(len(paragraphs), 2)
        self.assertIn("contains no music", store["description"])
        self.assertRegex(store["description"], r"High Voltage SID Collection \(HVSC\) is ")
        self.assertEqual(store["categories"], ["Media"])
        self.assertEqual(store["license"], "GPL-3.0-or-later")
        self.assertRegex(store["source_repo"], r"^https://[a-z.]+/[\w.-]+/sidplayzero$")
        self.assertEqual(store["author"], {"display_name": "The SidPlayZero authors"})
        self.assertEqual(store["share_code"], "SIDZ")
        self.assertEqual(png_size(ROOT / store["icon"]), (256, 256))
        self.assertNotRegex(Path(store["icon"]).name, r"^sidplayzero(_\d+)?\.png$")   # apart from the launcher's
        usbsid = False      # the package is built without the part `usbsid` (SPEC-115) until SPEC-191 is switched on
        wanted = {key: key == "network" or (key == "additional_hardware" and usbsid) for key in PERMISSIONS}
        self.assertEqual(store["permissions"], wanted)

    def test_screenshots(self):
        """SPEC-223 SPEC-255 the manifest has three to six screenshots of exactly 320×170, each one of the approved images"""
        shots = manifest()["store"]["screenshots"]
        self.assertTrue(3 <= len(shots) <= 6, f"the manifest has {len(shots)} screenshots, and needs three to six approved images")
        approved = {path.read_bytes() for path in (ROOT / "tests" / "golden").glob("*.png")}
        for shot in shots:
            self.assertEqual(png_size(ROOT / shot), (320, 170), shot)
            self.assertIn((ROOT / shot).read_bytes(), approved, f"{shot} is not one of the approved images")

    def test_store_accepts(self):
        """SPEC-223 the store's own check, at its pinned commit, reports no error on what the manifest publishes"""
        errors = [error.split(" / ")[0] for error in self.errors(manifest())]     # the English half of each
        self.assertEqual(len(errors), 0, "the store's check reported: " + "; ".join(errors))

    def test_only_the_screenshots_are_missing(self):
        """SPEC-223 with stand-in screenshots of the right size the store's check reports nothing: the manifest waits for images and for nothing else"""
        found, files = self.with_screenshots()
        errors = [error.split(" / ")[0] for error in self.errors(found, files)]
        self.assertEqual(len(errors), 0, "the store's check reported: " + "; ".join(errors))

    def test_store_refuses_bad_copies(self):
        """SPEC-223 copies with a summary of 81 characters, a share code of five, an icon of 100 pixels and a screenshot 321 wide each get an error"""
        base, files = self.with_screenshots()

        long_summary = copy.deepcopy(base)
        long_summary["store"]["summary"] = "s" * 81
        self.assertEqual(len([e for e in self.errors(long_summary, files, "summary") if "summary must be at most 80" in e]), 1)

        long_code = copy.deepcopy(base)
        long_code["store"]["share_code"] = "SIDZ5"
        self.assertEqual(len([e for e in self.errors(long_code, files, "code") if "share_code is required: exactly 4" in e]), 1)

        small_icon = {**files, base["store"]["icon"]: (ROOT / "packaging/icons/sidplayzero_100.png").read_bytes()}
        self.assertEqual(len([e for e in self.errors(base, small_icon, "icon") if "icon must be a square PNG" in e and "100×100" in e]), 1)

        wide = copy.deepcopy(base)
        wide["store"]["screenshots"][0] = "stand-in/wide.png"
        wide_files = {**files, "stand-in/wide.png": self.picture(321)}
        self.assertEqual(len([e for e in self.errors(wide, wide_files, "wide") if "must be 320×170, got 321×170" in e]), 1)

        for found, name in ((long_summary, "summary"), (long_code, "code"), (wide, "wide")):
            self.assertEqual(len(self.errors(found, {**files, "stand-in/wide.png": self.picture(321)}, name + "-again")), 1,
                             f"the copy with a bad {name} got more than its one error")

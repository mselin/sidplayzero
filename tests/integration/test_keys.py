# SPDX-License-Identifier: GPL-3.0-or-later
"""SPEC-110: the device's input set-up, which is `platform` code, creates one keypad and nothing else.

The set-up is reached through spz_tool, with a made-up list of input devices in place of /dev/input:

    spz_tool input-setup DEVICES

DEVICES is a text file with one input device to a line: its path, a tab, and what the device reports, as
words separated by spaces:

    letters     the keys A to Z, as a keyboard has them
    buttons     the buttons of a mouse, or the touch of a touch screen
    relative    movement, as a mouse or a trackball reports it
    absolute    positions, as a touch screen reports them

The tool does what the device build does at start: it chooses the keyboard as SPEC-111 says (the path in
SIDPLAYZERO_KEYBOARD, else /dev/input/by-path/platform-3f804000.i2c-event, else the first device of the
list that reports the keys A to Z) and makes LVGL's input devices. It opens no device. It prints, one
`key = value` to a line, and ends with status 0:

    keyboard = PATH     the device it would read keys from; empty if it found none
    keypads = N         the LVGL input devices of the keypad type it made
    pointers = N        those of the pointer type (a mouse or a touch screen)
    encoders = N        those of the encoder type
    buttons = N         those of the button type
    grabbed = no        "yes" if the set-up would take the keyboard for itself alone (EVIOCGRAB)
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "spz_tool"
OWN_KEYBOARD = "/dev/input/by-path/platform-3f804000.i2c-event"      # SPEC-111: the CardputerZero's own

# A made-up machine: a mouse, a touch screen, a power button, and two keyboards.
DEVICES = [
    ("/dev/input/event0", "buttons relative"),
    ("/dev/input/event1", "buttons absolute"),
    ("/dev/input/event2", ""),
    ("/dev/input/event3", "letters"),
    ("/dev/input/event4", "letters buttons relative"),
]


class InputSetup(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-input-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def setup(self, devices, keyboard=None):
        """Runs the set-up against a list of devices and returns what it printed, as a dict."""
        listing = self.work / "devices.tsv"
        listing.write_text("".join(f"{path}\t{reports}\n" for path, reports in devices), encoding="utf-8")
        env = {key: value for key, value in os.environ.items() if key != "SIDPLAYZERO_KEYBOARD"}
        if keyboard:
            env["SIDPLAYZERO_KEYBOARD"] = keyboard
        done = subprocess.run([str(TOOL), "input-setup", str(listing)], env=env, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=60)
        if done.returncode != 0:
            self.fail(f"spz_tool input-setup ended with status {done.returncode}: {done.stdout.strip()}")
        values = {}
        for line in done.stdout.split("\n"):
            if " = " in line or line.endswith(" ="):
                key, _, value = line.partition("=")
                values[key.strip()] = value.strip()
        return values

    def test_one_keypad_and_nothing_else(self):
        """SPEC-110 against a made-up list with a mouse, a touch screen and two keyboards, the input set-up creates one keypad and nothing else"""
        values = self.setup(DEVICES)
        self.assertEqual(values.get("keypads"), "1")
        self.assertEqual(values.get("pointers"), "0")
        self.assertEqual(values.get("encoders"), "0")
        self.assertEqual(values.get("buttons"), "0")
        self.assertEqual(values.get("keyboard"), "/dev/input/event3")       # the first that reports the keys A to Z
        self.assertEqual(values.get("grabbed"), "no")                       # SPEC-111: read without grabbing it

    def test_no_pointer_without_a_keyboard(self):
        """SPEC-110 with only a mouse and a touch screen in the list, the input set-up still creates no pointer"""
        values = self.setup(DEVICES[:3])
        self.assertEqual(values.get("pointers"), "0")
        self.assertEqual(values.get("encoders"), "0")
        self.assertEqual(values.get("buttons"), "0")
        self.assertEqual(values.get("keyboard"), "")

    def test_the_devices_own_keyboard_first(self):
        """SPEC-110 SPEC-111 the CardputerZero's own keyboard is chosen before another device with letters, and the path in SIDPLAYZERO_KEYBOARD before both"""
        devices = DEVICES + [(OWN_KEYBOARD, "letters")]
        own = self.setup(devices)
        self.assertEqual(own.get("keyboard"), OWN_KEYBOARD)
        self.assertEqual((own.get("keypads"), own.get("pointers")), ("1", "0"))
        named = self.setup(devices, keyboard="/dev/input/event4")
        self.assertEqual(named.get("keyboard"), "/dev/input/event4")
        self.assertEqual((named.get("keypads"), named.get("pointers")), ("1", "0"))
        self.assertEqual(named.get("grabbed"), "no")

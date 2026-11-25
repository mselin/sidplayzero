# SPDX-License-Identifier: GPL-3.0-or-later
"""The check of a screen description against SPEC-254's two tables (TF-043): no field and no form that
the tables do not name. Every dump a simulator test writes goes through this."""

import re

from .common import ROOT

SCREENS = ["browser", "empty", "playing", "search", "playlists", "playlist", "settings", "hvsc", "about", "selftest"]
PANELS = ["confirm", "prompt", "text", "notice-panel", "more-playing", "pick-playlist", "more-playlist", "import",
          "filter"]
PLACES = SCREENS + PANELS + ["search-field", "search-results", "help", "key-step"]
STATES = ["idle", "waiting", "fetching", "loading", "playing", "paused", "failed"]
ROW_KINDS = ["folder", "tune", "entry", "setting", "action"]
MARKS = ["absent", "fetching", "missing", "favourite", "playing"]
MODES = ["subtunes", "next_tune", "repeat", "stop"]
JOB_KINDS = ["setup", "update", "download", "fetch", "selftest"]
BANDS = ["top", "content", "actions"]
TIME = re.compile(r"^\d+:\d\d$")
POSITION = re.compile(r"^\d+/\d+$")
TOP = ["screen", "stack", "place", "title", "position", "mini", "rows", "list", "detail", "actions", "typing",
       "fields", "panel", "help", "notice", "player", "lines", "voices", "job", "text", "boxes"]


def message_ids():
    """The identifiers of SPEC-116's table."""
    text = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
    table = text[text.index("**SPEC-116"):text.index("## 7.")]
    return set(re.findall(r"MSG_[A-Z0-9_]+", table))


class _Checker:
    def __init__(self):
        self.problems = []
        self.messages = message_ids()

    def bad(self, where, why):
        self.problems.append(f"{where}: {why}")

    def keys(self, where, value, allowed, required=None):
        if not isinstance(value, dict):
            self.bad(where, "must be an object")
            return False
        for key in value:
            if key not in allowed:
                self.bad(where, f"has the field '{key}', which SPEC-254 does not name")
        for key in (allowed if required is None else required):
            if key not in value:
                self.bad(where, f"lacks the field '{key}'")
        return True

    def one_of(self, where, value, allowed, or_null=False):
        if value is None and or_null:
            return
        if value not in allowed:
            self.bad(where, f"is {value!r}; it must be one of {', '.join(allowed)}")

    def kind(self, where, value, kinds, or_null=False):
        if value is None and or_null:
            return True
        if isinstance(value, bool) and bool not in kinds:
            self.bad(where, f"is {value!r}, of the wrong kind")
            return False
        if not isinstance(value, kinds):
            self.bad(where, f"is {value!r}, of the wrong kind")
            return False
        return True

    def message(self, where, value, or_null=False):
        if value is None and or_null:
            return
        if value not in self.messages:
            self.bad(where, f"is {value!r}, which is not an identifier of SPEC-116")

    def rows(self, where, rows):
        if not self.kind(where, rows, (list,)):
            return
        for i, row in enumerate(rows):
            at = f"{where}[{i}]"
            if self.keys(at, row, ["text", "kind", "value", "marks", "highlighted"]):
                self.kind(at + ".text", row.get("text"), (str,))
                self.one_of(at + ".kind", row.get("kind"), ROW_KINDS)
                self.kind(at + ".value", row.get("value"), (str,), or_null=True)
                self.kind(at + ".highlighted", row.get("highlighted"), (bool,))
                if self.kind(at + ".marks", row.get("marks"), (list,)):
                    for mark in row["marks"]:
                        self.one_of(at + ".marks", mark, MARKS)

    def fields(self, where, fields):
        if not self.kind(where, fields, (dict,)):
            return
        for name, field in fields.items():
            if self.keys(f"{where}.{name}", field, ["text", "cursor"]):
                self.kind(f"{where}.{name}.text", field.get("text"), (str,))
                self.kind(f"{where}.{name}.cursor", field.get("cursor"), (int,))

    def check(self, d):
        if not self.keys("description", d, TOP):
            return self.problems
        self.one_of("screen", d.get("screen"), SCREENS)
        if self.kind("stack", d.get("stack"), (list,)):
            for screen in d["stack"]:
                self.one_of("stack", screen, SCREENS)
        self.one_of("place", d.get("place"), PLACES)
        self.kind("title", d.get("title"), (str,), or_null=True)
        if d.get("position") is not None and not (isinstance(d["position"], str) and POSITION.match(d["position"])):
            self.bad("position", f"is {d['position']!r}; it must read n/total")

        mini = d.get("mini")
        if mini is not None and self.keys("mini", mini, ["job", "state", "title", "elapsed"], required=[]):
            self.one_of("mini.state", mini.get("state"), STATES, or_null=True)
            self.kind("mini.title", mini.get("title"), (str,), or_null=True)
            self.kind("mini.job", mini.get("job"), (str,), or_null=True)
            if mini.get("elapsed") is not None and not TIME.match(str(mini["elapsed"])):
                self.bad("mini.elapsed", f"is {mini['elapsed']!r}; it must read m:ss")

        if d.get("rows") is not None:
            self.rows("rows", d["rows"])
        if d.get("list") is not None and self.keys("list", d["list"], ["total", "first", "objects"]):
            for key in ("total", "first", "objects"):
                self.kind("list." + key, d["list"].get(key), (int,))
        self.kind("detail", d.get("detail"), (str,), or_null=True)
        if d.get("actions") is not None:
            if not isinstance(d["actions"], list) or len(d["actions"]) != 5 or not all(isinstance(a, str) for a in d["actions"]):
                self.bad("actions", "must be five strings")
        self.kind("typing", d.get("typing"), (bool,), or_null=True)
        if d.get("fields") is not None:
            self.fields("fields", d["fields"])

        panel = d.get("panel")
        if panel is not None and self.keys("panel", panel, ["id", "title", "text", "rows", "fields"]):
            self.one_of("panel.id", panel.get("id"), PANELS)
            self.kind("panel.title", panel.get("title"), (str,), or_null=True)
            self.kind("panel.text", panel.get("text"), (str,), or_null=True)
            if panel.get("rows") is not None:
                self.rows("panel.rows", panel["rows"])
            if panel.get("fields") is not None:
                self.fields("panel.fields", panel["fields"])

        if d.get("help") is not None:
            if not isinstance(d["help"], list) or not all(isinstance(line, str) for line in d["help"]):
                self.bad("help", "must be a list of lines")
        notice = d.get("notice")
        if notice is not None and self.keys("notice", notice, ["id", "text"]):
            self.message("notice.id", notice.get("id"))
            self.kind("notice.text", notice.get("text"), (str,))

        player = d.get("player")
        if player is not None and self.keys("player", player, [
                "state", "message", "path", "subtune", "subtunes", "elapsed_ms", "length_ms", "engine", "chips",
                "model", "clock", "muted", "filter", "chip", "mode", "underrun_frames"]):
            self.one_of("player.state", player.get("state"), STATES)
            self.message("player.message", player.get("message"), or_null=True)
            self.kind("player.path", player.get("path"), (str,), or_null=True)
            for key in ("subtune", "subtunes", "chips", "chip"):
                if self.kind("player." + key, player.get(key), (int,), or_null=True) and player.get(key) is not None \
                        and player[key] < 1:
                    self.bad("player." + key, "is counted from 1")
            for key in ("elapsed_ms", "length_ms", "underrun_frames"):
                self.kind("player." + key, player.get(key), (int,), or_null=True)
            self.one_of("player.engine", player.get("engine"), ["accurate", "light"], or_null=True)
            self.one_of("player.model", player.get("model"), ["6581", "8580"], or_null=True)
            self.one_of("player.clock", player.get("clock"), ["PAL", "NTSC"], or_null=True)
            self.one_of("player.mode", player.get("mode"), MODES, or_null=True)
            muted = player.get("muted")
            if muted is not None and not (isinstance(muted, list) and all(
                    isinstance(chip, list) and len(chip) == 3 and all(isinstance(v, bool) for v in chip) for chip in muted)):
                self.bad("player.muted", "must be three true-or-false values for each chip")
            flt = player.get("filter")
            if flt is not None and not (isinstance(flt, list) and all(isinstance(v, bool) for v in flt)):
                self.bad("player.filter", "must be one true-or-false value for each chip")

        if d.get("lines") is not None:
            if not isinstance(d["lines"], list) or not all(isinstance(line, str) for line in d["lines"]):
                self.bad("lines", "must be a list of lines")
        if d.get("voices") is not None and self.kind("voices", d["voices"], (list,)):
            for i, voice in enumerate(d["voices"]):
                if self.keys(f"voices[{i}]", voice, ["height", "lit"]):
                    self.kind(f"voices[{i}].height", voice.get("height"), (int,))
                    self.kind(f"voices[{i}].lit", voice.get("lit"), (bool,))
        job = d.get("job")
        if job is not None and self.keys("job", job, ["kind", "step", "text", "done", "total"]):
            self.one_of("job.kind", job.get("kind"), JOB_KINDS)
            for key in ("step", "done", "total"):
                self.kind("job." + key, job.get(key), (int,), or_null=True)
            self.kind("job.text", job.get("text"), (str,), or_null=True)
        self.kind("text", d.get("text"), (str,), or_null=True)
        if d.get("boxes") is not None and self.kind("boxes", d["boxes"], (list,)):
            for i, box in enumerate(d["boxes"]):
                at = f"boxes[{i}]"
                if self.keys(at, box, ["id", "band", "x", "y", "w", "h", "text", "text_x", "text_w", "clipped"]):
                    self.one_of(at + ".band", box.get("band"), BANDS)
                    for key in ("x", "y", "w", "h", "text_x", "text_w"):
                        self.kind(f"{at}.{key}", box.get(key), (int,))
                    self.kind(at + ".id", box.get("id"), (str,))
                    self.kind(at + ".text", box.get("text"), (str,))
                    self.kind(at + ".clipped", box.get("clipped"), (bool,))
        return self.problems


def problems(description):
    """What is wrong with a screen description, as a list of sentences; empty if nothing is."""
    return _Checker().check(description)

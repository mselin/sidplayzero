# SPDX-License-Identifier: GPL-3.0-or-later
"""HVSC and the network with real parts on this machine: the real HttpClient against a local server
(SPEC-150, SPEC-006), the real 7z behind the real Unpacker (SPEC-007, SPEC-144, SPEC-149), and the app's
own reader of the song length database (SPEC-142).

The real parts are reached through `spz_tool`. These are the commands this file uses; each prints lines of
`key = value` and ends with status 0 when it could do what was asked, whatever came of it:

  spz_tool fetch URL TO [--limit BYTES] [--ca FILE] [--release-rules]
      One request by the real HttpClient, the body written to TO. --ca names a certificate authority to
      trust beside the system's, as a test build may (SPEC-150). --release-rules makes the client behave
      as a release build's: no http://127.0.0.1, and no authority but the system's.
        failure = none | not_found | network | tls | too_large | cannot_write | card_full | cancelled
        status = 200          the HTTP status, 0 if there was none
        bytes = 17            bytes of body written

  spz_tool setup --root DIR --cache DIR --url-files URL [--ca FILE]
      SPEC-140's steps 2 to 6 with the real HttpClient and the real file system, the notice accepted.
        result = ready | failed
        release = 85          when ready
        step = 3              when failed

  spz_tool listing DATABASE [--path HVSC_PATH]
      Builds the listing (SPEC-142) from a song length database into a file of its own, and opens it.
        ok = yes | no
        tunes = 11, folders = 10, lengths = 13, skipped = 0     each on a line of its own
        lengths_ms = 3500     with --path: that tune's lengths, separated by spaces

  spz_tool unpacker list ARCHIVE
      The real Unpacker's list() (it runs `7z l -slt`), and SPEC-149's check of what it lists.
        available = yes | no
        entries = 22
        entry = file 5 C64Music/MUSICIANS/A/a.sid     kind (file, folder, link, special), size, path; one line each
        fault = none | absolute_path | empty_part | dot_part | backslash | link | special |
                outside_top_folder | too_many_entries | too_large
  spz_tool unpacker unpack ARCHIVE INTO
      The real Unpacker's unpack() (it runs `7z x -mmt1`).
        ok = yes | no

  spz_tool complete-download --root DIR --cache DIR (--archive FILE | --url-archive URL [--ca FILE])
                             [--stop-in-step N] [--die-in-step N] [--die-after-moves N]
      SPEC-144's job with the real Unpacker and the real file system, the notice accepted, and a Memory
      that reports enough. HVSC must be set up in the root; the listing is built in the cache first if it
      is not there. With --archive the archive comes from a fake HttpClient that answers `url_archive`
      with that file; with --url-archive from the real one. --stop-in-step asks the job to stop when it
      enters that step. --die-in-step kills the process (SIGKILL) when the job has entered that step and
      written its marker; --die-after-moves kills it when that many files have been moved in step 7.
        result = done | failed
        step = 4              when failed: the step it failed or was stopped in
  spz_tool complete-download --root DIR --cache DIR --recover
      What the app does at its next start with the marker of SPEC-144.
        result = done | cleaned | nothing | failed
"""

import http.server
import json
import os
import shutil
import signal
import re
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import localserver  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures"
TOOL = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "spz_tool"
STANDINS = ROOT / "tests" / "integration" / "standins"
SEVEN_ZIP = next((name for name in ("7z", "7zz", "7za") if shutil.which(name)), None)
VERSION = json.loads((ROOT / "app-builder.json").read_text(encoding="utf-8"))["version"]
DOCUMENTS = ["DOCUMENTS/Songlengths.md5", "DOCUMENTS/STIL.txt", "DOCUMENTS/HVSC.txt", "DOCUMENTS/BUGlist.txt",
             "DOCUMENTS/hv_sids.txt"]
OWN_TUNE = "My Rips/Cool Söng.sid"
OLD_NAME = "MUSICIANS/T/Tester_Tom/Old_Name.sid"
MIB = 1024 * 1024


class Ran:
    """What one run of spz_tool gave."""

    def __init__(self, arguments, status, output):
        self.arguments = arguments
        self.status = status
        self.output = output
        self.values = {}                         # key -> the values it was given, in order
        for line in output.split("\n"):
            key, equals, value = line.partition(" = ")
            if equals:
                self.values.setdefault(key.strip(), []).append(value.strip())

    def get(self, key):
        return self.values.get(key, [""])[-1]


def tool(*arguments, env=None, timeout=300):
    process = subprocess.run([str(TOOL), *[str(a) for a in arguments]], env=env, text=True, errors="replace",
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return Ran(arguments, process.returncode, process.stdout)


def tree(folder):
    """Every file under a folder, by its path inside it, with its bytes; a link is "link -> target"."""
    found = {}
    folder = Path(folder)
    if not folder.is_dir():
        return found
    for path in sorted(folder.rglob("*")):
        name = path.relative_to(folder).as_posix()
        if path.is_symlink():
            found[name] = f"link -> {os.readlink(path)}".encode()
        elif path.is_file():
            found[name] = path.read_bytes()
    return found


class ToolCase(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-hvsc-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def ran(self, *arguments, **options):
        """Runs the tool and fails, with what it said, unless it ended with status 0."""
        result = tool(*arguments, **options)
        if result.status != 0:
            said = " / ".join(line for line in result.output.strip().split("\n") if line)
            self.fail(f"spz_tool {arguments[0]} ended with status {result.status}: {said}")
        return result


# --- SPEC-150: the real HttpClient ----------------------------------------------------------------------

class PlainServer:
    """A server on the loopback address that speaks plain http and logs what it is asked."""

    def __init__(self, body):
        self.log = []
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                server.log.append(self.path)
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.httpd.server_address[1]

    def url(self, path):
        return f"http://127.0.0.1:{self.port}{path}"

    def __enter__(self):
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *_):
        self.httpd.shutdown()
        self.httpd.server_close()


class RealHttpClient(ToolCase):
    def setUp(self):
        super().setUp()
        self.files = self.work / "files"
        self.files.mkdir()
        self.body = (FIXTURES / "tunes" / "commando_test.sid").read_bytes()
        (self.files / "tune.sid").write_bytes(self.body)
        self.to = self.work / "fetched.sid"

    def test_fetches_over_https(self):
        """SPEC-150 SPEC-006 the real HttpClient fetches a file over HTTPS from a server whose certificate the test's authority signed"""
        with localserver.LocalServer(self.files, self.work) as server:
            result = self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB, "--ca", server.authority)
            self.assertEqual(result.get("failure"), "none", result.output)
            self.assertEqual(result.get("status"), "200")
            self.assertEqual(result.get("bytes"), str(len(self.body)))
            self.assertEqual(self.to.read_bytes(), self.body)
            self.assertEqual(len(server.log), 1, server.log)
            self.assertTrue(server.log[0].startswith("GET /tune.sid"), server.log)

    def test_user_agent_and_no_cookie(self):
        """SPEC-150 the request as the server received it names the app and its version as its user agent, and carries no cookie"""
        with localserver.LocalServer(self.files, self.work) as server:
            self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB, "--ca", server.authority)
            self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB, "--ca", server.authority)
            self.assertEqual(len(server.log), 2, server.log)
            for entry in server.log:
                self.assertRegex(entry, r"\[User-Agent: [^\]]*SidPlayZero[^\]]*" + VERSION.replace(".", r"\.") + r"[^\]]*\]")
                self.assertNotIn("[Cookie:", entry)

    def test_unknown_authority_is_refused(self):
        """SPEC-150 a server whose certificate no trusted authority signed is refused, and nothing is written"""
        with localserver.LocalServer(self.files, self.work) as server:
            result = self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB)
            self.assertEqual(result.get("failure"), "tls", result.output)
            self.assertFalse(self.to.exists())
            self.assertEqual([e for e in server.log if e.startswith("GET")], [])

    def test_self_signed_is_refused(self):
        """SPEC-150 a self-signed certificate is refused, even with the test's authority trusted"""
        with localserver.LocalServer(self.files, self.work, self_signed=True) as server:
            result = self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB, "--ca", server.authority)
            self.assertEqual(result.get("failure"), "tls", result.output)
            self.assertFalse(self.to.exists())
            self.assertEqual([e for e in server.log if e.startswith("GET")], [])

    def test_redirect_to_http_is_refused(self):
        """SPEC-150 a redirect to http:// is refused: it is not followed, and nothing is written"""
        with localserver.LocalServer(self.files, self.work) as server:
            result = self.ran("fetch", server.url("/redirect-to-http/tune.sid"), self.to, "--limit", MIB,
                              "--ca", server.authority)
            # Refused because the address is not HTTPS; a client that followed it would fail to connect instead.
            self.assertEqual(result.get("failure"), "tls", result.output)
            self.assertFalse(self.to.exists())

    def test_endless_answer_is_cut(self):
        """SPEC-150 an endless answer is cut at the limit and the download fails as too large"""
        with localserver.LocalServer(self.files, self.work) as server:
            result = self.ran("fetch", server.url("/endless"), self.to, "--limit", MIB, "--ca", server.authority,
                              timeout=120)
            self.assertEqual(result.get("failure"), "too_large", result.output)
            if self.to.exists():
                self.assertLessEqual(self.to.stat().st_size, MIB)

    def test_not_found_and_server_error(self):
        """SPEC-150 SPEC-069 the real HttpClient tells a file that is not there from a server that fails"""
        with localserver.LocalServer(self.files, self.work) as server:
            missing = self.ran("fetch", server.url("/no-such.sid"), self.to, "--limit", MIB, "--ca", server.authority)
            self.assertEqual((missing.get("failure"), missing.get("status")), ("not_found", "404"), missing.output)
            gone = self.ran("fetch", server.url("/status/410"), self.to, "--limit", MIB, "--ca", server.authority)
            self.assertEqual(gone.get("failure"), "not_found", gone.output)
            broken = self.ran("fetch", server.url("/status/503"), self.to, "--limit", MIB, "--ca", server.authority)
            self.assertEqual(broken.get("failure"), "network", broken.output)
            self.assertFalse(self.to.exists())

    def test_test_build_accepts_loopback_http(self):
        """SPEC-150 a test build also accepts http://127.0.0.1"""
        with PlainServer(self.body) as server:
            result = self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB)
            self.assertEqual(result.get("failure"), "none", result.output)
            self.assertEqual(self.to.read_bytes(), self.body)
            self.assertEqual(server.log, ["/tune.sid"])

    def test_release_rules_refuse_loopback_http(self):
        """SPEC-150 with a release build's rules http://127.0.0.1 is refused, and no request reaches the server"""
        with PlainServer(self.body) as server:
            result = self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB, "--release-rules")
            self.assertEqual(result.get("failure"), "tls", result.output)
            self.assertFalse(self.to.exists())
            self.assertEqual(server.log, [])

    def test_release_rules_refuse_the_tests_authority(self):
        """SPEC-150 with a release build's rules an authority named by a test is not trusted"""
        with localserver.LocalServer(self.files, self.work) as server:
            result = self.ran("fetch", server.url("/tune.sid"), self.to, "--limit", MIB, "--ca", server.authority,
                              "--release-rules")
            self.assertEqual(result.get("failure"), "tls", result.output)
            self.assertFalse(self.to.exists())
            self.assertEqual([e for e in server.log if e.startswith("GET")], [])

    def test_log_of_a_set_up(self):
        """SPEC-150 SPEC-151 SPEC-140 with url_files pointed at the local server, the server's log is the complete list of requests a set-up makes: the five documents"""
        served = self.work / "served" / "C64Music"
        shutil.copytree(FIXTURES / "collections" / "85" / "C64Music", served)
        root, cache = self.work / "home" / "C64Music", self.work / "home" / "cache"
        with localserver.LocalServer(self.work / "served", self.work) as server:
            result = self.ran("setup", "--root", root, "--cache", cache, "--url-files", server.url("/C64Music"),
                              "--ca", server.authority)
            self.assertEqual(result.get("result"), "ready", result.output)
            self.assertEqual(result.get("release"), "85")
            asked = sorted(entry.split(" ")[1] for entry in server.log)
            self.assertEqual(asked, sorted(f"/C64Music/{document}" for document in DOCUMENTS))
            for entry in server.log:
                self.assertIn("[User-Agent: ", entry)
                self.assertNotIn("[Cookie:", entry)
                self.assertNotIn("[Range:", entry)           # a document is downloaded whole each time (SPEC-147)
        for document in DOCUMENTS:
            self.assertEqual((root / document).read_bytes(), (served / document).read_bytes(), document)


# --- SPEC-142: the listing, by the real program ---------------------------------------------------------

class RealListing(ToolCase):
    def test_fixture_database(self):
        """SPEC-142 the app's reader builds the listing of the fixture database with the real file system: 11 tunes and 13 lengths"""
        database = FIXTURES / "collections" / "85" / "C64Music" / "DOCUMENTS" / "Songlengths.md5"
        result = self.ran("listing", database, "--path", "/MUSICIANS/T/Tester_Tom/Three_Songs.sid")
        self.assertEqual(result.get("ok"), "yes", result.output)
        self.assertEqual((result.get("tunes"), result.get("lengths"), result.get("skipped")), ("11", "13", "0"))
        self.assertEqual(result.get("lengths_ms"), "2000 2500 3000")

    def test_refused_database(self):
        """SPEC-142 a database with two bad lines in a hundred is refused by the real program too"""
        lines = ["[Database]"]
        for number in range(48):
            lines += [f"; /GOOD/T{number:04d}.sid", f"{number + 1:032x}=1:00"]
        lines += ["neither one thing", "nor the other"]
        database = self.work / "Songlengths.md5"
        database.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))
        result = self.ran("listing", database)
        self.assertEqual(result.get("ok"), "no", result.output)

    def test_real_hvsc(self):
        """SPEC-142 SPEC-252 with HVSC #85's own documents the listing has 61,157 tunes in 1,981 folders and 87,868 lengths"""
        folder = os.environ.get("SIDPLAYZERO_REAL_HVSC")
        if not folder:
            self.skipTest("SIDPLAYZERO_REAL_HVSC is not set")
        result = self.ran("listing", Path(folder) / "Songlengths.md5", "--path", "/MUSICIANS/H/Hubbard_Rob/Commando.sid",
                          timeout=600)
        self.assertEqual(result.get("ok"), "yes", result.output)
        self.assertEqual((result.get("tunes"), result.get("folders"), result.get("lengths")), ("61157", "1981", "87868"))
        self.assertEqual(result.get("lengths_ms").split()[:2], ["235594", "61288"])     # 3:55.594 and 1:01.288


# --- SPEC-007, SPEC-144, SPEC-149: the real 7z -------------------------------------------------------------

class SevenZipCase(ToolCase):
    """A home with HVSC set up, and archives made here with the 7z program."""

    def setUp(self):
        super().setUp()
        if SEVEN_ZIP is None:
            self.fail("no 7z, 7zz or 7za on the path: the tests of the real unpacker need one")
        self.home = self.work / "home"
        self.root = self.home / "C64Music"
        self.cache = self.home / "cache"
        self.staging = self.home / ".sidplayzero-staging"
        self.cache.mkdir(parents=True)

    def seven_zip(self, *arguments, cwd=None):
        process = subprocess.run([SEVEN_ZIP, *[str(a) for a in arguments]], cwd=cwd, text=True, errors="replace",
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(process.returncode, 0, f"{SEVEN_ZIP} {' '.join(str(a) for a in arguments)}: {process.stdout[-400:]}")
        return process.stdout

    def paths_in(self, archive):
        """The entries' paths, as `7z l -slt` gives them: what the test made, before the app is asked."""
        listing = self.seven_zip("l", "-slt", archive)
        entries = listing.split("----------\n", 1)[1] if "----------\n" in listing else ""
        return [line[len("Path = "):] for line in entries.split("\n") if line.startswith("Path = ")]

    def good_archive(self, release=85, name="good.7z"):
        """HVSC's layout: one top-level folder, C64Music, holding the fixture collection."""
        archive = self.work / name
        self.seven_zip("a", "-bd", "-mx=1", archive, "C64Music", cwd=FIXTURES / "collections" / str(release))
        return archive

    def set_up(self, release=85, tunes=False):
        """HVSC set up in the root with that release's documents; with `tunes`, every tune present too."""
        source = FIXTURES / "collections" / str(release) / "C64Music"
        if tunes:
            shutil.copytree(source, self.root)
        else:
            shutil.copytree(source / "DOCUMENTS", self.root / "DOCUMENTS")

    def download(self, archive, *more, **options):
        return tool("complete-download", "--root", self.root, "--cache", self.cache, "--archive", archive, *more, **options)

    def need(self, result):
        if result.status != 0:
            said = " / ".join(line for line in result.output.strip().split("\n") if line)
            self.fail(f"spz_tool {result.arguments[0]} ended with status {result.status}: {said}")
        return result


class RealUnpacker(SevenZipCase):
    def test_argument_list_and_no_shell(self):
        """SPEC-007 the real Unpacker starts 7z with an argument list and no shell: a name full of a shell's characters arrives as one argument [parts: download-all]"""
        awkward = self.work / "an archive; touch pwned $(touch pwned2) 'x' \"y\".7z"
        shutil.copy(self.good_archive(), awkward)
        into = self.work / "into here; touch pwned3"
        log = self.work / "standin.log"
        env = {**os.environ, "PATH": f"{STANDINS}:{os.environ['PATH']}", "SPZ_STANDIN_LOG": str(log)}
        listed = tool("unpacker", "list", awkward, env=env)
        self.assertNotEqual(listed.status, 70, listed.output.strip())
        tool("unpacker", "unpack", awkward, into, env=env)
        self.assertTrue(log.exists(), "the stand-in for 7z was not run")
        # Each call starts with the program's name on a line of its own; the archive's name also ends in "7z".
        calls = [call.split("\n") for call in re.split(r"(?m)^7z\n", log.read_text()) if call]
        self.assertEqual(len(calls), 2, log.read_text())
        list_arguments = [line[2:] for line in calls[0] if line.startswith("  ")]
        unpack_arguments = [line[2:] for line in calls[1] if line.startswith("  ")]
        self.assertEqual(list_arguments[:2], ["l", "-slt"])
        self.assertIn(str(awkward), list_arguments)              # whole, as one argument
        self.assertEqual(unpack_arguments[0], "x")
        self.assertIn("-mmt1", unpack_arguments)
        self.assertIn(str(awkward), unpack_arguments)
        self.assertIn(f"-o{into}", unpack_arguments)
        for planted in ("pwned", "pwned2", "pwned3"):
            self.assertEqual(list(self.work.rglob(planted)), [], "a shell ran part of the archive's name")
            self.assertFalse((ROOT / planted).exists())

    def test_lists_a_good_archive(self):
        """SPEC-149 SPEC-007 the app's reader of the real 7z's listing gives every entry of a good archive, and the check passes it [parts: download-all]"""
        archive = self.good_archive()
        result = self.need(tool("unpacker", "list", archive))
        self.assertEqual(result.get("available"), "yes", result.output)
        self.assertEqual(result.get("fault"), "none")
        source = FIXTURES / "collections" / "85"
        expected = []
        for path in sorted(source.rglob("*")):
            name = path.relative_to(source).as_posix()
            expected.append(f"folder 0 {name}" if path.is_dir() else f"file {path.stat().st_size} {name}")
        self.assertEqual(sorted(result.values.get("entry", [])), sorted(expected))
        self.assertEqual(result.get("entries"), str(len(expected)))
        self.assertEqual(sorted(self.paths_in(archive)), sorted(e.split(" ", 2)[2] for e in expected))

    def test_unpacks_a_good_archive(self):
        """SPEC-007 the real Unpacker unpacks a good archive into the folder it is given [parts: download-all]"""
        archive = self.good_archive()
        into = self.work / "unpacked"
        result = self.need(tool("unpacker", "unpack", archive, into))
        self.assertEqual(result.get("ok"), "yes", result.output)
        self.assertEqual(tree(into), tree(FIXTURES / "collections" / "85"))


class RealCompleteDownload(SevenZipCase):
    def expected_tree(self, release=85, planted=True):
        expected = tree(FIXTURES / "collections" / str(release) / "C64Music")
        if planted:
            expected[OWN_TUNE] = (FIXTURES / "own" / OWN_TUNE).read_bytes()
        return expected

    def plant(self):
        target = self.root / OWN_TUNE
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(FIXTURES / "own" / OWN_TUNE, target)

    def check_clean(self):
        self.assertFalse(self.staging.exists(), "the staging folder is still there")
        left = sorted(p.name for p in self.cache.iterdir() if p.name != "listing.bin")
        self.assertEqual(left, [], "left in the cache")

    def test_ends_with_the_tree_in_place(self):
        """SPEC-144 SPEC-007 with a real 7z and an archive of the fixture collection, the complete download ends with the fixture tree under the root and every tune present [parts: download-all]"""
        self.set_up(85)
        result = self.need(self.download(self.good_archive()))
        self.assertEqual(result.get("result"), "done", result.output)
        self.assertEqual(tree(self.root), self.expected_tree(planted=False))
        self.check_clean()

    def test_planted_file_survives_and_old_files_go(self):
        """SPEC-144 a file the test planted in the root survives step 7, and a file only the old release had is removed [parts: download-all]"""
        self.set_up(84, tunes=True)
        self.plant()
        self.assertTrue((self.root / OLD_NAME).exists())
        result = self.need(self.download(self.good_archive(85)))
        self.assertEqual(result.get("result"), "done", result.output)
        self.assertEqual(tree(self.root), self.expected_tree())
        self.assertFalse((self.root / OLD_NAME).exists())
        self.check_clean()

    def test_stopping_with_a_real_7z(self):
        """SPEC-144 stopping at each of steps 3 to 6, with the real 7z, leaves the root's contents unchanged and no staging folder [parts: download-all]"""
        archive = self.good_archive()
        for step in (3, 4, 5, 6):
            shutil.rmtree(self.home, ignore_errors=True)
            self.cache.mkdir(parents=True)
            self.set_up(84, tunes=True)
            self.plant()
            before = tree(self.root)
            result = self.need(self.download(archive, "--stop-in-step", step))
            self.assertEqual(result.get("result"), "failed", result.output)
            self.assertEqual(tree(self.root), before, f"stopped in step {step}")
            self.assertFalse(self.staging.exists(), f"stopped in step {step}: the staging folder is still there")
            self.assertFalse((self.cache / "install.state").exists(), f"stopped in step {step}: the marker is still there")

    def test_killed_while_moving_and_started_again(self):
        """SPEC-144 the program killed during step 7 and started again ends with the same tree as an uninterrupted run: the old release's own files gone, the planted file still there [parts: download-all]"""
        archive = self.good_archive(85)
        for moves in (1, 6, 12):
            shutil.rmtree(self.home, ignore_errors=True)
            self.cache.mkdir(parents=True)
            self.set_up(84, tunes=True)
            self.plant()
            killed = self.download(archive, "--die-after-moves", moves)
            if killed.status == 70:
                self.need(killed)
            self.assertEqual(killed.status, -signal.SIGKILL, f"the tool was to be killed after {moves} moves: {killed.output}")
            self.assertTrue((self.cache / "install.state").exists(), "no marker was left by the kill")
            self.assertTrue(self.staging.exists())
            self.assertNotEqual(tree(self.root), self.expected_tree(), "nothing was left to do")

            again = self.need(tool("complete-download", "--root", self.root, "--cache", self.cache, "--recover"))
            self.assertEqual(again.get("result"), "done", again.output)
            self.assertEqual(tree(self.root), self.expected_tree(), f"killed after {moves} moves")
            self.assertFalse((self.root / OLD_NAME).exists())
            self.check_clean()

    def test_killed_before_moving_and_started_again(self):
        """SPEC-144 after a kill in step 6, with everything unpacked and nothing moved, the next start cleans up: the root is as it was, and the staging folder and the marker are gone [parts: download-all]"""
        self.set_up(84, tunes=True)
        self.plant()
        before = tree(self.root)
        killed = self.download(self.good_archive(85), "--die-in-step", 6)
        if killed.status == 70:
            self.need(killed)
        self.assertEqual(killed.status, -signal.SIGKILL, killed.output)
        self.assertEqual(tree(self.root), before)
        self.assertTrue((self.cache / "install.state").exists(), "no marker was left by the kill")
        self.assertTrue(self.staging.exists(), "nothing had been unpacked when the tool was killed")
        again = self.need(tool("complete-download", "--root", self.root, "--cache", self.cache, "--recover"))
        self.assertEqual(again.get("result"), "cleaned", again.output)
        self.assertEqual(tree(self.root), before)
        self.assertFalse(self.staging.exists())
        self.assertFalse((self.cache / "install.state").exists())

    def test_archive_from_the_local_server(self):
        """SPEC-150 SPEC-144 with url_archive pointed at the local server, the server's log is the complete list of requests the download makes: the archive of the release set up [parts: download-all]"""
        self.set_up(85)
        served = self.work / "served"
        served.mkdir()
        shutil.copy(self.good_archive(), served / "HVSC_85-all-of-them.7z")
        with localserver.LocalServer(served, self.work) as server:
            result = self.need(tool("complete-download", "--root", self.root, "--cache", self.cache,
                                    "--url-archive", server.url("/HVSC_{release}-all-of-them.7z"), "--ca", server.authority))
            self.assertEqual(result.get("result"), "done", result.output)
            self.assertEqual([entry.split(" ")[1] for entry in server.log], ["/HVSC_85-all-of-them.7z"])
            self.assertIn("[User-Agent: ", server.log[0])
            self.assertNotIn("[Cookie:", server.log[0])
        self.assertEqual(tree(self.root), self.expected_tree(planted=False))


class UnsafeArchives(SevenZipCase):
    """SPEC-149: one archive for each trick, made here with the 7z program; each is refused by the real 7z
    path, nothing is unpacked, and afterwards no file exists outside the staging folder, which is gone."""

    def setUp(self):
        super().setUp()
        self.set_up(85)
        self.make = self.work / "make"
        self.make.mkdir()
        self.tune = (FIXTURES / "tunes" / "commando_test.sid").read_bytes()

    def refused(self, archive, fault):
        """Runs the complete download on a trick archive and checks that nothing came of it."""
        before = tree(self.home)
        before_root = tree(self.root)
        listed = self.need(tool("unpacker", "list", archive, timeout=600))
        self.assertEqual(listed.get("fault"), fault, listed.output[-600:])
        result = self.need(self.download(archive, timeout=600))
        self.assertEqual(result.get("result"), "failed", result.output)
        self.assertEqual(result.get("step"), "4", result.output)
        self.assertFalse(self.staging.exists(), "the staging folder is still there")
        self.assertEqual(tree(self.root), before_root)
        after = {name: data for name, data in tree(self.home).items() if not name.startswith("cache/")}
        self.assertEqual(after, {name: data for name, data in before.items() if not name.startswith("cache/")})
        self.assertFalse((self.cache / "install.state").exists())

    def tree_with(self, folder, files):
        for name, data in files.items():
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def test_absolute_path(self):
        """SPEC-149 an archive with an absolute path is refused by the real 7z path, and nothing appears at that path [parts: download-all]"""
        elsewhere = self.work / "elsewhere"
        self.tree_with(elsewhere, {"C64Music/Absolute.sid": self.tune})
        self.tree_with(self.make, {"C64Music/Good.sid": self.tune})
        archive = self.work / "absolute.7z"
        self.seven_zip("a", "-bd", "-spf", archive, "C64Music/Good.sid", elsewhere / "C64Music" / "Absolute.sid", cwd=self.make)
        self.assertIn(str(elsewhere / "C64Music" / "Absolute.sid"), self.paths_in(archive), "the archive is not the trick it was meant to be")
        shutil.rmtree(elsewhere)
        self.refused(archive, "absolute_path")
        self.assertFalse(elsewhere.exists(), "the archive's absolute path was written")

    def test_dot_dot(self):
        """SPEC-149 an archive with a .. part is refused by the real 7z path, and nothing appears beside the staging folder [parts: download-all]"""
        self.tree_with(self.make, {"inner/C64Music/Good.sid": self.tune, "Evil.sid": self.tune})
        archive = self.work / "dotdot.7z"
        self.seven_zip("a", "-bd", "-spf", archive, "C64Music/Good.sid", "../Evil.sid", cwd=self.make / "inner")
        self.assertIn("../Evil.sid", self.paths_in(archive), "the archive is not the trick it was meant to be")
        self.refused(archive, "dot_part")
        self.assertEqual(list(self.work.rglob("Evil.sid")), [self.make / "Evil.sid"])

    def test_link_pointing_outside(self):
        """SPEC-149 an archive with a link that points outside is refused by the real 7z path [parts: download-all]"""
        outside = self.work / "outside.txt"
        outside.write_text("a file outside the collection\n")
        self.tree_with(self.make, {"C64Music/Good.sid": self.tune})
        os.symlink(outside, self.make / "C64Music" / "Link.sid")
        archive = self.work / "link.7z"
        self.seven_zip("a", "-bd", "-snl", archive, "C64Music", cwd=self.make)
        listing = self.seven_zip("l", "-slt", archive)
        self.assertRegex(listing, r"Path = C64Music/Link\.sid\n(?:.*\n)*?Attributes = \S* ?l", "the archive is not the trick it was meant to be")
        self.refused(archive, "link")
        self.assertEqual(outside.read_text(), "a file outside the collection\n")

    def test_two_top_level_folders(self):
        """SPEC-149 an archive with a second top-level folder is refused by the real 7z path [parts: download-all]"""
        self.tree_with(self.make, {"C64Music/Good.sid": self.tune, "Other/Extra.sid": self.tune})
        archive = self.work / "two.7z"
        self.seven_zip("a", "-bd", archive, "C64Music", "Other", cwd=self.make)
        self.assertIn("Other/Extra.sid", self.paths_in(archive))
        self.refused(archive, "outside_top_folder")
        self.assertEqual(list(self.home.rglob("Extra.sid")), [])

    def test_too_many_entries(self):
        """SPEC-149 an archive with more than 200,000 entries is refused by the real 7z path [parts: download-all]"""
        top = self.make / "C64Music"
        for folder in range(201):
            inside = top / f"D{folder:03d}"
            inside.mkdir(parents=True)
            for number in range(1000):
                (inside / f"t{number:03d}.sid").touch()
        archive = self.work / "many.7z"
        self.seven_zip("a", "-bd", "-mx=1", archive, "C64Music", cwd=self.make)
        self.assertGreater(len(self.paths_in(archive)), 200000)
        self.refused(archive, "too_many_entries")

    def test_too_large_in_total(self):
        """SPEC-149 an archive whose entries add up to more than 2 GiB is refused by the real 7z path [parts: download-all]"""
        top = self.make / "C64Music"
        top.mkdir()
        for name in ("Big_One.sid", "Big_Two.sid"):
            with open(top / name, "wb") as big:
                big.truncate(1100 * MIB)             # holes: they take no room here, and compress to nothing
        archive = self.work / "large.7z"
        self.seven_zip("a", "-bd", "-mx=1", archive, "C64Music", cwd=self.make)
        self.assertLess(archive.stat().st_size, 16 * MIB)
        shutil.rmtree(top)
        self.refused(archive, "too_large")


if __name__ == "__main__":
    unittest.main()

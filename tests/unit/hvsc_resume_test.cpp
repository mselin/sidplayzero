// SPDX-License-Identifier: GPL-3.0-or-later
// Carrying on with a download that was cut (SPEC-147): the partial file and its .meta in the cache, the
// Range request for the rest, and starting again from the beginning when the file is no longer the same.
// This is the mechanism the complete archive's download uses; it is tested here by itself, because it is
// always built.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/download_resume.h"
#include "support.h"

using namespace spz::test::hvsc;

namespace {

const char* const kUrl = "https://hvsc.brona.dk/HVSC/HVSC_85-all-of-them.7z";
const char* const kName = "HVSC_85-all-of-them.7z";
const std::uint64_t kLimit = 512 * kMiB;                 // SPEC-150: the archive's limit

// `size` bytes that differ from place to place, and from one `seed` to another.
spz::Bytes pattern(std::size_t size, unsigned seed) {
    spz::Bytes data(size);
    for (std::size_t i = 0; i < size; ++i) data[i] = static_cast<std::uint8_t>((i * 31 + i / 251 + seed * 7) & 0xFF);
    return data;
}

struct Download {
    spz::test::TempFolder temp;
    spz::fake::Files files;
    spz::fake::HttpClient http;
    std::string cache = temp.file("cache");
    std::string part = cache + "/" + kName + ".part";
    std::string meta = cache + "/" + kName + ".part.meta";

    Download() { fs::create_directories(cache); }

    // What the server has now.
    void serve(const spz::Bytes& body, const std::string& etag, const std::string& last_modified, std::uint64_t cut_after = 0) {
        spz::fake::HttpClient::Answer answer;
        answer.body = body;
        answer.etag = etag;
        answer.last_modified = last_modified;
        answer.cut_after = cut_after;
        answer.steps = 4;
        http.answer(kUrl, answer);
    }
    spz::HttpResult run(const spz::HttpClient::Progress& progress = nullptr) {
        return spz::download_resumable(&files, &http, kUrl, cache, kName, kLimit, progress);
    }
};

const char* const kDate = "Wed, 25 Nov 2026 10:00:00 GMT";

}  // namespace

TEST_CASE("SPEC-147 a download that is not cut is one request from the first byte, and the whole file is there") {
    Download d;
    const spz::Bytes whole = pattern(1000, 1);
    d.serve(whole, "\"v1\"", kDate);

    const spz::HttpResult result = d.run();
    CHECK(result.failure == spz::HttpFailure::None);
    REQUIRE(d.http.log.size() == 1);
    CHECK(d.http.log[0].url == kUrl);
    CHECK(d.http.log[0].to_path == d.part);
    CHECK(d.http.log[0].resume_from == 0);
    CHECK(d.http.log[0].limit_bytes == kLimit);
    CHECK(read_file(d.part) == whole);

    spz::discard_partial(&d.files, d.cache, kName);
    CHECK(names_in(d.cache).empty());
}

TEST_CASE("SPEC-147 an archive download cut at 50 % and tried again requests bytes from the cut, and ends with a file equal to the whole") {
    Download d;
    const spz::Bytes whole = pattern(1000, 1);
    d.serve(whole, "\"v1\"", kDate, 500);

    const spz::HttpResult cut = d.run();
    CHECK(cut.failure == spz::HttpFailure::Network);
    // The partial file is kept, with its .meta beside it, and nothing else.
    CHECK(names_in(d.cache) == std::vector<std::string>{std::string(kName) + ".part", std::string(kName) + ".part.meta"});
    CHECK(read_file(d.part) == spz::Bytes(whole.begin(), whole.begin() + 500));

    d.serve(whole, "\"v1\"", kDate);              // the same file, and the connection holds this time
    const spz::HttpResult rest = d.run();
    CHECK(rest.failure == spz::HttpFailure::None);
    CHECK(rest.resumed);
    REQUIRE(d.http.log.size() == 2);
    CHECK(d.http.log[1].resume_from == 500);      // "Range: bytes=500-"
    CHECK(d.http.log[1].if_etag == "\"v1\"");     // with the server's two values as its condition
    CHECK(d.http.log[1].if_last_modified == kDate);
    CHECK(d.http.log[1].to_path == d.part);
    CHECK(read_file(d.part) == whole);
}

TEST_CASE("SPEC-147 with a changed ETag the download starts again from the beginning: the file is the new one, whole") {
    Download d;
    const spz::Bytes old_file = pattern(1000, 1), new_file = pattern(1200, 2);
    d.serve(old_file, "\"v1\"", kDate, 500);
    CHECK(d.run().failure == spz::HttpFailure::Network);
    REQUIRE(read_file(d.part).size() == 500);

    d.serve(new_file, "\"v2\"", "Thu, 26 Nov 2026 10:00:00 GMT");     // the archive was replaced meanwhile
    const spz::HttpResult result = d.run();
    CHECK(result.failure == spz::HttpFailure::None);
    CHECK_FALSE(result.resumed);
    CHECK(read_file(d.part) == new_file);         // none of the old file's first half is in it

    // A cut of the new file is carried on with the new values.
    Download e;
    e.serve(old_file, "\"v1\"", kDate, 500);
    CHECK(e.run().failure == spz::HttpFailure::Network);
    e.serve(new_file, "\"v2\"", "Thu, 26 Nov 2026 10:00:00 GMT", 600);
    CHECK(e.run().failure == spz::HttpFailure::Network);
    REQUIRE(read_file(e.part) == spz::Bytes(new_file.begin(), new_file.begin() + 600));
    e.serve(new_file, "\"v2\"", "Thu, 26 Nov 2026 10:00:00 GMT");
    CHECK(e.run().failure == spz::HttpFailure::None);
    REQUIRE(e.http.log.size() == 3);
    CHECK(e.http.log[2].resume_from == 600);
    CHECK(e.http.log[2].if_etag == "\"v2\"");
    CHECK(read_file(e.part) == new_file);
}

TEST_CASE("SPEC-147 when the server gave neither an ETag nor a Last-Modified, the next attempt requests from 0") {
    Download d;
    const spz::Bytes whole = pattern(1000, 1);
    d.serve(whole, "", "", 500);
    CHECK(d.run().failure == spz::HttpFailure::Network);

    d.serve(whole, "", "");
    const spz::HttpResult result = d.run();
    CHECK(result.failure == spz::HttpFailure::None);
    REQUIRE(d.http.log.size() == 2);
    CHECK(d.http.log[1].resume_from == 0);
    CHECK(d.http.log[1].if_etag.empty());
    CHECK(d.http.log[1].if_last_modified.empty());
    CHECK(read_file(d.part) == whole);
}

TEST_CASE("SPEC-147 a partial file whose .meta is gone is not carried on with: the next attempt requests from 0") {
    Download d;
    const spz::Bytes whole = pattern(1000, 1);
    d.serve(whole, "\"v1\"", kDate, 500);
    CHECK(d.run().failure == spz::HttpFailure::Network);
    REQUIRE(exists(d.meta));
    fs::remove(d.meta);

    d.serve(whole, "\"v1\"", kDate);
    CHECK(d.run().failure == spz::HttpFailure::None);
    REQUIRE(d.http.log.size() == 2);
    CHECK(d.http.log[1].resume_from == 0);
    CHECK(read_file(d.part) == whole);
}

TEST_CASE("SPEC-147 stopping leaves nothing in the cache but the partial file and its .meta, and the next attempt carries on from there") {
    Download d;
    const spz::Bytes whole = pattern(1000, 1);
    d.serve(whole, "\"v1\"", kDate);

    // "Stop" when half has arrived: the progress callback says no.
    std::vector<std::uint64_t> reported;
    const spz::HttpResult stopped = d.run([&](std::uint64_t done, std::uint64_t total) {
        reported.push_back(done);
        CHECK(total == 1000);
        return done < 500;
    });
    CHECK(stopped.failure == spz::HttpFailure::Cancelled);
    CHECK(reported == std::vector<std::uint64_t>{250, 500});
    CHECK(names_in(d.cache) == std::vector<std::string>{std::string(kName) + ".part", std::string(kName) + ".part.meta"});
    CHECK(read_file(d.part) == spz::Bytes(whole.begin(), whole.begin() + 500));

    // Progress counts from the start of the whole file when the rest is fetched.
    reported.clear();
    const spz::HttpResult rest = d.run([&](std::uint64_t done, std::uint64_t) {
        reported.push_back(done);
        return true;
    });
    CHECK(rest.failure == spz::HttpFailure::None);
    REQUIRE(d.http.log.size() == 2);
    CHECK(d.http.log[1].resume_from == 500);
    REQUIRE_FALSE(reported.empty());
    CHECK(reported.front() > 500);
    CHECK(reported.back() == 1000);
    CHECK(read_file(d.part) == whole);
}

TEST_CASE("SPEC-147 SPEC-150 an archive past its limit fails as too large") {
    Download d;
    const spz::Bytes whole = pattern(2000, 1);
    d.serve(whole, "\"v1\"", kDate);
    const spz::HttpResult result = spz::download_resumable(&d.files, &d.http, kUrl, d.cache, kName, 1999, nullptr);
    CHECK(result.failure == spz::HttpFailure::TooLarge);
    REQUIRE(d.http.log.size() == 1);
    CHECK(d.http.log[0].limit_bytes == 1999);
}

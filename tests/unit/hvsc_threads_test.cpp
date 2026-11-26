// SPDX-License-Identifier: GPL-3.0-or-later
// HVSC's parts where threads meet (SPEC-022): the listing opened again while other threads read it
// (SPEC-142), and a fetch whose seam throws, after which the fetcher still fetches (SPEC-141).
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/fetcher.h"
#include "spz/listing.h"
#include "support.h"

using namespace spz::test::hvsc;
using spz::FetchAnswer;
using spz::FetchWhy;

namespace {

// Files whose free_bytes() throws, the first `throws` times it is asked.
struct ThrowingFiles : spz::fake::Files {
    int throws = 1;
    spz::FileStatus free_bytes(const std::string& path, std::uint64_t* out) override {
        if (throws > 0) {
            --throws;
            throw std::runtime_error("a seam that throws");
        }
        return spz::fake::Files::free_bytes(path, out);
    }
};

}  // namespace

TEST_CASE("SPEC-142 SPEC-022 a listing opened again while other threads read it gives each reader a whole listing, the one before or the one after") {
    World w;
    w.put_documents(85);
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    const int tunes = listing.tune_count();
    REQUIRE(tunes > 0);
    spz::ListedTune commando;
    REQUIRE(listing.by_path(kCommando, &commando));

    std::atomic<bool> stop{false};
    std::atomic<int> wrong{0}, reads{0};
    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r) {
        readers.emplace_back([&] {
            while (!stop.load()) {
                // One job's reads, from one copy: the listing of that moment, whatever happens to the original.
                const spz::Listing snapshot = listing;
                spz::ListedTune found;
                std::vector<std::string> folders, names;
                if (!snapshot.by_path(kCommando, &found) || found.md5 != commando.md5) ++wrong;
                if (!snapshot.folder("/", &folders, &names) || folders.empty()) ++wrong;
                if (snapshot.tune_count() != tunes || static_cast<int>(snapshot.tunes(0, tunes).size()) != tunes) ++wrong;
                // And straight from the original, each call whole.
                if (listing.by_path(kCommando, &found) && found.md5 != commando.md5) ++wrong;
                ++reads;
            }
        });
    }
    for (int i = 0; i < 200; ++i) REQUIRE(listing.open(&w.files, w.listing_file(), w.database()));
    while (reads.load() < 100) std::this_thread::yield();
    stop = true;
    for (std::thread& reader : readers) reader.join();
    CHECK(wrong.load() == 0);
    CHECK(listing.is_open());
}

TEST_CASE("SPEC-141 SPEC-022 a fetch whose seam throws has failed, and the fetcher goes on: the next request is fetched") {
    World w;
    w.record.accepted = true;
    w.put_documents(85);
    serve_collection(&w.http, 85);
    spz::Listing listing;
    REQUIRE(w.open_listing(&listing));
    ThrowingFiles files;
    Heard heard;
    spz::FetcherParts parts = fetcher_parts(w, &listing);
    parts.files = &files;
    spz::Fetcher fetcher(parts);
    fetcher.set_listener(&heard);

    REQUIRE(fetcher.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    CHECK_NOTHROW(w.run());
    CHECK(heard.events == std::vector<std::string>{"failed " + kCommando + " MSG_CANNOT_WRITE"});
    CHECK_FALSE(fetcher.fetching(kCommando));

    REQUIRE(fetcher.request(kCommando, FetchWhy::Chosen) == FetchAnswer::Fetching);
    w.run();
    REQUIRE_FALSE(heard.events.empty());
    CHECK(heard.events.back() == "fetched " + kCommando);
    CHECK(exists(w.in_root(kCommando)));
}

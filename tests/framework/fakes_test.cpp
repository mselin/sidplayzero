// SPDX-License-Identifier: GPL-3.0-or-later
// TF-021: the fakes do what SPEC-021's table says of them. Tests of the test framework.
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "support.h"

using spz::test::TempFolder;
using spz::test::bytes;

TEST_CASE("TF-021 the fake clock moves only when told, and fires timers in the order they fall due") {
    spz::fake::Clock clock;
    std::vector<int> fired;
    clock.call_after(300, [&] { fired.push_back(300); });
    clock.call_after(100, [&] {
        fired.push_back(100);
        clock.call_after(50, [&] { fired.push_back(150); });  // set while firing, due inside the same advance
    });
    const auto cancelled = clock.call_after(200, [&] { fired.push_back(200); });
    clock.cancel(cancelled);

    CHECK(clock.now_ms() == 0);
    clock.sleep_ms(1000);
    CHECK(clock.now_ms() == 0);
    CHECK(clock.slept_ms == 1000);

    clock.advance(99);
    CHECK(fired.empty());
    clock.advance(201);
    CHECK(fired == std::vector<int>{100, 150, 300});
    CHECK(clock.now_ms() == 300);
    CHECK(clock.timers_waiting() == 0);
}

TEST_CASE("TF-021 the fake executor runs jobs when told, in the order posted") {
    spz::fake::Executor executor;
    std::vector<int> ran;
    executor.post([&] { ran.push_back(1); executor.post([&] { ran.push_back(3); }); });
    executor.post([&] { ran.push_back(2); });
    CHECK(ran.empty());
    CHECK(executor.run_one());
    CHECK(ran == std::vector<int>{1});
    CHECK(executor.run_all() == 2);
    CHECK(ran == std::vector<int>{1, 2, 3});
    CHECK_FALSE(executor.run_one());
}

TEST_CASE("TF-021 the fake files work on a folder of the test's own and log every operation") {
    TempFolder folder;
    spz::fake::Files files;
    CHECK(files.write(folder.file("a.txt"), bytes("hello")) == spz::FileStatus::Ok);
    std::vector<std::uint8_t> read;
    CHECK(files.read(folder.file("a.txt"), 100, &read) == spz::FileStatus::Ok);
    CHECK(read == bytes("hello"));
    CHECK(files.read(folder.file("a.txt"), 4, &read) == spz::FileStatus::TooLarge);
    CHECK(files.read(folder.file("none.txt"), 100, &read) == spz::FileStatus::NotFound);
    CHECK(files.rename(folder.file("a.txt"), folder.file("b.txt")) == spz::FileStatus::Ok);
    std::vector<spz::FileInfo> entries;
    CHECK(files.list(folder.path.string(), &entries) == spz::FileStatus::Ok);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "b.txt");
    CHECK(entries[0].size == 5);
    CHECK_FALSE(entries[0].is_folder);
    CHECK(files.count("write") == 1);
    CHECK(files.count("rename") == 1);
    CHECK(files.calls == 6);
}

TEST_CASE("TF-021 the fake files can fail at any chosen operation, and can be made read-only or full") {
    TempFolder folder;
    spz::fake::Files files;
    files.fail_at(2, spz::FileStatus::NoSpace);
    CHECK(files.write(folder.file("one"), bytes("1")) == spz::FileStatus::Ok);
    CHECK(files.write(folder.file("two"), bytes("2")) == spz::FileStatus::NoSpace);
    CHECK_FALSE(std::filesystem::exists(folder.file("two")));
    CHECK(files.write(folder.file("three"), bytes("3")) == spz::FileStatus::Ok);

    files.make_read_only(folder.path.string());
    CHECK(files.write(folder.file("four"), bytes("4")) == spz::FileStatus::NoAccess);
    CHECK_FALSE(files.can_write(folder.path.string()));

    files.set_free_bytes(7 * 1024 * 1024);
    std::uint64_t free = 0;
    CHECK(files.free_bytes(folder.file("anything"), &free) == spz::FileStatus::Ok);
    CHECK(free == 7u * 1024 * 1024);
}

TEST_CASE("TF-021 the fake network answers from its table, logs requests, and fails as told") {
    TempFolder folder;
    spz::fake::HttpClient http;
    http.answer("https://example.test/tune.sid", "PSID-bytes");
    spz::fake::HttpClient::Answer big;
    big.body = bytes("0123456789");
    big.etag = "\"v1\"";
    big.last_modified = "Sat, 28 Jun 2026 00:00:00 GMT";
    big.cut_after = 4;
    http.answer("https://example.test/archive.7z", big);

    spz::HttpRequest request;
    request.url = "https://example.test/tune.sid";
    request.to_path = folder.file("tune.sid");
    spz::HttpResult result = http.fetch(request, nullptr);
    CHECK(result.failure == spz::HttpFailure::None);
    CHECK(result.bytes == 10);

    request.url = "https://example.test/missing.sid";
    CHECK(http.fetch(request, nullptr).failure == spz::HttpFailure::NotFound);
    request.url = "http://example.test/tune.sid";
    CHECK(http.fetch(request, nullptr).failure == spz::HttpFailure::Tls);

    // A download cut at 4 bytes, then carried on with the validators the server gave.
    request.url = "https://example.test/archive.7z";
    request.to_path = folder.file("archive.part");
    result = http.fetch(request, nullptr);
    CHECK(result.failure == spz::HttpFailure::Network);
    CHECK(result.bytes == 4);
    big.cut_after = 0;
    http.answer("https://example.test/archive.7z", big);
    request.resume_from = 4;
    request.if_etag = result.etag;
    request.if_last_modified = result.last_modified;
    result = http.fetch(request, nullptr);
    CHECK(result.failure == spz::HttpFailure::None);
    CHECK(result.resumed);
    CHECK(std::filesystem::file_size(folder.file("archive.part")) == 10);

    http.down = true;
    CHECK(http.fetch(request, nullptr).failure == spz::HttpFailure::Network);
    CHECK(http.log.size() == 6);
    CHECK(http.log[0].url == "https://example.test/tune.sid");
}

TEST_CASE("TF-021 the fake network can be cancelled from the progress call, and counts forbidden requests") {
    TempFolder folder;
    spz::fake::HttpClient http;
    spz::fake::HttpClient::Answer answer;
    answer.body = bytes("0123456789");
    answer.steps = 5;
    http.answer("https://example.test/file", answer);
    spz::HttpRequest request;
    request.url = "https://example.test/file";
    request.to_path = folder.file("file");
    int reports = 0;
    const spz::HttpResult result = http.fetch(request, [&](std::uint64_t done, std::uint64_t total) {
        ++reports;
        CHECK(total == 10);
        return done < 4;
    });
    CHECK(result.failure == spz::HttpFailure::Cancelled);
    CHECK(reports == 2);

    http.forbidden = true;
    http.fetch(request, nullptr);
    CHECK(http.forbidden_requests == 1);
}

TEST_CASE("TF-021 the fake engine produces a known ramp, can be slow, and can refuse") {
    spz::fake::Engine engine;
    spz::EngineSettings settings;
    std::string why;
    REQUIRE(engine.load({}, 2, settings, &why));
    std::int16_t out[8] = {};
    CHECK(engine.render(out, 4) == 4);
    CHECK(out[0] == spz::fake::Engine::sample(2, 0));
    CHECK(out[1] == -spz::fake::Engine::sample(2, 0));
    CHECK(out[6] == spz::fake::Engine::sample(2, 3));
    engine.frames_per_call = 1;
    CHECK(engine.render(out, 4) == 1);
    CHECK(out[0] == spz::fake::Engine::sample(2, 4));
    engine.refuse = true;
    CHECK_FALSE(engine.load({}, 1, settings, &why));
    CHECK(why == "MSG_ENGINE_REFUSED");
    CHECK(engine.loads == 2);
}

TEST_CASE("TF-021 the fake sink pulls when told, records what it got, and can refuse or lose the device") {
    spz::fake::AudioSink sink;
    int lost = 0;
    sink.on_device_lost([&] { ++lost; });
    std::int16_t next = 0;
    REQUIRE(sink.open(48000, [&](std::int16_t* out, std::size_t frames) {
        for (std::size_t i = 0; i < frames * 2; ++i) out[i] = next++;
    }));
    sink.pull(3);
    CHECK(sink.received == std::vector<std::int16_t>{0, 1, 2, 3, 4, 5});
    CHECK(sink.sample_rate == 48000);
    sink.drop_device();
    CHECK(lost == 1);
    sink.close();
    sink.refuse = true;
    CHECK_FALSE(sink.open(48000, nullptr));
}

TEST_CASE("TF-021 the fake backend records what it is told, reports events, and its position follows the clock") {
    struct Listener : spz::SoundBackendListener {
        std::vector<std::string> events;
        void song_ended() override { events.push_back("song_ended"); }
        void device_lost() override { events.push_back("device_lost"); }
        void device_back() override { events.push_back("device_back"); }
        void device_gone() override { events.push_back("device_gone"); }
        void failed(const std::string& reason) override { events.push_back("failed " + reason); }
    } listener;
    spz::fake::Clock clock;
    spz::fake::SoundBackend backend(&clock);
    backend.set_listener(&listener);
    spz::EngineSettings settings;
    settings.sample_rate = 48000;
    std::string why;
    REQUIRE(backend.load({}, 1, settings, &why));
    backend.set_length(96000);
    backend.start();
    clock.advance(500);
    CHECK(backend.position() == 24000);
    backend.pause();
    clock.advance(500);
    CHECK(backend.position() == 24000);
    backend.resume();
    clock.advance(250);
    CHECK(backend.position() == 36000);
    backend.emit_song_ended();
    backend.emit_device_gone();
    backend.emit_failed("MSG_NO_SOUND");
    CHECK(listener.events == std::vector<std::string>{"song_ended", "device_gone", "failed MSG_NO_SOUND"});
    CHECK(backend.log == std::vector<std::string>{"load 1", "set_length 96000", "start", "pause", "resume"});
    CHECK(backend.count("load") == 1);
}

TEST_CASE("TF-021 the small fakes hold what the test gives them") {
    spz::fake::SystemVolume volume;
    int percent = 0;
    bool muted = true;
    REQUIRE(volume.get(&percent, &muted));
    CHECK(percent == 50);
    CHECK_FALSE(muted);
    CHECK(volume.set(55));
    CHECK(volume.toggle_mute());
    CHECK(volume.log == std::vector<std::string>{"get", "set 55", "toggle_mute"});
    volume.works = false;
    CHECK_FALSE(volume.set(60));

    spz::fake::Backlight backlight;
    CHECK(backlight.set(40));
    CHECK(backlight.level == 40);
    backlight.writable = false;
    CHECK_FALSE(backlight.can_change());
    CHECK_FALSE(backlight.set(50));

    spz::fake::Memory memory;
    memory.available = 449;
    CHECK(memory.available_mib() + memory.swap_free_mib() == 649);

    TempFolder source, target;
    std::ofstream(source.file("tune.sid")) << "x";
    spz::fake::Unpacker unpacker;
    unpacker.tree = source.path.string();
    CHECK(unpacker.unpack("a.7z", target.path.string(), nullptr));
    CHECK(std::filesystem::exists(target.file("tune.sid")));
    unpacker.is_available = false;
    CHECK_FALSE(unpacker.available());
}

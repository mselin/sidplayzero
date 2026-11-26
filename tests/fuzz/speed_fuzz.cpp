// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 fuzz: the speed factor file reader (SPEC-072, SPEC-202) on any bytes as speed.ini
//
// The input is the whole file in the cache folder. It is read back for the version and sample rate the
// starting files were written for, and for another rate. Whatever the bytes, the reader must end, must
// not read outside them, and may only call factors it gives "measured" when they are numbers above 0.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "spz/engine_choice.h"
#include "spz/files.h"
#include "spz/types.h"

namespace {

// A file system of one file: the input, at any path.
class OneFile : public spz::Files {
public:
    explicit OneFile(spz::Bytes bytes) : bytes_(std::move(bytes)) {}

    spz::FileStatus read(const std::string&, std::size_t limit, std::vector<std::uint8_t>* out) override {
        if (bytes_.size() > limit) return spz::FileStatus::TooLarge;
        *out = bytes_;
        return spz::FileStatus::Ok;
    }
    spz::FileStatus write(const std::string&, const std::vector<std::uint8_t>&) override { return spz::FileStatus::Failed; }
    spz::FileStatus append(const std::string&, const std::vector<std::uint8_t>&) override { return spz::FileStatus::Failed; }
    spz::FileStatus map(const std::string&, std::size_t, spz::MappedFile*) override { return spz::FileStatus::Failed; }
    spz::FileStatus sync(const std::string&) override { return spz::FileStatus::Ok; }
    spz::FileStatus rename(const std::string&, const std::string&) override { return spz::FileStatus::Failed; }
    spz::FileStatus remove(const std::string&) override { return spz::FileStatus::Ok; }
    spz::FileStatus make_folders(const std::string&) override { return spz::FileStatus::Failed; }
    spz::FileStatus list(const std::string&, std::vector<spz::FileInfo>*) override { return spz::FileStatus::NotFound; }
    spz::FileStatus info(const std::string&, spz::FileInfo* out) override {
        out->size = bytes_.size();
        return spz::FileStatus::Ok;
    }
    spz::FileStatus free_bytes(const std::string&, std::uint64_t* out) override {
        *out = 0;
        return spz::FileStatus::Ok;
    }
    spz::FileStatus block_size(const std::string&, std::uint64_t* out) override {
        *out = 4096;
        return spz::FileStatus::Ok;
    }
    bool can_write(const std::string&) override { return false; }

private:
    spz::Bytes bytes_;
};

void check(const spz::SpeedFactors& factors) {
    if (!factors.measured) return;
    if (!std::isfinite(factors.accurate) || !std::isfinite(factors.light) || factors.accurate <= 0 || factors.light <= 0) {
        std::abort();
    }
    (void)spz::choose_engine(spz::EngineChoice::Auto, factors, 3);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    OneFile files(spz::Bytes(data, data + size));
    check(spz::load_speed_factors(&files, "/cache/sidplayzero", "1.0.0", 48000));
    check(spz::load_speed_factors(&files, "/cache/sidplayzero/", "1.0.0", 44100));
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/software_backend.h"

#include "spz/not_built.h"

namespace spz {

SoftwareBackend::SoftwareBackend(Engine* engine, AudioSink* sink, Clock* clock, Executor* ui)
    : engine_(engine), sink_(sink), clock_(clock), ui_(ui) {}
SoftwareBackend::~SoftwareBackend() {}

void SoftwareBackend::set_listener(SoundBackendListener*) { SPZ_NOT_BUILT("SPEC-025"); }
bool SoftwareBackend::load(const std::vector<std::uint8_t>&, int, const EngineSettings&, std::string*) {
    SPZ_NOT_BUILT("SPEC-025");
}
void SoftwareBackend::start() { SPZ_NOT_BUILT("SPEC-070"); }
void SoftwareBackend::pause() { SPZ_NOT_BUILT("SPEC-070"); }
void SoftwareBackend::resume() { SPZ_NOT_BUILT("SPEC-070"); }
void SoftwareBackend::stop() { SPZ_NOT_BUILT("SPEC-070"); }
void SoftwareBackend::set_length(std::uint64_t) { SPZ_NOT_BUILT("SPEC-064"); }
std::uint64_t SoftwareBackend::position() const { SPZ_NOT_BUILT("SPEC-064"); }
void SoftwareBackend::set_muted(int, int, bool) { SPZ_NOT_BUILT("SPEC-074"); }
void SoftwareBackend::set_filter(int, bool) { SPZ_NOT_BUILT("SPEC-074"); }
void SoftwareBackend::set_skip(bool) { SPZ_NOT_BUILT("SPEC-066"); }
bool SoftwareBackend::registers(int, std::uint8_t[Engine::kRegisters]) { SPZ_NOT_BUILT("SPEC-080"); }
int SoftwareBackend::chips() const { SPZ_NOT_BUILT("SPEC-075"); }
BackendAbilities SoftwareBackend::can() const { SPZ_NOT_BUILT("SPEC-025"); }
std::uint64_t SoftwareBackend::underruns() const { SPZ_NOT_BUILT("SPEC-070"); }
bool SoftwareBackend::render_step() { SPZ_NOT_BUILT("SPEC-070"); }
std::size_t SoftwareBackend::buffered() const { SPZ_NOT_BUILT("SPEC-070"); }

}  // namespace spz

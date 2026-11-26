// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/engine_choice.h"

#include "spz/not_built.h"

namespace spz {

EngineKind choose_engine(EngineChoice, const SpeedFactors&, int) { SPZ_NOT_BUILT("SPEC-072"); }
const Bytes& builtin_tune() { SPZ_NOT_BUILT("SPEC-072"); }
double measure_engine(Engine*, EngineKind, int, Clock*) { SPZ_NOT_BUILT("SPEC-072"); }
bool save_speed_factors(Files*, const std::string&, const SpeedFactors&, const std::string&, int) {
    SPZ_NOT_BUILT("SPEC-072");
}
SpeedFactors load_speed_factors(Files*, const std::string&, const std::string&, int) { SPZ_NOT_BUILT("SPEC-072"); }

}  // namespace spz

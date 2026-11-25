// SPDX-License-Identifier: GPL-3.0-or-later
// Which of the two engines plays a tune (SPEC-004, SPEC-072): the setting `engine`, and for `auto` a
// measurement of how fast each engine is on this machine.
#pragma once

#include <string>

#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/files.h"
#include "spz/settings.h"
#include "spz/types.h"

namespace spz {

// The speed factors of SPEC-072: seconds of sound rendered for each second taken.
struct SpeedFactors {
    bool measured = false;   // false until the measurement is done; `auto` then means sidlite
    double accurate = 0;     // reSIDfp
    double light = 0;        // sidlite
};

// With `engine = auto`, reSIDfp plays a tune of n SID chips when its factor divided by n is at least this.
const double kAccurateFactorNeeded = 2.5;

// The engine for a tune that uses `chips` SID chips (1 to 3). `accurate` is always reSIDfp and `light`
// always sidlite; `auto` is reSIDfp when factors.accurate / chips >= 2.5, and sidlite otherwise and
// while nothing has been measured.
EngineKind choose_engine(EngineChoice choice, const SpeedFactors& factors, int chips);

// The tune the engines are measured with (SPEC-072), and that the self-test renders (SPEC-241): a few
// dozen bytes of code of the app's own, for one SID chip, with one subtune. It passes SPEC-203's check.
const Bytes& builtin_tune();

// One engine's speed factor (SPEC-072), measured on the long worker: the built-in tune is loaded with
// `kind` at `sample_rate` and rendered for 0.5 s without timing, then for three runs of 1 s each, each
// timed with clock->now_ms(); the factor is that of the slowest run. 0 if the engine refuses the tune.
double measure_engine(Engine* engine, EngineKind kind, int sample_rate, Clock* clock);

// Keeps the factors in the cache folder (SPEC-072, SPEC-134), with the app's version and the sample rate
// they were measured for, written through SPEC-132's safe write. False if they could not be written.
bool save_speed_factors(Files* files, const std::string& cache_folder, const SpeedFactors& factors,
                        const std::string& app_version, int sample_rate);

// Reads them back. `measured` is false, so that a new measurement is due, when there is no such file,
// when it cannot be read as one (SPEC-202), and when it was written by another version of the app or
// for another sample rate.
SpeedFactors load_speed_factors(Files* files, const std::string& cache_folder, const std::string& app_version,
                                int sample_rate);

}  // namespace spz

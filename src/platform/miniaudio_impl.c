/* SPDX-License-Identifier: GPL-3.0-or-later */
/* miniaudio 0.11.25 (SPEC-005), built into the program from its one pinned header (cmake/pins.cmake). The
 * options it is built with (only PulseAudio, ALSA and the null device; no decoding, encoding or engine) are
 * the compile definitions of spz_platform in src/CMakeLists.txt, which miniaudio_sink.cpp sees too. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

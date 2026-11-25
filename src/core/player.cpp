// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/player.h"

#include "spz/not_built.h"

namespace spz {

Player::Player(const PlayerSeams& seams, const PlayerOptions& options, const Settings* settings)
    : seams_(seams), options_(options), settings_(settings) {}
Player::~Player() {}

void Player::set_listing(const Listing*) { SPZ_NOT_BUILT("SPEC-063"); }
void Player::set_speed_factors(const SpeedFactors&) { SPZ_NOT_BUILT("SPEC-072"); }
void Player::set_roms(const Roms&) { SPZ_NOT_BUILT("SPEC-068"); }
void Player::set_playing_off(bool) { SPZ_NOT_BUILT("SPEC-060"); }
void Player::settings_changed() { SPZ_NOT_BUILT("SPEC-065"); }

void Player::play(const std::vector<Entry>&, std::size_t) { SPZ_NOT_BUILT("SPEC-060"); }
void Player::play_pause() { SPZ_NOT_BUILT("SPEC-060"); }
void Player::stop() { SPZ_NOT_BUILT("SPEC-060"); }
void Player::next_tune() { SPZ_NOT_BUILT("SPEC-062"); }
void Player::previous_tune() { SPZ_NOT_BUILT("SPEC-062"); }
void Player::next_subtune() { SPZ_NOT_BUILT("SPEC-061"); }
void Player::previous_subtune() { SPZ_NOT_BUILT("SPEC-061"); }
void Player::restart() { SPZ_NOT_BUILT("SPEC-066"); }
void Player::use_light_engine() { SPZ_NOT_BUILT("SPEC-079"); }

void Player::toggle_voice(int) { SPZ_NOT_BUILT("SPEC-074"); }
void Player::toggle_filter() { SPZ_NOT_BUILT("SPEC-074"); }
void Player::next_chip() { SPZ_NOT_BUILT("SPEC-074"); }
void Player::set_skip(bool) { SPZ_NOT_BUILT("SPEC-066"); }

void Player::restore(const std::vector<Entry>&, std::size_t) { SPZ_NOT_BUILT("SPEC-131"); }

PlayerStatus Player::status() const { SPZ_NOT_BUILT("SPEC-060"); }
const std::vector<Entry>& Player::queue() const { SPZ_NOT_BUILT("SPEC-062"); }
std::size_t Player::place() const { SPZ_NOT_BUILT("SPEC-062"); }

void Player::song_ended() { SPZ_NOT_BUILT("SPEC-065"); }
void Player::device_lost() { SPZ_NOT_BUILT("SPEC-078"); }
void Player::device_back() { SPZ_NOT_BUILT("SPEC-078"); }
void Player::device_gone() { SPZ_NOT_BUILT("SPEC-078"); }
void Player::failed(const std::string&) { SPZ_NOT_BUILT("SPEC-060"); }

}  // namespace spz

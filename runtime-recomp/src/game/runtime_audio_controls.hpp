#pragma once

namespace dkr::runtime::audio {

float music_volume();
void set_music_volume(float volume);
float sound_effects_volume();
void set_sound_effects_volume(float volume);
float vehicle_volume();
void set_vehicle_volume(float volume);

} // namespace dkr::runtime::audio

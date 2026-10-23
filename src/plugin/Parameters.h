#pragma once

namespace ironstack::parameters
{
// Reserve the sound-spec IDs now; add each host parameter only with its DSP.
inline constexpr auto bassVolume = "bass_vol_db";
inline constexpr auto guitarVolume = "guitar_vol_db";
inline constexpr auto bassEnabled = "bass_enabled";
inline constexpr auto guitarEnabled = "guitar_enabled";
inline constexpr auto guitarMono = "guitar_mono";
inline constexpr auto destruction = "destruction";
inline constexpr auto color = "color";
inline constexpr auto tight = "tight";
inline constexpr auto crossover = "xover_hz";
inline constexpr auto spread = "guitar_spread";
inline constexpr auto detune = "guitar_detune_cents";
inline constexpr auto impact = "impact_on";
inline constexpr auto portamento = "portamento_on";
inline constexpr auto glide = "glide_ms";
inline constexpr auto cab = "cab_on";
inline constexpr auto output = "output_db";
}

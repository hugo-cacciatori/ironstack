#pragma once

namespace ironstack
{
// One linear trim for every contribution preserves the chosen layer/branch
// balance while leaving headroom. This does not limit or reshape peaks.
inline constexpr double outputTrim = 0.75;
}

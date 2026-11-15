// The trigonometry Recast & Detour use in this package instead of the platform's C library (patches/0002:
// Detour's hooks dtMathCosf, dtMathSinf, dtMathAtan2f in DetourMath.h, and Recast's walkable-slope cosf):
// the same bits on every platform. Defined in DeterministicMath.cpp, compiled with the package's flags.
#pragma once

namespace mcpplibs::mwrap::recastnavigation::port {

float cosf(float x);
float sinf(float x);
float atan2f(float y, float x);

}  // namespace mcpplibs::mwrap::recastnavigation::port

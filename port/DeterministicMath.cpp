// Deterministic cosf, sinf and atan2f for Recast & Detour (patches/0002-deterministic-math-hooks.patch).
//
// Only IEEE-754 addition, subtraction, multiplication, division and conversion, which every platform
// rounds the same way (the package builds with -ffp-contract=off -fno-fast-math, so no multiply and add
// is fused), and integer arithmetic: no call into the C library, whose cosf, sinf and atan2f differ by an
// ulp between glibc, macOS libSystem, the Windows UCRT and emscripten's musl. Each function computes in
// double and rounds to float once; the error before that rounding is a few units of 2^-53 relative, so the
// result is the correctly rounded float or, at a value within about 2^-28 ulp of a midpoint, its
// neighbour: within 1 ulp everywhere (tests/trig_test.cpp measures it).
//
// sin and cos: x reduced by the nearest multiple k of pi/2 to r in [-pi/4, pi/4]. |x| < 2^25: Cody-Waite,
// pi/2 in three parts (28, 28 and 53 bits; k has at most 25 bits, so k * part 1 and k * part 2 are exact
// and x - k * part 1 is exact by Sterbenz). |x| >= 2^25: Payne-Hanek, x's 24-bit significand times a
// 128-bit window of 2/pi's binary expansion, in integers. Then sin(r) and cos(r) by their Taylor series to
// r^17 and r^18 (the next terms are under 2^-62 relative on |r| <= pi/4), in Horner form.
// atan2: |y| / |x| or its inverse t in [0, 1]; t near c = k/8, atan(t) = atan(c) + atan(u),
// u = (t - c) / (1 + t c), |u| <= 1/16, atan(u) by its series to u^15; then the octant.
// The constants are written by tools/port_constants.py (mpmath, 600 bits).
#include "DeterministicMath.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace mcpplibs::mwrap::recastnavigation::port {

namespace {

// pi/2 = PIO2_1 + PIO2_2 + PIO2_3 (28 + 28 + 53 bits; about 109 bits in all).
constexpr double PIO2_1 = 0x1.921fb54000000p+0;
constexpr double PIO2_2 = 0x1.10b4610000000p-30;
constexpr double PIO2_3 = 0x1.a62633145c06ep-58;
constexpr double INV_PIO2 = 0x1.45f306dc9c883p-1;
constexpr double PI = 0x1.921fb54442d18p+1;
constexpr double PIO2 = 0x1.921fb54442d18p+0;
constexpr double PIO4 = 0x1.921fb54442d18p-1;
// The result for an infinity or a NaN: one quiet NaN, the same bits everywhere (x - x or x + y would give
// x86-64's negative default NaN, arm64's positive one, or carry an operand's payload).
constexpr float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();
// Round to the nearest integer by adding and subtracting 1.5 * 2^52 (|value| < 2^51).
constexpr double TO_INTEGER = 0x1.8p52;

// The first 256 bits of 2/pi after the binary point, most significant first.
constexpr std::uint64_t TWO_OVER_PI[4] = {
    0xa2f9836e4e441529ull, 0xfc2757d1f534ddc0ull, 0xdb6295993c439041ull, 0xfe5163abdebbc561ull};

// (-1)^n / (2n+1)!, n = 1 .. 8, and (-1)^n / (2n)!, n = 1 .. 9.
constexpr double SIN[8] = {-0x1.5555555555555p-3, 0x1.1111111111111p-7,  -0x1.a01a01a01a01ap-13,
                           0x1.71de3a556c734p-19, -0x1.ae64567f544e4p-26, 0x1.6124613a86d09p-33,
                           -0x1.ae7f3e733b81fp-41, 0x1.952c77030ad4ap-49};
constexpr double COS[9] = {-0x1.0000000000000p-1, 0x1.5555555555555p-5,  -0x1.6c16c16c16c17p-10,
                           0x1.a01a01a01a01ap-16,  -0x1.27e4fb7789f5cp-22, 0x1.1eed8eff8d898p-29,
                           -0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-45,  -0x1.6827863b97d97p-53};
// atan(k/8), k = 0 .. 8, and (-1)^n / (2n+1), n = 1 .. 7.
constexpr double ATAN_AT[9] = {0x0.0p+0,
                               0x1.fd5ba9aac2f6ep-4,
                               0x1.f5b75f92c80ddp-3,
                               0x1.6f61941e4def1p-2,
                               0x1.dac670561bb4fp-2,
                               0x1.1e00babdefeb4p-1,
                               0x1.4978fa3269ee1p-1,
                               0x1.700a7c5784634p-1,
                               0x1.921fb54442d18p-1};
constexpr double ATAN[7] = {-0x1.5555555555555p-2, 0x1.999999999999ap-3,  -0x1.2492492492492p-3,
                            0x1.c71c71c71c71cp-4,  -0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4,
                            -0x1.1111111111111p-4};

template <std::size_t count>
double horner(const double (&coefficients)[count], double square) {
    double sum = coefficients[count - 1];
    for (std::size_t index = count - 1; index-- > 0;) sum = coefficients[index] + square * sum;
    return sum;
}

// sin(r) and cos(r) for |r| <= pi/4 (and a little over).
double sin_kernel(double r) {
    const double square = r * r;
    return r + r * (square * horner(SIN, square));
}
double cos_kernel(double r) {
    const double square = r * r;
    return 1.0 + square * horner(COS, square);
}

// 2^-count, 0 <= count < 1022, exactly.
double power_of_half(int count) {
    return std::bit_cast<double>(static_cast<std::uint64_t>(1023 - count) << 52);
}

struct Reduced {
    double r;              // x - quadrant * pi/2, in [-pi/4, pi/4] (a little over at most)
    std::uint32_t quadrant;  // mod 4
};

// |x| >= 2^25 (finite): Payne-Hanek. x = m 2^e (m the 24-bit significand, e >= 2); x * 2/pi modulo 4 is
// m times the bits of 2/pi from the one of weight 2 (index e - 1 after the binary point) on, the earlier
// ones adding multiples of 4. A 128-bit window W of them: x * 2/pi = m W 2^-126 (mod 4), and the bits past
// the window add less than 2^-102.
Reduced reduce_large(float x) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x) & 0x7fffffffu;
    const std::uint64_t significand = (bits & 0x7fffffu) | 0x800000u;
    const int exponent = static_cast<int>(bits >> 23) - 150;  // >= 2
    const int offset = exponent - 2;                           // the window's first bit, from 0
    const int word = offset / 64;
    const int shift = offset % 64;
    auto window = [&](int index) {
        return shift == 0 ? TWO_OVER_PI[index]
                          : (TWO_OVER_PI[index] << shift) | (TWO_OVER_PI[index + 1] >> (64 - shift));
    };
    const std::uint64_t window_high = window(word);
    const std::uint64_t window_low = window(word + 1);
    // m W modulo 2^128, m < 2^24: (high, low).
    const std::uint64_t low_part = significand * (window_low & 0xffffffffu);
    const std::uint64_t high_part = significand * (window_low >> 32);
    const std::uint64_t low = low_part + (high_part << 32);
    const std::uint64_t carry = (high_part >> 32) + (low < low_part ? 1u : 0u);
    const std::uint64_t high = significand * window_high + carry;
    // Q2.126: the quadrant is the top two bits, the fraction the 126 below, as 128 bits.
    std::uint32_t quadrant = static_cast<std::uint32_t>(high >> 62);
    std::uint64_t fraction_high = (high << 2) | (low >> 62);
    std::uint64_t fraction_low = low << 2;
    bool negative = false;
    if (fraction_high >> 63) {  // fraction >= 1/2: the next quadrant, less 1 - fraction
        quadrant += 1;
        negative = true;
        fraction_low = ~fraction_low + 1;
        fraction_high = ~fraction_high + (fraction_low == 0 ? 1u : 0u);
    }
    // The fraction's leading 64 bits, normalised, converted to double (rounded once), scaled.
    int leading = 0;
    std::uint64_t top = 0;
    if (fraction_high != 0) {
        leading = std::countl_zero(fraction_high);
        top = leading == 0 ? fraction_high : (fraction_high << leading) | (fraction_low >> (64 - leading));
    } else if (fraction_low != 0) {
        leading = std::countl_zero(fraction_low);
        top = fraction_low << leading;
        leading += 64;
    }
    double fraction = static_cast<double>(top) * power_of_half(64 + leading);
    if (negative) fraction = -fraction;
    return {fraction * PIO2, quadrant & 3u};
}

// |x| < 2^25: Cody-Waite.
Reduced reduce(float x) {
    const double value = x;
    const double magnitude = value < 0 ? -value : value;
    if (magnitude <= PIO4) return {value, 0};
    const double k = (value * INV_PIO2 + TO_INTEGER) - TO_INTEGER;
    const double r = ((value - k * PIO2_1) - k * PIO2_2) - k * PIO2_3;
    return {r, static_cast<std::uint32_t>(static_cast<std::int64_t>(k)) & 3u};
}

bool is_large(float x) {
    return (std::bit_cast<std::uint32_t>(x) & 0x7fffffffu) >= 0x4c000000u;  // |x| >= 2^25
}
bool is_finite(float x) {
    return (std::bit_cast<std::uint32_t>(x) & 0x7f800000u) != 0x7f800000u;
}
bool sign_of(float x) {
    return (std::bit_cast<std::uint32_t>(x) >> 31) != 0;
}

// sin (sine = true) or cos of a finite x.
double trig(float x, bool sine) {
    Reduced reduced{};
    bool flip = false;
    if (is_large(x)) {
        reduced = reduce_large(x);
        flip = sine && sign_of(x);  // reduce_large reduces |x|; sin is odd, cos even
    } else {
        reduced = reduce(x);
    }
    const std::uint32_t quadrant = sine ? reduced.quadrant : (reduced.quadrant + 1) & 3u;
    // sin(r + q pi/2): q = 0 sin r, 1 cos r, 2 -sin r, 3 -cos r; cos(x) = sin(x + pi/2).
    double result = (quadrant & 1u) ? cos_kernel(reduced.r) : sin_kernel(reduced.r);
    if (quadrant & 2u) result = -result;
    return flip ? -result : result;
}

// atan(t), 0 <= t <= 1.
double atan_unit(double t) {
    const int k = static_cast<int>(t * 8.0 + 0.5);
    const double c = k * 0.125;
    const double u = k == 0 ? t : (t - c) / (1.0 + t * c);
    const double square = u * u;
    return ATAN_AT[k] + (u + u * (square * horner(ATAN, square)));
}

}  // namespace

float sinf(float x) {
    if (!is_finite(x)) return NOT_A_NUMBER;
    if (x == 0) return x;  // +-0 (the kernel's r + r (r^2 P) would make -0 +0)
    return static_cast<float>(trig(x, true));
}

float cosf(float x) {
    if (!is_finite(x)) return NOT_A_NUMBER;
    return static_cast<float>(trig(x, false));
}

float atan2f(float y, float x) {
    if (y != y || x != x) return NOT_A_NUMBER;
    const bool y_negative = sign_of(y);
    const bool x_negative = sign_of(x);
    double result = 0;
    if (y == 0) {
        result = x_negative ? PI : 0.0;  // atan2(+-0, -0 or < 0) = +-pi, (+-0, +0 or > 0) = +-0
    } else if (!is_finite(y)) {
        result = !is_finite(x) ? (x_negative ? 3 * PIO4 : PIO4) : PIO2;
    } else if (!is_finite(x)) {
        result = x_negative ? PI : 0.0;
    } else if (x == 0) {
        result = PIO2;
    } else {
        const double a = y_negative ? -static_cast<double>(y) : static_cast<double>(y);
        const double b = x_negative ? -static_cast<double>(x) : static_cast<double>(x);
        const bool steep = a > b;
        result = steep ? PIO2 - atan_unit(b / a) : atan_unit(a / b);
        if (x_negative) result = PI - result;
    }
    return static_cast<float>(y_negative ? -result : result);
}

}  // namespace mcpplibs::mwrap::recastnavigation::port

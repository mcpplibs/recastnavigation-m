// The trigonometry Recast & Detour use in this package (dtMathCosf, dtMathSinf, dtMathAtan2f, calling
// port/DeterministicMath.cpp): against the platform's double sin, cos and atan2 as the reference, and its own
// results fingerprinted, the same on every platform CI builds.
//
// The error is measured in units in the last place of the float result, with the double reference (good to
// about 2^-29 of a float ulp, so it can tell a correctly rounded result, at most 0.5 ulp, from one that is
// not). Measured (2026-11-15, x86-64, against glibc's 64-bit long double sinl, cosl, atan2l): sin and cos
// over every finite float (2^32 - 2^24 of them) at most 0.500000001 ulp, 2 and 6 of them not correctly
// rounded; atan2 over 14.5 billion pairs (every float against 1 and -3, 1 against every float, a 40000 x
// 40000 grid of bit patterns) at most 0.500000001 ulp, 6 not correctly rounded. Every row here checks a
// dense sweep (a stride through every bit pattern, every float of [-4 pi, 4 pi] at a finer stride) and
// requires less than 1 ulp; RECAST_TRIG_EXHAUSTIVE=1 sweeps every float for sin and cos.
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.recastnavigation;

namespace {

float from_bits(std::uint32_t bits) {
    return std::bit_cast<float>(bits);
}

// The size of a float ulp at a value (the spacing of floats of the magnitude the reference rounds to).
double ulp_at(double reference) {
    const double magnitude = std::fabs(reference);
    const float rounded = static_cast<float>(magnitude);
    if (rounded < 0x1p-126f) return 0x1p-149;
    int exponent = 0;
    std::frexp(static_cast<double>(rounded), &exponent);  // rounded = fraction 2^exponent, fraction in [1/2, 1)
    // Below a power of two the spacing is half the one above it.
    return std::ldexp(1.0, (magnitude < std::ldexp(1.0, exponent - 1) ? exponent - 25 : exponent - 24));
}

struct Sweep {
    double worst = 0;
    float worst_at = 0;
    std::uint64_t count = 0;
    std::uint64_t not_nearest = 0;
    std::uint64_t fingerprint = 0xcbf29ce484222325ull;

    void add(float argument, float got, double reference) {
        ++count;
        if (std::isnan(reference)) {  // the one quiet NaN, folded into the fingerprint
            if (std::bit_cast<std::uint32_t>(got) != 0x7fc00000u) ++not_nearest;
            fingerprint = (fingerprint ^ 0x7f) * 0x100000001b3ull;
            return;
        }
        const double error = std::fabs(static_cast<double>(got) - reference) / ulp_at(reference);
        if (static_cast<float>(reference) != got) ++not_nearest;
        if (error > worst) {
            worst = error;
            worst_at = argument;
        }
        for (const unsigned char byte : std::bit_cast<std::array<unsigned char, 4>>(got))
            fingerprint = (fingerprint ^ byte) * 0x100000001b3ull;
    }
};

// Every finite float with bits = start + k * stride, both signs.
template <typename Each>
void every(std::uint32_t start, std::uint32_t stride, std::uint32_t end, Each each) {
    for (std::uint64_t bits = start; bits < end; bits += stride) {
        each(from_bits(static_cast<std::uint32_t>(bits)));
        each(from_bits(static_cast<std::uint32_t>(bits) | 0x80000000u));
    }
}

constexpr std::uint32_t FINITE_END = 0x7f800000u;
constexpr std::uint32_t FOUR_PI_END = 0x41490fdbu;  // the bits of 4 pi, rounded up

}  // namespace

TEST(Trig, SinCos) {
    Sweep sine, cosine;
    auto both = [&](float x) {
        sine.add(x, dtMathSinf(x), std::sin(static_cast<double>(x)));
        cosine.add(x, dtMathCosf(x), std::cos(static_cast<double>(x)));
    };
    every(0, 4093, FINITE_END, both);        // the whole range, to 2^128 (Payne-Hanek past 2^25)
    every(0x3c000000u, 61, FOUR_PI_END, both);  // the angles a crowd turns by, finer
    // Detour's own: the adaptive sampling pattern's step 2 pi / divs and half of it, divs 1 .. 32.
    for (int divs = 1; divs <= 32; ++divs) {
        const float step = (1.0f / static_cast<float>(divs)) * 3.14159265f * 2;
        both(step);
        both(step * 0.5f);
    }
    std::printf("sin: %llu values, max %.9f ulp at %a, %llu not correctly rounded, fingerprint %016llx\n",
                static_cast<unsigned long long>(sine.count), sine.worst, static_cast<double>(sine.worst_at),
                static_cast<unsigned long long>(sine.not_nearest), static_cast<unsigned long long>(sine.fingerprint));
    std::printf("cos: %llu values, max %.9f ulp at %a, %llu not correctly rounded, fingerprint %016llx\n",
                static_cast<unsigned long long>(cosine.count), cosine.worst, static_cast<double>(cosine.worst_at),
                static_cast<unsigned long long>(cosine.not_nearest), static_cast<unsigned long long>(cosine.fingerprint));
    EXPECT_LT(sine.worst, 1.0) << "sin within 1 ulp (measured 0.500000001)";
    EXPECT_LT(cosine.worst, 1.0) << "cos within 1 ulp (measured 0.500000001)";
    EXPECT_EQ(sine.fingerprint, 0xa4a160984117e80cull) << "sin: the same bits on every platform";
    EXPECT_EQ(cosine.fingerprint, 0xf19308b108d2f164ull) << "cos: the same bits on every platform";

    // An infinity or a NaN gives the one quiet NaN (0x7fc00000), whatever the platform's default NaN.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(dtMathSinf(std::numeric_limits<float>::infinity())), 0x7fc00000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(dtMathCosf(-std::numeric_limits<float>::infinity())), 0x7fc00000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(dtMathSinf(-std::numeric_limits<float>::quiet_NaN())), 0x7fc00000u);
    EXPECT_TRUE(std::signbit(dtMathSinf(-0.0f)));
    EXPECT_EQ(dtMathCosf(-0.0f), 1.0f);
    EXPECT_EQ(dtMathSinf(0x1p-149f), 0x1p-149f);
}

TEST(Trig, Atan2) {
    Sweep sweep;
    auto pair = [&](float y, float x) {
        // Each pair folded into the argument for the record of the worst: y only.
        sweep.add(y, dtMathAtan2f(y, x), std::atan2(static_cast<double>(y), static_cast<double>(x)));
    };
    every(0, 4093, FINITE_END, [&](float value) {
        pair(value, 1.0f);
        pair(1.0f, value);
        pair(value, -3.0f);
    });
    for (std::uint32_t row = 0; row < 600; ++row)
        for (std::uint32_t column = 0; column < 600; ++column)
            pair(from_bits(row * 7158278u), from_bits(column * 7158278u + 12345u));
    std::printf("atan2: %llu pairs, max %.9f ulp, %llu not correctly rounded, fingerprint %016llx\n",
                static_cast<unsigned long long>(sweep.count), sweep.worst, static_cast<unsigned long long>(sweep.not_nearest),
                static_cast<unsigned long long>(sweep.fingerprint));
    EXPECT_LT(sweep.worst, 1.0) << "atan2 within 1 ulp (measured 0.500000001)";
    EXPECT_EQ(sweep.fingerprint, 0x8fa403711fdbaba9ull) << "atan2: the same bits on every platform";

    // C's special cases (Annex F.10.1.4).
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr float pi = 3.14159265f;
    EXPECT_EQ(dtMathAtan2f(0.0f, -0.0f), pi);
    EXPECT_EQ(dtMathAtan2f(-0.0f, -0.0f), -pi);
    EXPECT_TRUE(std::signbit(dtMathAtan2f(-0.0f, 0.0f)));
    EXPECT_EQ(dtMathAtan2f(0.0f, -2.0f), pi);
    EXPECT_EQ(dtMathAtan2f(-1.0f, 0.0f), -pi / 2);
    EXPECT_EQ(dtMathAtan2f(inf, inf), pi / 4);
    EXPECT_EQ(dtMathAtan2f(-inf, -inf), -3 * pi / 4);
    EXPECT_EQ(dtMathAtan2f(5.0f, -inf), pi);
    EXPECT_TRUE(std::signbit(dtMathAtan2f(-5.0f, inf)));
    EXPECT_EQ(dtMathAtan2f(inf, 2.0f), pi / 2);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(dtMathAtan2f(-std::numeric_limits<float>::quiet_NaN(), 1.0f)), 0x7fc00000u);
}

// Every finite float, both functions (some minutes; set RECAST_TRIG_EXHAUSTIVE=1).
TEST(Trig, Exhaustive) {
    const char* wanted = std::getenv("RECAST_TRIG_EXHAUSTIVE");
    if (wanted == nullptr || std::string_view(wanted) != "1") GTEST_SKIP() << "RECAST_TRIG_EXHAUSTIVE=1 to run";
    Sweep sine, cosine;
    every(0, 1, FINITE_END, [&](float x) {
        sine.add(x, dtMathSinf(x), std::sin(static_cast<double>(x)));
        cosine.add(x, dtMathCosf(x), std::cos(static_cast<double>(x)));
    });
    std::printf("every finite float: sin max %.9f ulp at %a, cos max %.9f ulp at %a\n", sine.worst,
                static_cast<double>(sine.worst_at), cosine.worst, static_cast<double>(cosine.worst_at));
    EXPECT_LT(sine.worst, 1.0);
    EXPECT_LT(cosine.worst, 1.0);
}

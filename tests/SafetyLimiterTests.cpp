#include "TestSignals.h"
#include "engine/SafetyLimiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

using Catch::Matchers::WithinAbs;

TEST_CASE ("the limiter leaves signals below the ceiling untouched")
{
    SafetyLimiter limiter;
    limiter.prepare (48000.0);

    for (auto sample : test::sine (440.0, 0.8f, 48000.0, 4800))
        REQUIRE_THAT (limiter.processSample (sample), WithinAbs (sample, 0.0));
}

TEST_CASE ("the limiter never lets a sample past the ceiling")
{
    SafetyLimiter limiter;
    limiter.prepare (48000.0);

    // Includes the very first loud sample, which has no warning.
    for (auto sample : test::sine (440.0, 8.0f, 48000.0, 4800))
        REQUIRE (std::abs (limiter.processSample (sample)) <= SafetyLimiter::ceiling + 1.0e-6f);
}

TEST_CASE ("the limiter recovers after a loud burst")
{
    SafetyLimiter limiter;
    limiter.prepare (48000.0);

    for (int i = 0; i < 100; ++i)
        limiter.processSample (10.0f);

    // One second later the gain reduction has fully released.
    for (int i = 0; i < 48000; ++i)
        limiter.processSample (0.0f);

    REQUIRE_THAT (limiter.processSample (0.5f), WithinAbs (0.5, 0.0));
}

TEST_CASE ("the limiter turns broken values into silence")
{
    SafetyLimiter limiter;
    limiter.prepare (48000.0);

    REQUIRE_THAT (limiter.processSample (std::numeric_limits<float>::quiet_NaN()), WithinAbs (0.0, 0.0));
    REQUIRE_THAT (limiter.processSample (std::numeric_limits<float>::infinity()), WithinAbs (0.0, 0.0));
    REQUIRE_THAT (limiter.processSample (0.25f), WithinAbs (0.25, 0.0));
}

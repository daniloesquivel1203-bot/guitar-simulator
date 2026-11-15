#include "engine/InputRouting.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
juce::BigInteger channels (std::initializer_list<int> enabled)
{
    juce::BigInteger bits;

    for (auto channel : enabled)
        bits.setBit (channel);

    return bits;
}
}

TEST_CASE ("with every channel enabled the buffer index equals the device channel")
{
    const auto active = channels ({ 0, 1 });
    REQUIRE (findInputBufferIndex (active, 2, 0) == 0);
    REQUIRE (findInputBufferIndex (active, 2, 1) == 1);
}

TEST_CASE ("disabled channels below the chosen one shift its buffer index down")
{
    REQUIRE (findInputBufferIndex (channels ({ 1 }), 2, 1) == 0);
    REQUIRE (findInputBufferIndex (channels ({ 0, 3, 5 }), 8, 5) == 2);
}

TEST_CASE ("a channel that is switched off is unavailable, not swapped for another")
{
    REQUIRE (findInputBufferIndex (channels ({ 0 }), 2, 1) == -1);
    REQUIRE (findInputBufferIndex (channels ({ 1 }), 2, 0) == -1);
    REQUIRE (findInputBufferIndex (channels ({}), 2, 0) == -1);
}

TEST_CASE ("a channel the device does not have is unavailable")
{
    const auto active = channels ({ 0, 1 });
    REQUIRE (findInputBufferIndex (active, 2, 2) == -1);
    REQUIRE (findInputBufferIndex (active, 2, -1) == -1);
    REQUIRE (findInputBufferIndex (active, 0, 0) == -1);
}

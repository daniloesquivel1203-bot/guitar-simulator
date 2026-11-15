#pragma once

#include <juce_core/juce_core.h>

/** Finds where a device input channel sits in the buffers an audio device
    delivers. A device only delivers its enabled channels, packed in order, so
    the position is the number of enabled channels below the chosen one.

    @param activeChannels   one bit per device input channel, set when enabled
    @param numChannels      how many input channels the device has
    @param deviceChannel    the chosen channel, counted from 0 across all inputs
    @returns the buffer index, or -1 if the channel does not exist or is off
*/
inline int findInputBufferIndex (const juce::BigInteger& activeChannels, int numChannels, int deviceChannel) noexcept
{
    if (deviceChannel < 0 || deviceChannel >= numChannels || ! activeChannels[deviceChannel])
        return -1;

    int index = 0;

    for (int channel = 0; channel < deviceChannel; ++channel)
        if (activeChannels[channel])
            ++index;

    return index;
}

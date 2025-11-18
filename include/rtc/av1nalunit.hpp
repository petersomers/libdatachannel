#pragma once

#if RTC_ENABLE_MEDIA

#include "common.hpp"
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace rtc {

// AV1 RTP Payload Descriptor structure (RFC 9106)
// The payload descriptor is a sequence of OBUs
class AV1NalUnit : public binary {
public:
    using binary::binary;
    AV1NalUnit(const binary& data) : binary(data) {}
    AV1NalUnit(binary&& data) : binary(std::move(data)) {}

    // AV1 Descriptor Byte (First byte of the payload after RTP header)
    // 0 1 2 3 4 5 6 7
    // |S|E|Z|Y|...|N|
    // S: Start of frame flag (1 bit)
    // E: End of frame flag (1 bit)
    // Z: Zeros flag (1 bit) - Ignored for depacketizing
    // Y: Ones flag (1 bit) - Ignored for depacketizing
    // N: OBU Count flag (1 bit) - 0: One OBU, 1: Multiple OBUs

    struct Descriptor {
        uint8_t S : 1; // Start of frame
        uint8_t E : 1; // End of frame
        uint8_t Z : 1; // Zeros flag (RFC 9106, 5.1)
        uint8_t Y : 1; // Ones flag (RFC 9106, 5.1)
        uint8_t Reserved : 3; // Must be 0
        uint8_t N : 1; // OBU Count

        Descriptor() : S(0), E(0), Z(0), Y(0), Reserved(0), N(0) {}
    };

    // The entire descriptor can be 1-3 bytes:
    // 1st byte: Descriptor (S, E, Z, Y, N)
    // 2nd byte (if N=1): OBU Count
    // 3rd byte (if N=1 and OBU Count > 0): OBU Lengths (not implemented here for simplicity)

    // Parses the AV1 descriptor bytes and returns their length.
    size_t parseDescriptor();

    // The actual raw AV1 OBU data after the descriptor.
    binary payload() const;

private:
    Descriptor mDescriptor;
    size_t mDescLength = 0;

public:
    bool isStartOfFrame() const { return mDescriptor.S; }
    bool isEndOfFrame() const { return mDescriptor.E; }
    bool hasMultipleObus() const { return mDescriptor.N; }
    size_t getDescriptorLength() const { return mDescLength; }

    // Fragment generation is not strictly needed for basic depacketizing,
    // but included for completeness if needed for an eventual packetizer.
    static std::vector<binary> GenerateFragments(const std::vector<AV1NalUnit>& units,
                                                 size_t maxFragmentSize);
    std::vector<AV1NalUnit> generateFragments(size_t maxFragmentSize) const;
};

} // namespace rtc

#endif /* RTC_ENABLE_MEDIA */

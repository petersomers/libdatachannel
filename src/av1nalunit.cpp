#if RTC_ENABLE_MEDIA

#include "av1nalunit.hpp"
#include <stdexcept>
#include <algorithm>

namespace rtc {

size_t AV1NalUnit::parseDescriptor()
{
    // The AV1 descriptor is much simpler than VP8.
    // It's 1 byte (S, E, N, Z, Y) + possibly 1 byte OBU count (if N=1) + possibly OBU lengths.
    
    mDescLength = 0;
    if (empty()) {
        return 0;
    }
    const size_t totalSize = size();
    size_t offset = 0;

    const std::byte* rawPtr = data();

    // Read the mandatory first descriptor byte (S, E, Z, Y, N)
    if (offset + 1 > totalSize) {
        return 0;
    }
    uint8_t fbVal = std::to_integer<uint8_t>(rawPtr[offset]);

    // Populate the Descriptor struct
    mDescriptor.S = (fbVal >> 7) & 0x01; // S bit
    mDescriptor.E = (fbVal >> 6) & 0x01; // E bit
    mDescriptor.Z = (fbVal >> 5) & 0x01; // Z bit
    mDescriptor.Y = (fbVal >> 4) & 0x01; // Y bit
    mDescriptor.N = fbVal & 0x01;        // N bit (OBU count flag)
    
    offset++;
    mDescLength = 1;

    // Optional: If N=1, the next byte is the OBU Count (optional for depacketizing)
    if (mDescriptor.N && offset < totalSize) {
        // uint8_t obuCount = std::to_integer<uint8_t>(rawPtr[offset]);
        offset++;
        mDescLength = 2;
        
        // Note: For full compliance, if OBU count > 0, the next bytes
        // would contain OBU lengths, which are required to split the payload.
        // For basic depacketizing, we treat the rest of the payload as a single
        // AV1 frame/temporal unit, which is common.
    }

    // Keyframe detection in AV1 is based on the first OBU header (not a descriptor bit like VP8).
    // This is often handled in the decoder, not the depacketizer. We omit it here.

    return mDescLength;
}

binary AV1NalUnit::payload() const
{
    // We want to return the portion after the descriptor. 
    AV1NalUnit* self = const_cast<AV1NalUnit*>(this);
    size_t descLen = self->parseDescriptor();
    if (descLen >= size()) {
        return {};
    }
    // The actual compressed OBU data is after the descriptor bytes.
    return binary(begin() + descLen, end());
}

// Fragment generation methods (minimal implementation, similar to original)
std::vector<binary> AV1NalUnit::GenerateFragments(const std::vector<AV1NalUnit>& units,
                                                 size_t maxFragmentSize)
{
    std::vector<binary> output;
    for (auto &u : units) {
        if (u.size() <= maxFragmentSize) {
            output.push_back(u);
        } else {
            auto frags = u.generateFragments(maxFragmentSize);
            for (auto &f : frags) {
                output.push_back(std::move(f));
            }
        }
    }
    return output;
}

std::vector<AV1NalUnit> AV1NalUnit::generateFragments(size_t maxFragmentSize) const
{
    // Fragmentation logic for AV1 is complex due to OBU structure, 
    // but we use the general approach from your VP8 code for basic fragmentation.
    std::vector<AV1NalUnit> result;
    if (size() <= maxFragmentSize) {
        result.push_back(*this);
        return result;
    }

    AV1NalUnit tmp(*this);
    size_t descLen = tmp.parseDescriptor();
    if (descLen >= size()) {
        result.push_back(*this);
        return result;
    }

    binary descriptor(begin(), begin() + descLen);
    binary av1Data(begin() + descLen, end());

    size_t offset = 0;
    while (offset < av1Data.size()) {
        size_t spaceForPayload =
             (descriptor.size() >= maxFragmentSize)
                ? 0
                : (maxFragmentSize - descriptor.size());
        size_t chunk = std::min(spaceForPayload, av1Data.size() - offset);

        binary fragData;
        fragData.reserve(descriptor.size() + chunk);

        // Modify the descriptor for non-first packets:
        binary descCopy(descriptor);
        uint8_t d0 = std::to_integer<uint8_t>(descCopy[0]);
        
        if (offset != 0) {
            // For non-first fragments, the S bit must be cleared.
            d0 &= ~(1 << 7); // clear S bit
        } 
        
        // For non-last fragments, the E bit must be cleared.
        if (offset + chunk < av1Data.size()) {
            d0 &= ~(1 << 6); // clear E bit
        }
        
        descCopy[0] = std::byte(d0);
        fragData.insert(fragData.end(), descCopy.begin(), descCopy.end());

        fragData.insert(fragData.end(),
                        av1Data.begin() + offset,
                        av1Data.begin() + offset + chunk);

        offset += chunk;
        result.emplace_back(std::move(fragData));
    }

    return result;
}

} // namespace rtc

#endif
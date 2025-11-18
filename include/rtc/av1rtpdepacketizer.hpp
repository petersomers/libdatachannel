#pragma once
#include "rtpdepacketizer.hpp"
#include "av1nalunit.hpp" // New AV1 specific file

namespace rtc {

class AV1RtpDepacketizer : public RtpDepacketizer {
public:
    AV1RtpDepacketizer(size_t clockRate) : RtpDepacketizer(clockRate) {}
    
    // Inherited from RtpDepacketizer:
    void incoming(message_vector& messages, const message_callback&) override;

protected:
    message_vector buildFrame(
        std::vector<message_ptr>::iterator first,
        std::vector<message_ptr>::iterator last,
        uint8_t payloadType,
        uint32_t timestamp) override;

private:
    std::deque<message_ptr> mRtpBuffer;
};

} // namespace rtc
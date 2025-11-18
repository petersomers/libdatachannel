#pragma once
#if RTC_ENABLE_MEDIA

#include "common.hpp"
#include "mediahandler.hpp"
#include "message.hpp"

namespace rtc {

class RTC_CPP_EXPORT AV1RtpDepacketizer : public MediaHandler {
public:
	static constexpr uint32_t ClockRate = 90000; // 90 kHz for video

    AV1RtpDepacketizer() = default;
	~AV1RtpDepacketizer() override = default;
    void incoming(message_vector& messages, const message_callback&send) override;

protected:
    message_vector buildFrame(
        std::vector<message_ptr>::iterator first,
        std::vector<message_ptr>::iterator last,
        uint8_t payloadType,
        uint32_t timestamp);

private:
    std::vector<message_ptr> mRtpBuffer;
	static bool seqLess(uint16_t a, uint16_t b) {
		// Sort by ascending sequence number, with 16-bit wrap
		return (int16_t)(a - b) < 0;
    }
};

} // namespace rtc

#endif // RTC_ENABLE_MEDIA
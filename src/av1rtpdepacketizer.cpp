#if RTC_ENABLE_MEDIA

#include "av1rtpdepacketizer.hpp"
#include "impl/internals.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>

namespace rtc {

// The 'incoming' function remains essentially identical, as it handles
// generic RTP buffering, timestamp grouping, and sequence number checking.
void AV1RtpDepacketizer::incoming(message_vector &messages, const message_callback &) {
	// ... (Use the VP8RtpDepacketizer::incoming code here, it is general) ...

	// Move all non-control messages into mRtpBuffer
	messages.erase(std::remove_if(messages.begin(), messages.end(),
	                              [&](message_ptr msg) {
		                              if (msg->type == Message::Control)
			                              return false; // keep
		                              if (msg->size() < sizeof(RtpHeader)) {
			                              PLOG_VERBOSE << "Dropping too-short RTP packet, size="
			                                           << msg->size();
			                              return true;
		                              }
		                              mRtpBuffer.push_back(std::move(msg));
		                              return true;
	                              }),
	               messages.end());

	// Process RTP packets
	while (!mRtpBuffer.empty()) {
		// Get the timestamp of the first packet
		const auto &front = mRtpBuffer.front();
		auto rh = reinterpret_cast<const RtpHeader *>(front->data());
		uint32_t ts = rh->timestamp();
		uint8_t pt = rh->payloadType();

		// Collect all packets with the same timestamp
		std::vector<message_ptr> framePackets;
		bool markerFound = false;
		uint16_t expectedSeq = rh->seqNumber();
		bool missingPackets = false;

		// --- collect packets with same timestamp and check sequence numbers ---
		for (auto it = mRtpBuffer.begin(); it != mRtpBuffer.end();) {
			auto hdr = reinterpret_cast<const RtpHeader *>((*it)->data());
			if (hdr->timestamp() == ts) {
				if (hdr->seqNumber() != expectedSeq) {
					missingPackets = true;
					break;
				}
				framePackets.push_back(std::move(*it));
				if (hdr->marker())
					markerFound = true;
				expectedSeq++; // handles wrap-around
				it = mRtpBuffer.erase(it);
			} else {
				++it;
			}
		}

		if (missingPackets || !markerFound) {
			// Missing packets or frame not complete yet, wait for more packets
			mRtpBuffer.insert(mRtpBuffer.begin(), framePackets.begin(), framePackets.end());
			break;
		}
		// --- end of generic RTP buffer processing ---

		// Now build the frame
		auto frames = buildFrame(framePackets.begin(), framePackets.end(), pt, ts);
		for (auto &f : frames)
			messages.push_back(std::move(f));
	}
}

message_vector AV1RtpDepacketizer::buildFrame(std::vector<message_ptr>::iterator first,
                                              std::vector<message_ptr>::iterator last,
                                              uint8_t payloadType, uint32_t timestamp) {
	// Sort by ascending sequence number
	std::sort(first, last, [&](const message_ptr &a, const message_ptr &b) {
		auto ha = reinterpret_cast<const RtpHeader *>(a->data());
		auto hb = reinterpret_cast<const RtpHeader *>(b->data());
		return seqLess(ha->seqNumber(), hb->seqNumber());
	});

	binary frameData;
	// In AV1, we track the 'S' (Start) and 'E' (End) bits from the payload descriptor
	bool foundStart = false;

	for (auto it = first; it != last; ++it) {
		auto &pkt = *it;
		auto hdr = reinterpret_cast<const RtpHeader *>(pkt->data());

		// The AV1 payload descriptor is after the RTP header:
		size_t hdrSize = hdr->getSize() + hdr->getExtensionHeaderSize();
		size_t totalSize = pkt->size();
		if (totalSize <= hdrSize) {
			continue;
		}

		// Create an AV1NalUnit object that contains the raw payload (minus RTP header).
		AV1NalUnit nal{binary(pkt->begin() + hdrSize, pkt->end())};
		// Let it parse the AV1 descriptor bytes.
		size_t descLen = nal.parseDescriptor();

		// Check the S and E bits from the descriptor
		bool Sbit = nal.isStartOfFrame();
		bool Ebit = nal.isEndOfFrame();

		if (Sbit) {
			foundStart = true;
		}
		if (Ebit) {
			foundEnd = true;
		}

		// Now strip off the descriptor bytes and append the underlying AV1 OBU bitstream:
		size_t payloadSize = totalSize - hdrSize; // how many bytes of actual payload
		if (descLen > payloadSize) {
			continue;
		}

		// The actual compressed data is after the descriptor
		size_t offset = hdrSize + descLen;
		size_t copyLen = totalSize - offset;
		frameData.insert(frameData.end(), pkt->begin() + offset, pkt->begin() + offset + copyLen);
	}

	// AV1 Frame Validation: We require the frame to have at least one 'Start' packet,
	// and the last packet must have the RTP Marker bit set AND an 'End' bit set
	// (or rely on the Marker bit if the 'E' bit is not reliably set).

	// Check for frame start
	if (!foundStart) {
		// Discard partial frame that didn't contain a start packet.
		return {};
	}

	// Check for frame end (RTP Marker bit AND/OR Descriptor End bit)
	auto lastPktHeader = reinterpret_cast<const RtpHeader *>((last - 1)->get()->data());
	bool lastMarker = lastPktHeader->marker();

	// RFC 9106 states the Marker bit (M) MUST be set on the last packet of a frame/temporal unit.
	if (!lastMarker) {
		// return {} if M=0, discarding partial frame.
		return {};
	}

	// Although the E bit is often used, the M bit is mandated by the RFC for the frame boundary.
	// If you want to strictly require E=1 on the last packet, uncomment the following:
	/*
	// Re-parse the last packet to check the E bit
	AV1NalUnit lastNal{binary((last-1)->get()->begin() + lastPktHeader->getSize() +
	lastPktHeader->getExtensionHeaderSize(), (last-1)->get()->end())}; lastNal.parseDescriptor();
	if(!lastNal.isEndOfFrame()) {
	    return {};
	}
	*/

	// Normal return
	message_vector out;
	if (!frameData.empty()) {
		auto finfo = std::make_shared<FrameInfo>(timestamp);
		finfo->timestampSeconds =
		    std::chrono::duration<double>(double(timestamp) / double(ClockRate));
		finfo->payloadType = payloadType;

		auto msg = make_message(std::move(frameData), finfo);
		out.push_back(std::move(msg));
	}
	return out;
}

} // namespace rtc

#endif // RTC_ENABLE_MEDIA
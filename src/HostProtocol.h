#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace HostProtocol {

static const size_t FRAME_SIZE = 16;
static const uint32_t FRAME_TIMEOUT_MS = 100;

enum Opcode : uint8_t {
  HOMING = 0x01,
  SET_ID = 0x02,
  TRIM = 0x03,
  CALIBRATE_MID = 0x04,
  CTRL_POS = 0x11,
  CTRL_TOR = 0x12,
  GET_POS = 0x22,
  GET_VEL = 0x23,
  GET_CURR = 0x24,
  GET_TEMP = 0x25,
  SET_SPE = 0x31,
  SET_TOR = 0x32,
};

inline bool isKnownOpcode(uint8_t op) {
  switch (op) {
    case HOMING: case SET_ID: case TRIM: case CALIBRATE_MID:
    case CTRL_POS: case CTRL_TOR:
    case GET_POS: case GET_VEL: case GET_CURR: case GET_TEMP:
    case SET_SPE: case SET_TOR:
      return true;
    default:
      return false;
  }
}

// The existing wire format has no checksum or unique start marker. Recover
// from leading noise using the known opcode + reserved-zero header, and drop
// incomplete frames after an idle gap rather than combining two connections.
class FrameParser {
 public:
  void reset() { used_ = 0; }

  bool push(uint8_t byte, uint32_t nowMs, uint8_t out[FRAME_SIZE]) {
    if (used_ && (uint32_t)(nowMs - lastByteMs_) > FRAME_TIMEOUT_MS) reset();
    lastByteMs_ = nowMs;
    buffer_[used_++] = byte;

    while (used_ >= 2 && (!isKnownOpcode(buffer_[0]) || buffer_[1] != 0)) {
      --used_;
      memmove(buffer_, buffer_ + 1, used_);
    }
    if (used_ < FRAME_SIZE) return false;

    memcpy(out, buffer_, FRAME_SIZE);
    reset();
    return true;
  }

 private:
  uint8_t buffer_[FRAME_SIZE] = {};
  size_t used_ = 0;
  uint32_t lastByteMs_ = 0;
};

}  // namespace HostProtocol

#include "HostProtocol.h"

#include <array>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

using namespace HostProtocol;
using Frame = std::array<uint8_t, FRAME_SIZE>;

static Frame makeFrame(uint8_t opcode) {
  Frame frame = {};
  frame[0] = opcode;
  return frame;
}

static void feed(FrameParser& parser, const uint8_t* data, size_t size,
                 uint32_t now, std::vector<Frame>& received) {
  Frame out;
  for (size_t i = 0; i < size; ++i) {
    if (parser.push(data[i], now, out.data())) received.push_back(out);
  }
}

int main() {
  const Frame query = makeFrame(GET_TEMP);

  // A cold boot with 1..15 stray bytes used to require a reset/reflash.
  for (size_t offset = 0; offset < FRAME_SIZE; ++offset) {
    FrameParser parser;
    std::vector<Frame> received;
    uint8_t noise[FRAME_SIZE] = {};
    feed(parser, noise, offset, 0, received);
    for (int i = 0; i < 100; ++i) {
      feed(parser, query.data(), query.size(), i + 1, received);
    }
    assert(received.size() == 100);
    for (const auto& frame : received) assert(frame == query);
  }

  // Host probes, boot text and unknown opcodes must not become commands.
  {
    FrameParser parser;
    std::vector<Frame> received;
    const uint8_t noise[] = "AT\r\n[BOOT] Hand Type: LEFT_HAND\r\n\xff\xff";
    feed(parser, noise, sizeof(noise), 0, received);
    feed(parser, query.data(), query.size(), 1, received);
    assert(received.size() == 1 && received[0] == query);
  }

  // Keep arbitrary payload bytes intact, including opcode-like byte pairs,
  // across fragmented USB packets and back-to-back frames.
  {
    FrameParser parser;
    std::vector<Frame> received;
    Frame control = makeFrame(CTRL_POS);
    for (size_t i = 2; i < control.size(); ++i) control[i] = i % 2 ? 0 : HOMING;
    feed(parser, control.data(), 5, 0, received);
    feed(parser, control.data() + 5, 11, 10, received);
    feed(parser, query.data(), query.size(), 10, received);
    assert(received.size() == 2 && received[0] == control && received[1] == query);
  }

  // A disconnected host can leave a partial frame. Test the timeout both
  // normally and across the millis() counter wrapping.
  for (uint32_t start : {uint32_t(0), UINT32_MAX - 50}) {
    FrameParser parser;
    std::vector<Frame> received;
    Frame partial = makeFrame(CTRL_POS);
    feed(parser, partial.data(), 7, start, received);
    feed(parser, query.data(), query.size(), start + FRAME_TIMEOUT_MS + 1, received);
    assert(received.size() == 1 && received[0] == query);
  }

  // A recognized opcode with a nonzero reserved byte is still noise.
  {
    FrameParser parser;
    std::vector<Frame> received;
    const uint8_t noise[] = {CTRL_POS, 0xff, 0xff, GET_POS, 0xff};
    feed(parser, noise, sizeof(noise), 0, received);
    feed(parser, query.data(), query.size(), 1, received);
    assert(received.size() == 1 && received[0] == query);
  }

  // All existing SDK commands retain their original 16-byte wire format.
  {
    FrameParser parser;
    std::vector<Frame> received;
    const uint8_t commands[] = {HOMING, SET_ID, TRIM, CALIBRATE_MID, CTRL_POS,
      CTRL_TOR, GET_POS, GET_VEL, GET_CURR, GET_TEMP, SET_SPE, SET_TOR};
    for (uint8_t op : commands) {
      Frame frame = makeFrame(op);
      feed(parser, frame.data(), frame.size(), 0, received);
      assert(received.back() == frame);
    }
    assert(received.size() == sizeof(commands));
  }

  puts("Host protocol regression tests passed");
}

#pragma once
#include "unity_protocol.h"
namespace kojo {
enum class PairKind : uint8_t { Request = 1, Offer, Commit, Confirm, Discover, Found };
#pragma pack(push, 1)
struct PairFrame {
  uint8_t magic[2] = {0x4b, 0x50};
  uint8_t version = 1;
  PairKind kind = PairKind::Request;
  uint64_t device = 0, host = 0, request = 0, session = 0;
  uint8_t slot = 0, group = 0, red = 0, green = 0, blue = 0, reserved[3] = {};
  uint32_t crc = 0;
};
template<typename T> struct Envelope {
  uint8_t magic[2] = {0x4b, 0x44};
  uint8_t version = 1, length = sizeof(T) + 24;
  uint64_t device = 0, session = 0;
  T payload;
  uint32_t crc = 0;
};
#pragma pack(pop)
static_assert(sizeof(PairFrame) == 48, "pair frame layout");
static_assert(sizeof(Envelope<Packet>) == 88, "sample envelope layout");
static_assert(sizeof(Envelope<GameAcknowledgement>) == 40, "feedback envelope layout");
inline void seal(PairFrame& frame) { frame.crc = crc32(reinterpret_cast<const uint8_t*>(&frame), 44); }
inline bool valid(const PairFrame& frame) {
  return frame.magic[0] == 0x4b && frame.magic[1] == 0x50 && frame.version == 1 &&
    static_cast<uint8_t>(frame.kind) >= 1 && static_cast<uint8_t>(frame.kind) <= 6 && frame.device && frame.request &&
    frame.slot < 2 && frame.group < 3 && !frame.reserved[0] && !frame.reserved[1] && !frame.reserved[2] &&
    frame.crc == crc32(reinterpret_cast<const uint8_t*>(&frame), 44);
}
template<typename T> inline void seal(Envelope<T>& frame) {
  frame.crc = crc32(reinterpret_cast<const uint8_t*>(&frame), sizeof(frame) - 4);
}
template<typename T> inline bool valid(const Envelope<T>& frame, uint64_t device, uint64_t session) {
  return device && session && frame.magic[0] == 0x4b && frame.magic[1] == 0x44 && frame.version == 1 &&
    frame.length == sizeof(frame) && frame.device == device && frame.session == session &&
    frame.crc == crc32(reinterpret_cast<const uint8_t*>(&frame), sizeof(frame) - 4);
}
}

namespace kojo {
// Millisecond arithmetic is unsigned so retries and holds survive millis() wrap.
class PairingClient {
public:
  uint64_t device = 0;
  PairFrame binding;
  uint32_t hostIp = 0;
  bool changed = false, conflict = false;
  enum class State { Idle, Requesting, Collecting, Committing };
  State state = State::Idle;
  void begin(uint64_t nonce, uint32_t now) {
    request = nonce ? nonce : 1; started = now; sentAt = now - 500;
    state = State::Requesting; conflict = false; candidate = PairFrame{}; candidateIp = 0;
  }
  bool bound() const { return binding.host && binding.session && binding.device == device; }
  bool active() const { return state != State::Idle; }
  bool next(uint32_t now, bool connected, uint64_t nonce, PairFrame& frame, uint32_t& destination) {
    if (active() && now - started >= 10000) { state = State::Idle; return false; }
    if (state == State::Collecting && now - offeredAt >= 1000) state = State::Committing;
    if (active()) {
      if (now - sentAt < 400) return false;
      sentAt = now;
      if (state == State::Committing) { frame = candidate; frame.kind = PairKind::Commit; destination = candidateIp; }
      else { frame = PairFrame{}; frame.device = device; frame.request = request; destination = 0; }
    } else {
      if (!bound() || connected || now - discoveredAt < 2000) return false;
      discoveredAt = now; discovery = nonce ? nonce : 1;
      frame = binding; frame.kind = PairKind::Discover; frame.request = discovery; destination = 0;
    }
    seal(frame); return true;
  }
  bool receive(const PairFrame& frame, uint32_t ip, uint32_t now) {
    if (!valid(frame) || frame.device != device) return false;
    if (frame.kind == PairKind::Offer && (state == State::Requesting || state == State::Collecting)
        && frame.request == request && frame.host && frame.session) {
      if (state == State::Requesting) {
        candidate = frame; candidateIp = ip; offeredAt = now; state = State::Collecting;
      } else if (!matches(frame, candidate) || candidateIp != ip) {
        state = State::Idle; conflict = true;
      }
    } else if (frame.kind == PairKind::Confirm && state == State::Committing && ip == candidateIp && matches(frame, candidate)) {
      binding = frame; hostIp = ip; changed = true; state = State::Idle;
      return true;
    } else if (frame.kind == PairKind::Found && !active() && bound() && frame.request == discovery && discovery
        && frame.host == binding.host && frame.session == binding.session && frame.slot == binding.slot && frame.group == binding.group) {
      if (hostIp != ip) { hostIp = ip; changed = true; }
    }
    return false;
  }
private:
  uint64_t request = 0, discovery = 0;
  PairFrame candidate;
  uint32_t candidateIp = 0, started = 0, sentAt = 0, offeredAt = 0, discoveredAt = 0;
  static bool matches(const PairFrame& a, const PairFrame& b) {
    return a.device == b.device && a.host == b.host && a.request == b.request && a.session == b.session && a.slot == b.slot && a.group == b.group;
  }
};

// Once a chord starts, mask both buttons until both are released.
class PairingButtons {
public:
  bool masked = false;
  bool update(bool guard, bool reset, uint32_t now) {
    if (!guard && !reset) { masked = fired = holding = false; return false; }
    if (guard && reset) {
      masked = true;
      if (!holding) { holding = true; since = now; }
      if (!fired && now - since >= 1000) { fired = true; return true; }
    } else holding = false;
    return false;
  }
private:
  bool fired = false, holding = false;
  uint32_t since = 0;
};
}

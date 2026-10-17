#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace kojo {
enum Flags : uint8_t {
  Guard = 1, OrientationValid = 2, GyroscopeValid = 4, AccelerationValid = 8,
  RecenterHeld = 16
};

// ESP32 and the supported Unity hosts are little-endian IEEE-754 platforms.
#pragma pack(push, 1)
struct Packet {
  uint8_t magic[2] = {0x4b, 0x4a};
  uint8_t version = 1;
  uint8_t length = 64;
  uint8_t group = 0;
  uint8_t slot = 0;
  uint8_t flags = 0;
  uint8_t reserved = 0;
  uint32_t sequence = 0;
  uint32_t timestampMs = 0;
  uint16_t recenterSequence = 0;
  uint16_t reserved2 = 0;
  float rotation[4] = {0, 0, 0, 1};
  float angularVelocity[3] = {0, 0, 0};
  float linearAcceleration[3] = {0, 0, 0};
  uint32_t crc = 0;
};
struct GameAcknowledgement {
  uint8_t magic[2] = {0x4b, 0x41};
  uint8_t version = 1;
  uint8_t length = 16;
  uint8_t group = 0;
  uint8_t slot = 0;
  // Feedback duration in milliseconds. Zero is the ordinary heartbeat.
  uint16_t vibrationMs = 0;
  uint32_t sequence = 0;
  uint32_t crc = 0;
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 64, "Controller frame must contain exactly 64 bytes");
static_assert(offsetof(Packet, crc) == 60, "CRC covers the first 60 bytes");
static_assert(sizeof(GameAcknowledgement) == 16, "Game acknowledgement must contain 16 bytes");

inline uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc ^ 0xffffffffu;
}

inline void seal(Packet& packet) {
  packet.crc = crc32(reinterpret_cast<const uint8_t*>(&packet), 60);
}
inline bool valid(const Packet& packet) {
  return packet.magic[0] == 0x4b && packet.magic[1] == 0x4a &&
         packet.version == 1 && packet.length == 64 && packet.group < 3 &&
         packet.slot < 2 && packet.reserved == 0 && packet.reserved2 == 0 &&
         (packet.flags & 0xe0) == 0 &&
         packet.crc == crc32(reinterpret_cast<const uint8_t*>(&packet), 60);
}
inline bool valid(const GameAcknowledgement& ack, const Packet& sent) {
  return ack.magic[0] == 0x4b && ack.magic[1] == 0x41 && ack.version == 1 &&
         ack.length == sizeof(ack) && ack.group == sent.group && ack.slot == sent.slot &&
         ack.vibrationMs <= 1000 && ack.crc == crc32(reinterpret_cast<const uint8_t*>(&ack), 12) &&
         static_cast<uint32_t>(sent.sequence - ack.sequence) <= 150;
}
}

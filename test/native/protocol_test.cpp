#include "../../include/unity_protocol.h"
#include <assert.h>
#include <initializer_list>
int main() {
  const uint8_t check[] = "123456789";
  assert(kojo::crc32(check, 9) == 0xcbf43926u);
  kojo::Packet packet;
  packet.group = 2;
  packet.slot = 1;
  packet.sequence = 0xffffffffu;
  packet.flags = kojo::Guard | kojo::OrientationValid;
  kojo::seal(packet);
  assert(kojo::valid(packet));
  packet.flags |= kojo::RecenterHeld;
  kojo::seal(packet);
  assert(kojo::valid(packet));
  packet.flags |= 0x20;
  kojo::seal(packet);
  assert(!kojo::valid(packet));
  packet.flags &= ~0x20;
  kojo::seal(packet);
  packet.rotation[0] = 0.5f;
  assert(!kojo::valid(packet));
  kojo::seal(packet);
  assert(kojo::valid(packet));
  packet.flags |= kojo::AccelerationValid;
  packet.linearAcceleration[0] = 3.25f;
  kojo::seal(packet);
  assert(kojo::valid(packet));
  packet.linearAcceleration[0] = 0.0f;
  assert(!kojo::valid(packet));
  kojo::seal(packet);
  packet.group = 3;
  kojo::seal(packet);
  assert(!kojo::valid(packet));
  packet.group = 2;
  packet.sequence = 12;
  kojo::GameAcknowledgement ack;
  ack.group = packet.group; ack.slot = packet.slot; ack.sequence = packet.sequence;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  assert(kojo::valid(ack, packet));
  ack.slot = 0;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  assert(!kojo::valid(ack, packet));
  ack.slot = packet.slot; ack.sequence = 13;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  assert(!kojo::valid(ack, packet));
  ack.sequence = 12;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  packet.sequence = 200;
  assert(!kojo::valid(ack, packet));
  packet.sequence = 0; ack.sequence = 0xffffffffu;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  assert(kojo::valid(ack, packet));
  for (uint16_t duration : {200, 400}) {
    ack.vibrationMs = duration;
    ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
    assert(kojo::valid(ack, packet));
  }
  ack.vibrationMs = 1001;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  assert(!kojo::valid(ack, packet));
  ack.vibrationMs = 400;
  ack.crc = kojo::crc32(reinterpret_cast<const uint8_t*>(&ack), 12);
  ack.crc ^= 1;
  assert(!kojo::valid(ack, packet));
}

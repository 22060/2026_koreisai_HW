#include <cassert>
#include <cstring>
#include "../../include/controller_pairing.h"
using namespace kojo;
int main() {
  PairFrame frame; frame.kind = PairKind::Offer; frame.device = 11; frame.host = 33;
  frame.request = 22; frame.session = 44; frame.slot = 1; frame.red = 55; seal(frame);
  assert(valid(frame));
  // Native and C# tests share this exact frame's CRC.
  assert(frame.crc == 0x097db479u);
  const auto original = frame;
  for (size_t i = 0; i < sizeof(frame); ++i) {
    reinterpret_cast<uint8_t*>(&frame)[i] ^= 1; assert(!valid(frame)); frame = original;
  }
  PairingClient client; client.device = 11; client.begin(22, 100);
  PairFrame outgoing; uint32_t target;
  assert(client.next(100, false, 99, outgoing, target) && target == 0 && outgoing.kind == PairKind::Request);
  client.receive(frame, 0x01020304, 200);
  assert(client.next(1100, false, 99, outgoing, target) && outgoing.kind == PairKind::Request);
  assert(client.next(1500, false, 99, outgoing, target) && outgoing.kind == PairKind::Commit && target == 0x01020304);
  frame.kind = PairKind::Confirm; seal(frame);
  assert(client.receive(frame, 0x01020304, 1600)); assert(client.bound() && client.changed && client.binding.slot == 1);
  assert(client.next(4000, false, 66, outgoing, target) && outgoing.kind == PairKind::Discover && target == 0);
  outgoing.kind = PairKind::Found; seal(outgoing);
  client.receive(outgoing, 0x02030405, 4100); assert(client.hostIp == 0x02030405);
  client.begin(77, 5000); frame = original; frame.request = 77; seal(frame); client.receive(frame, 1, 5100);
  frame.host++; seal(frame); client.receive(frame, 2, 5200);
  assert(client.conflict && !client.active() && client.bound());
  client.begin(88, 6000); assert(!client.next(16000, false, 99, outgoing, target) && !client.active() && client.bound());
  Envelope<Packet> sample; sample.device = 11; sample.session = 44; seal(sample);
  assert(valid(sample, 11, 44)); assert(!valid(sample, 11, 45));
  reinterpret_cast<uint8_t*>(&sample)[30] ^= 1; assert(!valid(sample, 11, 44));
  PairingButtons buttons;
  assert(!buttons.update(true, true, 0)); assert(buttons.masked);
  assert(!buttons.update(true, true, 999)); assert(buttons.update(true, true, 1000));
  assert(!buttons.update(true, true, 2000)); assert(!buttons.update(false, true, 2100) && buttons.masked);
  assert(!buttons.update(false, false, 2200) && !buttons.masked);
  assert(!buttons.update(true, true, 0xfffffff0u)); assert(buttons.update(true, true, 0x3d8u));
}

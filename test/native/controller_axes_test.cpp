#include "../../include/controller_axes.h"
#include <assert.h>
#include <math.h>

namespace {
struct Vector { float x, y, z; };
struct Quaternion { float x, y, z, w; };

Quaternion multiply(Quaternion a, Quaternion b) {
  return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
          a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
          a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Quaternion inverse(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }
Vector rotate(Quaternion q, Vector v) {
  Quaternion r = multiply(multiply(q, {v.x, v.y, v.z, 0}), inverse(q));
  return {r.x, r.y, r.z};
}
Quaternion decodeController(Quaternion bno) {
  float norm = sqrtf(bno.x*bno.x+bno.y*bno.y+bno.z*bno.z+bno.w*bno.w);
  float packet[4];
  kojo::controllerRotation(bno.x, bno.y, bno.z, bno.w, 1/norm, packet);
  // MotionPacketCodec's unchanged phone/hardware handedness conversion.
  return {-packet[0], -packet[1], packet[2], packet[3]};
}
Vector angularVelocity(Quaternion delta, float seconds) {
  if (delta.w < 0) delta = {-delta.x, -delta.y, -delta.z, -delta.w};
  float n = sqrtf(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
  float scale = 2*atan2f(n, delta.w)*180/3.14159265358979323846f/n/seconds;
  return {delta.x*scale, delta.y*scale, delta.z*scale};
}
void close(float actual, float expected) { assert(fabsf(actual-expected) < .001f); }
void close(Vector actual, Vector expected) {
  close(actual.x, expected.x); close(actual.y, expected.y); close(actual.z, expected.z);
}

struct Swing {
  Quaternion before, after;
  float seconds;
  bool up;
};
// Adjacent raw BNO085 reports from the same-grip up/up/right/right capture.
// Both quaternion signs are exercised below for each recorded movement.
const Swing swings[] = {
  {{.020203065f,.986593187f,-.019958919f,.160708979f},
   {.030151824f,.960707903f,-.009338520f,.275761008f},.016f,true},
  {{.043030076f,.950628996f,-.025451833f,.306276500f},
   {.051574122f,.887807310f,-.026672062f,.456537813f},.017f,true},
  {{.242679864f,.967789710f,-.066590473f,.007446414f},
   {.334234327f,.939225316f,-.073060356f,.028381841f},.017f,false},
  {{.498773634f,.863149405f,-.075926878f,.020812752f},
   {.591554821f,.799624443f,-.085449524f,.057983607f},.018f,false},
};

void recordedSwingsNavigateAndMoveSwordInTheSameDirection() {
  // The reset pose preceding all four swings, in decoded Unity coordinates.
  const Quaternion reference = {.070984125f,.000366212f,.039184701f,.996707439f};
  for (const Swing& swing : swings) {
    Quaternion before = decodeController(swing.before), after = decodeController(swing.after);
    Vector world = angularVelocity(multiply(after, inverse(before)), swing.seconds);
    // Hardware menus use the same fixed reference as gameplay, rather than
    // the BNO startup world heading. The directional gate remains 360 deg/s.
    Vector menu = rotate(inverse(reference), world);
    if (swing.up) {
      assert(-menu.x > 360 && fabsf(menu.x) > fabsf(menu.y));
    } else {
      assert(menu.y > 360 && fabsf(menu.y) > fabsf(menu.x));
    }
    // Recentring cancels the world-frame correction. The sword's local axes
    // must still mean pitch up (-X) and swing right (+Y), with +Z for thrust.
    Vector local = angularVelocity(multiply(inverse(before), after), swing.seconds);
    if (swing.up) assert(local.x < -360 && fabsf(local.x) > fabsf(local.y));
    else assert(local.y > 360 && fabsf(local.y) > fabsf(local.x));

    // Quaternion signs are equivalent; crossing the sign boundary cannot
    // generate an opposite gesture or a spurious full-turn angular velocity.
    after = {-after.x, -after.y, -after.z, -after.w};
    close(angularVelocity(multiply(after, inverse(before)), swing.seconds), world);
    Vector reverse = angularVelocity(multiply(before, inverse(after)), swing.seconds);
    close(reverse, {-world.x, -world.y, -world.z});
  }
}

void localAccelerationAndOrientationRemainCoherent() {
  for (const Swing& swing : swings) {
    Quaternion q = decodeController(swing.after);
    close(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w, 1);
    Vector raw = {2.4f, -1.7f, 6.2f};
    float packet[3];
    kojo::controllerAcceleration(raw.x, raw.y, raw.z, packet);
    Vector actual = rotate(q, {packet[0], packet[1], -packet[2]});
    Quaternion oldUnity = {-swing.after.y, -swing.after.z, swing.after.x, swing.after.w};
    Vector oldWorld = rotate(oldUnity, {raw.y, raw.z, -raw.x});
    // Local basis changes cancel between pose and acceleration. Only the
    // measured world-frame Y half-turn remains, regardless of current pose.
    close(actual, {-oldWorld.x, oldWorld.y, -oldWorld.z});
  }
  float thrust[3];
  kojo::controllerAcceleration(-10.85f, 1.51f, -1.27f, thrust);
  assert(-thrust[2] > 10); // Recorded forward thrust still reaches sword +Z.
  assert(fabsf(thrust[0]) < -thrust[2] && fabsf(thrust[1]) < -thrust[2]);
}
} // namespace

int main() {
  recordedSwingsNavigateAndMoveSwordInTheSameDirection();
  localAccelerationAndOrientationRemainCoherent();
}

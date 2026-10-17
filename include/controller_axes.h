#pragma once

namespace kojo {

// Calibration for the controller's BNO085 mounting, measured against up/right
// swings and a forward thrust. The original packet frame was (j,k,i,w).
// Rotate its world frame 180 degrees about Y, and its controller frame
// 180 degrees about Z for recentered sword movement.
// Applying world * rotation * inverse(controller) gives the quaternion below
// (with the equivalent opposite sign). The world and controller corrections
// differ; conjugating with one basis would lose the calibrated local axes.
inline void controllerRotation(float i, float j, float k, float real,
                               float inverseNorm, float output[4]) {
  output[0] = real * inverseNorm;
  output[1] = -i * inverseNorm;
  output[2] = -k * inverseNorm;
  output[3] = j * inverseNorm;
}

// Acceleration is a controller-local vector, so only the controller-frame
// rotation applies. Unity subsequently reflects packet Z for its handedness.
inline void controllerAcceleration(float x, float y, float z, float output[3]) {
  output[0] = -y;
  output[1] = -z;
  output[2] = x;
}

} // namespace kojo

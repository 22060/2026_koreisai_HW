#ifdef GYRO_TEST
#include <Arduino.h>
// This demo explores two reports (SH2_ARVR_STABILIZED_RV and SH2_GYRO_INTEGRATED_RV) both can be used to give 
// quartenion and euler (yaw, pitch roll) angles.  Toggle the FAST_MODE define to see other report.  
// Note sensorValue.status gives calibration accuracy (which improves over time)
#include <Adafruit_BNO08x.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

const char* WIFI_SSID = "Xiaomi 14T Pro";
const char* WIFI_PASS = "g2k4bcc25ti2xbf";

const char* SERVER_IP = "192.168.1.100";
const uint16_t SERVER_PORT = 8081;

const char* CONTROLLER_ID = "controller_001";

WebSocketsClient webSocket;
bool calibrateRequested = false;

float refW = 1;
float refX = 0;
float refY = 0;
float refZ = 0;
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length)
{
    switch(type)
    {
        case WStype_CONNECTED:
            Serial.println("WS Connected");
            break;

        case WStype_DISCONNECTED:
            Serial.println("WS Disconnected");
            break;
        case WStype_TEXT:
        {
            String msg =
                (char*)payload;

            Serial.println(msg);

            if(msg.indexOf("calibrate") >= 0)
            {
                calibrateRequested = true;
            }

            break;
        }
        default:
            break;
    }
}


// For SPI mode, we need a CS pin
// #define BNO08X_CS 10
// #define BNO08X_INT 9


// #define FAST_MODE

// For SPI mode, we also need a RESET 
//#define BNO08X_RESET 5
// but not for I2C or UART
#define BNO08X_RESET -1

struct euler_t {
  float yaw;
  float pitch;
  float roll;
} ypr;

Adafruit_BNO08x  bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

#ifdef FAST_MODE
  // Top frequency is reported to be 1000Hz (but freq is somewhat variable)
  sh2_SensorId_t reportType = SH2_GYRO_INTEGRATED_RV;
  long reportIntervalUs = 2000;
#else
  // Top frequency is about 250Hz but this report is more accurate
  sh2_SensorId_t reportType = SH2_ARVR_STABILIZED_RV;
  long reportIntervalUs = 5000;
#endif
void setReports(sh2_SensorId_t reportType, long report_interval) {
  Serial.println("Setting desired reports");
  if (! bno08x.enableReport(reportType, report_interval)) {
    Serial.println("Could not enable stabilized remote vector");
  }
}

void setup(void) {

  Serial.begin(115200);
  while (!Serial) delay(10);     // will pause Zero, Leonardo, etc until serial console opens

  Serial.println("Adafruit BNO08x test!");

  // Try to initialize!
  if (!bno08x.begin_I2C(0x4B)) {
  //if (!bno08x.begin_UART(&Serial1)) {  // Requires a device with > 300 byte UART buffer!
  //if (!bno08x.begin_SPI(BNO08X_CS, BNO08X_INT)) {
    Serial.println("Failed to find BNO08x chip");
    while (1) { delay(10); }
  }
  Serial.println("BNO08x Found!");


  setReports(reportType, reportIntervalUs);

  Serial.println("Reading events");
  delay(100);
  Serial.println("Connecting WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while(WiFi.status() != WL_CONNECTED)
  {
      delay(500);
      Serial.print(".");
  }

  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  webSocket.beginSSL("koreisai-game.ryutolab.com", 443, "/ws");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void quaternionToEuler(float qr, float qi, float qj, float qk, euler_t* ypr, bool degrees = false) {

    float sqr = sq(qr);
    float sqi = sq(qi);
    float sqj = sq(qj);
    float sqk = sq(qk);

    ypr->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
    ypr->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
    ypr->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

    if (degrees) {
      ypr->yaw *= RAD_TO_DEG;
      ypr->pitch *= RAD_TO_DEG;
      ypr->roll *= RAD_TO_DEG;
    }
}

void quaternionToEulerRV(sh2_RotationVectorWAcc_t* rotational_vector, euler_t* ypr, bool degrees = false) {
    quaternionToEuler(rotational_vector->real, rotational_vector->i, rotational_vector->j, rotational_vector->k, ypr, degrees);
}

void quaternionToEulerGI(sh2_GyroIntegratedRV_t* rotational_vector, euler_t* ypr, bool degrees = false) {
    quaternionToEuler(rotational_vector->real, rotational_vector->i, rotational_vector->j, rotational_vector->k, ypr, degrees);
}
float currentW,currentX,currentY,currentZ;
void loop() {
  webSocket.loop();
  if (bno08x.wasReset()) {
    Serial.print("sensor was reset ");
    setReports(reportType, reportIntervalUs);
  }
  
  if (bno08x.getSensorEvent(&sensorValue))
  {
      float qw, qx, qy, qz;

      switch (sensorValue.sensorId)
      {
          case SH2_ARVR_STABILIZED_RV:

              qw = sensorValue.un.arvrStabilizedRV.real;
              qx = sensorValue.un.arvrStabilizedRV.i;
              qy = sensorValue.un.arvrStabilizedRV.j;
              qz = sensorValue.un.arvrStabilizedRV.k;

              break;

          case SH2_GYRO_INTEGRATED_RV:

              qw = sensorValue.un.gyroIntegratedRV.real;
              qx = sensorValue.un.gyroIntegratedRV.i;
              qy = sensorValue.un.gyroIntegratedRV.j;
              qz = sensorValue.un.gyroIntegratedRV.k;

              break;

          default:
              return;
      }
      currentW = qw;
      currentX = qx;
      currentY = qy;
      currentZ = qz;
      if(calibrateRequested)
      {
          refW = qw;
          refX = qx;
          refY = qy;
          refZ = qz;

          calibrateRequested = false;

          Serial.println("Calibrated");
      }
      float invW = refW;
      float invX = -refX;
      float invY = -refY;
      float invZ = -refZ;
      float outW =
          qw*invW -
          qx*invX -
          qy*invY -
          qz*invZ;

      float outX =
          qw*invX +
          qx*invW +
          qy*invZ -
          qz*invY;

      float outY =
          qw*invY -
          qx*invZ +
          qy*invW +
          qz*invX;

      float outZ =
          qw*invZ +
          qx*invY -
          qy*invX +
          qz*invW;
      char json[256];

      snprintf(
          json,
          sizeof(json),
          "{\"id\":\"%s\","
          "\"quat\":{"
          "\"w\":%.6f,"
          "\"x\":%.6f,"
          "\"y\":%.6f,"
          "\"z\":%.6f"
          "},"
          "\"accuracy\":%d"
          "}",
          CONTROLLER_ID,
          outW,
          outX,
          outY,
          outZ,
          sensorValue.status
      );

      webSocket.sendTXT(json);
      // Serial.println("sensor_send");
  }

}

#endif
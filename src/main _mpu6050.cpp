#ifdef GYRO_TEST2
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "kj_wifi_config.h"

// MPU6050 および DMP制御用のライブラリ
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"

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
            String msg = (char*)payload;
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

// MPU6050のインスタンスとDMP制御用変数
MPU6050 mpu(0x68);
bool dmpReady = false;  // DMPの初期化成否フラグ
uint16_t packetSize;    // DMPパケットサイズ
uint8_t fifoBuffer[64]; // FIFO格納バッファ
Quaternion q;           // クォータニオン構造体 [w, x, y, z]

struct euler_t {
  float yaw;
  float pitch;
  float roll;
} ypr;

void setup(void) {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("MPU6050 DMP test!");

  // I2Cバスの初期化 (ESP32のデフォルトピン SDA:21, SCL:22)
  Wire.begin(0, 1);
//   pinMode(0, INPUT_PULLUP); // SDAピン
//   pinMode(1, INPUT_PULLUP); // SCLピン
  Wire.setClock(400000); // 400kHzの高速度モード

  // MPU6050の初期化
  mpu.initialize();
  Serial.println(mpu.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");

  // DMPの初期化
  uint8_t devStatus = mpu.dmpInitialize();

  // 必要に応じて手動オフセットを設定（以下は初期値。個体に合わせて調整すると精度が上がります）
  mpu.setXAccelOffset(0);
  mpu.setYAccelOffset(0);
  mpu.setZAccelOffset(0);
  mpu.setXGyroOffset(0);
  mpu.setYGyroOffset(0);
  mpu.setZGyroOffset(0);

  if (devStatus == 0) {
    // 起動時の自動キャリブレーション (数秒間、センサーを完全に静止させてください)
    Serial.println("Calibrating sensor... Keep it flat and still.");
    mpu.CalibrateAccel(6);
    mpu.CalibrateGyro(6);
    
    // DMPを有効化
    mpu.setDMPEnabled(true);
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize();
    Serial.println("DMP Ready!");
  } else {
    Serial.print("DMP Initialization failed (code ");
    Serial.print(devStatus);
    Serial.println(")");
    while (1) { delay(10); }
  }

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

// 汎用クォータニオンからオイラー角への変換関数（元のコードのものを維持）
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

float currentW, currentX, currentY, currentZ;

void loop() {
  webSocket.loop();

  if (!dmpReady) return;

  // FIFOバッファから最新のパケットをノンブロッキングで取得
  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer))
  {
      // クォータニオンデータを取得 (q.w, q.x, q.y, q.z に正規化されたfloatが入る)
      mpu.dmpGetQuaternion(&q, fifoBuffer);

      float qw = q.w;
      float qx = q.x;
      float qy = q.y;
      float qz = q.z;

      currentW = qw;
      currentX = qy;
      currentY = qx;
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

      // 相対クォータニオンの計算（元のロジックをそのまま維持）
      float invW = refW;
      float invX = -refX;
      float invY = -refY;
      float invZ = -refZ;

      float outW = qw*invW - qx*invX - qy*invY - qz*invZ;
      float outX = qw*invX + qx*invW + qy*invZ - qz*invY;
      float outY = qw*invY - qx*invZ + qy*invW + qz*invX;
      float outZ = qw*invZ + qx*invY - qy*invX + qz*invW;

      char json[256];

      // MPU6050にはBNOのようなリアルタイム精度ステータス（accuracy）がないため、
      // JSONの互換性を壊さないようダミー値として固定値「3」（高精度相当）を入れています。
      int dummyAccuracy = 3; 

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
          dummyAccuracy
      );

      webSocket.sendTXT(json);
  }
}
#endif

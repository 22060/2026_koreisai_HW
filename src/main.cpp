#ifdef GYRO_TEST
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoOTA.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <NeoPixelBus.h>

const uint8_t LED_PIN = 20;
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(1, LED_PIN);

float hue = 0.0f;
const float hueStep = 360.0f / (100.0f * 10.0f); // 10秒で一周
const char* WIFI_SSID = "Xiaomi 14T Pro";
const char* WIFI_PASS = "g2k4bcc25ti2xbf";
const char* CONTROLLER_ID = "controller_001";

WebSocketsClient webSocket;
Adafruit_BNO055 bno055 = Adafruit_BNO055(55, 0x29, &Wire);

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length)
{
    switch (type) {
    case WStype_CONNECTED:
        Serial.println("WS Connected");
        break;
    case WStype_DISCONNECTED:
        Serial.println("WS Disconnected");
        break;
    }
}

void setup()
{
    strip.Begin();
    strip.Show();
    int hue = 60;
    RgbColor color = HslColor(hue / 360.0f, 1.0f, 0.05f);
    color.Darken(0.001f);  // 明るさを下げる
    strip.SetPixelColor(0, color);
    strip.Show();
    delay(1000);
    Serial.begin(115200);

    Wire.begin(5, 4);  // SDA=GPIO5, SCL=GPIO4
    Wire.setClock(400000);

    if (!bno055.begin()) {
        Serial.println("Failed to find BNO055");
        hue = 0;  // 緑色
         color = HslColor(hue / 360.0f, 1.0f, 0.05f);
        color.Darken(0.001f);  // 明るさを下げる
        strip.SetPixelColor(0, color);
        strip.Show();
        while (1) delay(100);
    }
    hue = 80;
     color = HslColor(hue / 360.0f, 1.0f, 0.05f);
    color.Darken(0.001f);  // 明るさを下げる
    strip.SetPixelColor(0, color);
    strip.Show();

    bno055.setExtCrystalUse(true);
    Serial.println("BNO055 Found!");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
    }
    hue = 100;
     color = HslColor(hue / 360.0f, 1.0f, 0.05f);
    color.Darken(0.001f);  // 明るさを下げる
    strip.SetPixelColor(0, color);
    strip.Show();

    Serial.println();
    Serial.println(WiFi.localIP());

    ArduinoOTA.setPassword("admin");
    ArduinoOTA.begin();

    webSocket.beginSSL("koreisai-game.ryutolab.com", 443, "/ws");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    hue = 120;
     color = HslColor(hue / 360.0f, 1.0f, 0.05f);
    color.Darken(0.001f);  // 明るさを下げる
    strip.SetPixelColor(0, color);
    strip.Show();
    delay(1000);
}

void loop()
{
    ArduinoOTA.handle();
    webSocket.loop();

    imu::Quaternion q = bno055.getQuat();

    char json[256];
    snprintf(
        json, sizeof(json),
        "{\"id\":\"%s\","
        "\"quat\":{\"w\":%.6f,\"x\":%.6f,"
        "\"y\":%.6f,\"z\":%.6f},"
        "\"accuracy\":0}",
        CONTROLLER_ID,
        q.w(), q.x(), q.z(), -q.y()
    );

    webSocket.sendTXT(json);

    hue += hueStep;
    if (hue >= 360.0f) hue -= 360.0f;

    RgbColor color = HslColor(hue / 360.0f, 1.0f, 0.05f);
    color.Darken(0.001f);  // 明るさを下げる
    strip.SetPixelColor(0, color);
    strip.Show();
    delay(10);
}
#endif
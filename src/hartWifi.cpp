#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "services/display_ssd1306.h"
#include "services/wserial.h"

const char *ssid = "InovaIndustria";
const char *password = "industria50";

static constexpr uint8_t HART_RX_PIN = 16;
static constexpr uint8_t HART_TX_PIN = 17;
static constexpr uint8_t PIN_SDA = 21;
static constexpr uint8_t PIN_SCL = 22;

HardwareSerial &hartSerial = Serial2;
static bool networkServicesStarted = false;

void receivedFunc(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) return;
  hartSerial.write(data, len);
}

void setup() {
  hartSerial.begin(1200, SERIAL_8O1, HART_RX_PIN, HART_TX_PIN);
  wserial.onBytesReceived(receivedFunc);
  wserial.begin(1200, 47268, SERIAL_8O1);
  WiFi.begin(ssid, password);

  if (disp.begin(PIN_SDA, PIN_SCL)) {
    disp.setText(1, "WiFi conectando");
    disp.setText(2, KIT_HOSTNAME);
    disp.setText(3, "HART UDP");
  }

}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !networkServicesStarted) {
    networkServicesStarted = true;
    MDNS.begin(KIT_HOSTNAME);
    ArduinoOTA
      .setHostname(KIT_HOSTNAME)
      .begin();
    disp.setText(1, (WiFi.localIP().toString() + " ID:" + String(KIT_ID)).c_str());
  }
  if (networkServicesStarted) ArduinoOTA.handle();
  wserial.update();
  disp.update();

  while (hartSerial.available()) {
    uint8_t buf[64];
    size_t len = hartSerial.readBytes(buf, min(hartSerial.available(), (int)sizeof(buf)));
    if (len > 0) wserial.write(buf, len);
  }
}

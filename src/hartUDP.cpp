#include <Arduino.h>
#include "services/display_ssd1306.h"
#include "services/lasecNet.h"
#include "services/wserial.h"

static constexpr uint8_t HART_RX_PIN = 16;
static constexpr uint8_t HART_TX_PIN = 17;
static constexpr uint8_t PIN_SDA = 21;
static constexpr uint8_t PIN_SCL = 22;

HardwareSerial &hartSerial = Serial2;
const char *hostName = KIT_HOSTNAME;

void receivedFunc(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) return;
  hartSerial.write(data, len);
}

void setup() {
  wserial.begin(1200, SERIAL_8O1);
  hartSerial.begin(1200, SERIAL_8O1, HART_RX_PIN, HART_TX_PIN);

  wserial.onBytesReceived(receivedFunc);

  net.onOtaStart([]() { wserial.println("[OTA] Start"); });
  net.onOtaEnd([]() { wserial.println("[OTA] End"); });
  net.onOtaProgress([](uint32_t p, uint32_t t) {
    wserial.println("[OTA] " + String((p * 100) / t));
  });
  net.onOtaError([](ota_error_t e) {
    wserial.println("[OTA] Error " + String(e));
  });

  if (!net.begin(hostName)) {
    wserial.println("[mDNS] begin failed");
  } else {
    wserial.println("[IP] is " + net.localIP().toString());
    wserial.println("[mDNS] begin in " + String(net.hostname()));
  }

  if (disp.begin(PIN_SDA, PIN_SCL)) {
    disp.setText(1, (WiFi.localIP().toString() + " ID:" + String(KIT_ID)).c_str());
    disp.setText(2, KIT_HOSTNAME);
    disp.setText(3, "HART UDP");
  } else {
    wserial.println("Display initialization failed.");
  }
}

void loop() {
  net.update();
  wserial.update();
  disp.update();

  while (hartSerial.available()) {
    uint8_t buf[64];
    size_t len = hartSerial.readBytes(buf, min(hartSerial.available(), (int)sizeof(buf)));
    if (len > 0) wserial.write(buf, len);
  }
}

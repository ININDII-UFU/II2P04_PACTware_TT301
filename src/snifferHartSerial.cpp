#include <Arduino.h>
#include <WiFi.h>

// --- CONFIGURE SEU WIFI ---
const char *ssid = "Inova";
const char *password = "josue32154538";
const IPAddress local_ip(200, 19, 148, 112);
const IPAddress gateway(200, 19, 148, 1);
const IPAddress subnet(255, 255, 255, 0);
// Porta para log TCP
#define LOG_PORT 5000

WiFiServer logServer(LOG_PORT);
WiFiClient logClient; // Apenas um cliente para log

HardwareSerial &hartSerial = Serial2;

void send_log(const char *prefix, const uint8_t *data, size_t len) {
  if (logClient && logClient.connected()) {
    logClient.write((const uint8_t*)prefix, strlen(prefix));
    logClient.write(data, len);
    logClient.write("\n", 1); // delimita
  }
}


void setup() {
  Serial.begin(1200, SERIAL_8O1);           // Serial USB (PACTware)
  hartSerial.begin(1200, SERIAL_8O1, 16, 17); // UART2 (Modem HART)

  // Setup WiFi
  WiFi.mode(WIFI_STA);
  WiFi.config(local_ip, gateway, subnet);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  logServer.begin();
}

void loop() {
  // Aceita conexão de log (uma vez)
  if (!logClient || !logClient.connected()) {
    logClient = logServer.available();
  }

  // PACTware → HART Modem
  while (Serial.available()) {
    uint8_t b = Serial.read();
    hartSerial.write(b);
    send_log("[TX] ", &b, 1);

  }

  // HART Modem → PACTware
  while (hartSerial.available()) {
    uint8_t b = hartSerial.read();
    Serial.write(b);
    send_log("[RX] ", &b, 1);
  }
}
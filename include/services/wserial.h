#pragma once

// Fluxo de bytes: USB Serial por padrao, UDP durante uma sessao CONNECT.
// A UART do modem HART pertence ao chamador.
#include <Arduino.h>
#include <AsyncUDP.h>
#include <WiFi.h>
#include <functional>
#include <string>
#include <cstring>

#define WSERIAL_NEWLINE "\r\n"
class WSerial {
public:
    using BytesCallback = std::function<void(const uint8_t *, size_t)>;
    using TextCallback = std::function<void(std::string)>;

    void begin(unsigned long baudrate = 115200, uint16_t udpPort = 47268,
               uint32_t config = SERIAL_8N1) {
        Serial.begin(baudrate, config);
        _listenPort = udpPort;
        if (udpPort && WiFi.status() == WL_CONNECTED) startListen();
    }

    void onBytesReceived(BytesCallback callback) { _onBytes = callback; }
    void onInputReceived(TextCallback callback) { _onText = callback; }

    void update() {
        const uint32_t now = millis();
        if (WiFi.status() != WL_CONNECTED) {
            portENTER_CRITICAL(&_lock);
            _linked = false;
            portEXIT_CRITICAL(&_lock);
            if (_listening) {
                _udp.close();
                _listening = false;
            }
        } else {
            portENTER_CRITICAL(&_lock);
            if (_linked && now - _lastConnect > 15000) _linked = false;
            portEXIT_CRITICAL(&_lock);
            if (_listenPort && !_listening && now - _lastRetry >= 2000) {
                _lastRetry = now;
                startListen();
            }
        }

        portENTER_CRITICAL(&_lock);
        const bool linked = _linked;
        portEXIT_CRITICAL(&_lock);
        if (linked) {
            // Descarte entrada USB durante a sessao UDP; nao a reproduza depois.
            while (Serial.available() > 0) Serial.read();
            return;
        }
        if (!_onBytes && !_onText) return;

        if (_onBytes) {
            uint8_t buffer[64];
            while (Serial.available() > 0) {
                const size_t count = Serial.readBytes(
                    buffer, min(Serial.available(), static_cast<int>(sizeof(buffer))));
                if (!count) break;
                _onBytes(buffer, count);
            }
        } else if (Serial.available() > 0) {
            String line = Serial.readStringUntil('\n');
            _onText(std::string(line.c_str(), line.length()));
        }
    }

    size_t write(const uint8_t *data, size_t len) {
        if (!data || !len) return 0;
        IPAddress ip;
        uint16_t port;
        portENTER_CRITICAL(&_lock);
        const bool linked = _linked;
        ip = _peerIP;
        port = _peerPort;
        portEXIT_CRITICAL(&_lock);
        if (linked && WiFi.status() == WL_CONNECTED)
            return _udp.writeTo(data, len, ip, port);
        return Serial.write(data, len);
    }

    size_t write(uint8_t data) { return write(&data, 1); }
    size_t write(const char *data, size_t len) {
        return write(reinterpret_cast<const uint8_t *>(data), len);
    }
    template <typename T> void print(const T &value) { send(String(value)); }
    void print(const std::string &value) { send(String(value.c_str())); }
    template <typename T> void println(const T &value) {
        send(String(value) + WSERIAL_NEWLINE);
    }
    void println(const std::string &value) {
        send(String(value.c_str()) + WSERIAL_NEWLINE);
    }
    void println() { send(WSERIAL_NEWLINE); }

    // === plot com timestamp explícito ===
    template <typename T>
    void plot(const char *varName, TickType_t x, T y, const char *unit = nullptr) {
        String str(">");
        str += varName; str += ":";
        uint32_t ts_ms = (uint32_t)x;
        if (ts_ms < 100000) ts_ms = millis();
        str += String(ts_ms); str += ":"; str += String(y);
        if (unit && unit[0]) { str += "\xC2\xA7"; str += unit; }
        str += WSERIAL_NEWLINE;
        send(str);
    }

    // === plot simples (timestamp automático) ===
    template <typename T>
    void plot(const char *varName, T y, const char *unit = nullptr) {
        plot(varName, (TickType_t)xTaskGetTickCount(), y, unit);
    }

    // === plot de array com dt fixo ===
    template <typename T>
    void plot(const char *varName, uint32_t dt_ms, const T* y, size_t ylen, const char *unit = nullptr) {
        String str(">");
        str += varName; str += ":";
        for (size_t i = 0; i < ylen; i++) {
            str += String((uint32_t)_base_ms); str += ":";
            str += String((double)y[i], 6);
            _base_ms += dt_ms;
            if (i < ylen - 1) str += ";";
        }
        if (unit) { str += "\xC2\xA7"; str += unit; }
        str += WSERIAL_NEWLINE;
        send(str);
    }

    void log(const char *text, uint32_t ts_ms = 0) {
        if (ts_ms == 0) ts_ms = millis();
        send(String(ts_ms) + ":" + String(text ? text : "") + WSERIAL_NEWLINE);
    }

private:
    void send(const String &text) {
        write(reinterpret_cast<const uint8_t *>(text.c_str()), text.length());
    }

    void startListen() {
        if (_udp.listen(_listenPort)) {
            _udp.onPacket([this](AsyncUDPPacket packet) { handlePacket(packet); });
            _listening = true;
        }
    }

    // O endereco anunciado deve corresponder ao remetente real do datagrama.
    static bool isControl(AsyncUDPPacket &packet, const char *prefix) {
        const size_t prefixLen = strlen(prefix);
        if (packet.length() <= prefixLen ||
            memcmp(packet.data(), prefix, prefixLen) != 0) return false;
        const String address = packet.remoteIP().toString() + ":" +
                               String(packet.remotePort());
        return packet.length() == prefixLen + address.length() &&
               memcmp(packet.data() + prefixLen, address.c_str(),
                      address.length()) == 0;
    }

    void handlePacket(AsyncUDPPacket packet) {
        const IPAddress sourceIP = packet.remoteIP();
        const uint16_t sourcePort = packet.remotePort();
        if (isControl(packet, "CONNECT:")) {
            bool accepted = false;
            portENTER_CRITICAL(&_lock);
            if (!_linked || (_peerIP == sourceIP && _peerPort == sourcePort)) {
                _peerIP = sourceIP;
                _peerPort = sourcePort;
                _lastConnect = millis();
                _linked = accepted = true;
            }
            portEXIT_CRITICAL(&_lock);
            if (accepted) {
                const String reply = String("CONNECT:") + WiFi.localIP().toString() +
                                     ":" + String(sourcePort) + "\n";
                packet.write(reinterpret_cast<const uint8_t *>(reply.c_str()),
                             reply.length());
            }
            return;
        }
        if (isControl(packet, "DISCONNECT:")) {
            bool accepted = false;
            portENTER_CRITICAL(&_lock);
            if (_linked && _peerIP == sourceIP && _peerPort == sourcePort) {
                _linked = false;
                accepted = true;
            }
            portEXIT_CRITICAL(&_lock);
            if (accepted) {
                const String reply = String("DISCONNECT:") + WiFi.localIP().toString() +
                                     ":" + String(sourcePort) + "\n";
                packet.write(reinterpret_cast<const uint8_t *>(reply.c_str()),
                             reply.length());
            }
            return;
        }

        portENTER_CRITICAL(&_lock);
        const bool accepted = _linked && _peerIP == sourceIP &&
                              _peerPort == sourcePort;
        portEXIT_CRITICAL(&_lock);
        if (!accepted) return;
        if (_onBytes) _onBytes(packet.data(), packet.length());
        else if (_onText)
            _onText(std::string(reinterpret_cast<const char *>(packet.data()),
                                packet.length()));
    }

    AsyncUDP _udp;
    portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
    IPAddress _peerIP;
    uint16_t _peerPort = 0;
    uint16_t _listenPort = 0;
    uint32_t _base_ms = 0;
    uint32_t _lastConnect = 0;
    uint32_t _lastRetry = 0;
    bool _linked = false;
    bool _listening = false;
    BytesCallback _onBytes;
    TextCallback _onText;
};

inline WSerial wserial;

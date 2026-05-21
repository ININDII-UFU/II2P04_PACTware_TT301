#pragma once
/**
 * @file wserial.h
 * @brief Classe de comunicação serial/UDP. Usa Serial quando não há link UDP ativo.
 * Uso: wserial.setup(); wserial.loop(); wserial.plot("var", valor);
 */
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncUDP.h>

#define WSERIAL_NEWLINE "\r\n"

class WSerial {
private:
    IPAddress   _udpTargetIP;
    uint16_t    _udpTargetPort  = 0;
    uint16_t    _listenPort     = 0;
    bool        _udpAvailable   = false;
    bool        _udpLinked      = false;
    uint32_t    _base_ms        = 0;
    AsyncUDP    _udp;
    std::function<void(std::string)> _onInput;
    std::function<void(const uint8_t*, size_t)> _onBytesInput;

    void _send(const String &txt) {
        write(reinterpret_cast<const uint8_t*>(txt.c_str()), txt.length());
    }

    size_t _sendBytes(const uint8_t *data, size_t len) {
        if (_udpLinked) {
            _udp.writeTo(
                data, len, _udpTargetIP, _udpTargetPort);
            return len;
        }
        return Serial.write(data, len);
    }

    bool _parseHostPort(const String &s, String &cmd, String &host, uint16_t &port) {
        int c1 = s.indexOf(':');
        int c2 = s.lastIndexOf(':');
        if (c1 <= 0 || c2 <= c1) return false;
        cmd  = s.substring(0, c1);
        host = s.substring(c1 + 1, c2);
        long v = s.substring(c2 + 1).toInt();
        if (v <= 0 || v > 65535) return false;
        port = (uint16_t)v;
        return true;
    }

    void _handlePacket(AsyncUDPPacket packet) {
        const uint8_t *data = packet.data();
        const size_t len = packet.length();
        String s((const char*)data, len);
        String cmdLine = s;
        cmdLine.trim();
        if (!cmdLine.startsWith("CONNECT:") && !cmdLine.startsWith("DISCONNECT:")) {
            if (_onBytesInput) _onBytesInput(data, len);
            else if (_onInput) _onInput(std::string((const char*)data, len));
            return;
        }
        String cmd, host;
        uint16_t port;
        if (!_parseHostPort(cmdLine, cmd, host, port)) {
            if (_onBytesInput) _onBytesInput(data, len);
            else if (_onInput) _onInput(std::string((const char*)data, len));
            return;
        }
        IPAddress ip;
        if (!ip.fromString(host)) {
            if (WiFi.hostByName(host.c_str(), ip) != 1) return;
        }
        if (ip == IPAddress()) return;
        _udpTargetIP   = ip;
        _udpTargetPort = port;
        if (cmd == "CONNECT") {
            _udpLinked = true;
            _send("CONNECT:" + WiFi.localIP().toString() + ":" + String(_udpTargetPort) + "\n");
        } else if (cmd == "DISCONNECT" && _udpLinked) {
            _send("DISCONNECT:" + WiFi.localIP().toString() + ":" + String(_udpTargetPort) + "\n");
            _udpLinked = false;
        }
    }

    void _startListen() {
        if (_udp.listen(_listenPort)) {
            _udpAvailable = true;
            _udp.onPacket([this](AsyncUDPPacket pkt){ _handlePacket(pkt); });
        }
    }

public:
    /**
     * @brief Inicializa a Serial e tenta abrir o socket UDP (requer WiFi já iniciado).
     * @param baudrate  Velocidade da Serial (padrão 115200).
     * @param port      Porta UDP de escuta (padrão 47268). Passa 0 para desabilitar UDP.
     */
    void begin(unsigned long baudrate = 115200, uint32_t config = SERIAL_8N1, uint16_t port = 47268) {
        Serial.begin(baudrate, config);
        _listenPort = port;
        if (_listenPort != 0 && WiFi.status() == WL_CONNECTED) {
            _startListen();
        }
    }

    /**
     * @brief Deve ser chamado no loop(). Processa Serial e retenta UDP se necessário.
     */
    void update() {
        // Retenta UDP quando WiFi conectar
        if (_listenPort != 0 && !_udpAvailable && WiFi.status() == WL_CONNECTED) {
            static uint32_t lastRetry = 0;
            if (millis() - lastRetry > 2000) {
                lastRetry = millis();
                _startListen();
            }
        }
        if (Serial.available()) {
            if (_onBytesInput) {
                uint8_t buf[64];
                size_t n = Serial.readBytes(buf, min(Serial.available(), (int)sizeof(buf)));
                if (n > 0) _onBytesInput(buf, n);
            } else {
                String linha = Serial.readStringUntil('\n');
                if (_onInput) _onInput(linha.c_str());
            }
        }
    }

    /** @brief Registra callback chamado ao receber dados. */
    void onInputReceived(std::function<void(std::string)> cb) { _onInput = cb; }
    void onBytesReceived(std::function<void(const uint8_t*, size_t)> cb) { _onBytesInput = cb; }

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
        _send(str);
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
        _send(str);
    }

    void log(const char *text, uint32_t ts_ms = 0) {
        if (ts_ms == 0) ts_ms = millis();
        _send(String(ts_ms) + ":" + String(text ? text : "") + WSERIAL_NEWLINE);
    }

    template <typename T>
    void println(const T &data) { _send(String(data) + WSERIAL_NEWLINE); }
    void println() { _send(WSERIAL_NEWLINE); }

    template <typename T>
    void print(const T &data) { _send(String(data)); }

    size_t write(uint8_t data) { return _sendBytes(&data, 1); }
    size_t write(const uint8_t *data, size_t len) { return _sendBytes(data, len); }
    size_t write(const char *data, size_t len) {
        return write(reinterpret_cast<const uint8_t*>(data), len);
    }
};

inline WSerial wserial; ///< Instância global de comunicação serial/UDP.

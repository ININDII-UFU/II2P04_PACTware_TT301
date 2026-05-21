#pragma once
/**
 * @file lasecNet.h
 * @brief Gerenciamento de rede: WiFi (WiFiManager), mDNS e OTA.
 *
 * Uso:
 *   net.begin("iikit3");          // conecta/portal, registra mDNS e inicia OTA
 *   net.begin("iikit3", "MeuAP"); // nome do AP do portal customizado
 *
 *   void loop() { net.update(); } // mantém OTA e mDNS ativos
 *
 *   if (net.isConnected()) { ... }
 *   net.localIP();                // IPAddress do ESP
 *   net.hostname();               // hostname registrado
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>

class LasecNet {
public:
    using OtaProgressCb = std::function<void(uint32_t done, uint32_t total)>;
    using OtaEventCb    = std::function<void()>;
    using OtaErrorCb    = std::function<void(ota_error_t)>;

    /**
     * @brief Inicializa WiFi (via WiFiManager), mDNS e OTA.
     *
     * Bloqueia até a conexão ser estabelecida ou o portal ser usado.
     * Se @p apName for omitido usa "ConfigAP".
     *
     * @param hostname  Nome registrado no mDNS e no OTA (ex.: "iikit3").
     * @param apName    SSID do portal de configuração (padrão: "ConfigAP").
     * @return true se WiFi e mDNS iniciaram com sucesso.
     */
    bool begin(const char *hostname, const char *apName = "ConfigAP") {
        strncpy(_hostname, hostname, sizeof(_hostname) - 1);
        _hostname[sizeof(_hostname) - 1] = '\0';

        // ── WiFi via WiFiManager ──────────────────────────────────────────
        _wm.setHostname(_hostname);
        _wm.autoConnect(apName);   // bloqueia até conectar ou salvar via portal

        // ── mDNS ─────────────────────────────────────────────────────────
        _mdnsOk = MDNS.begin(_hostname);

        // ── OTA ──────────────────────────────────────────────────────────
        ArduinoOTA.setHostname(_hostname);

        ArduinoOTA.onStart([this]() {
            if (_onStart) _onStart();
        });
        ArduinoOTA.onEnd([this]() {
            if (_onEnd) _onEnd();
        });
        ArduinoOTA.onProgress([this](uint32_t done, uint32_t total) {
            if (_onProgress) _onProgress(done, total);
        });
        ArduinoOTA.onError([this](ota_error_t e) {
            if (_onError) _onError(e);
        });

        ArduinoOTA.begin();

        return isConnected() && _mdnsOk;
    }

    /**
     * @brief Deve ser chamado em loop(). Processa OTA e reconecta mDNS se necessário.
     */
    void update() {
        ArduinoOTA.handle();

        // Retenta mDNS se WiFi reconectou após queda
        if (!_mdnsOk && isConnected()) {
            _mdnsOk = MDNS.begin(_hostname);
        }
    }

    /** @brief Registra callback chamado no início de uma atualização OTA. */
    void onOtaStart(OtaEventCb cb)        { _onStart    = cb; }

    /** @brief Registra callback chamado ao fim de uma atualização OTA. */
    void onOtaEnd(OtaEventCb cb)          { _onEnd      = cb; }

    /** @brief Registra callback de progresso OTA (bytes feitos / total). */
    void onOtaProgress(OtaProgressCb cb)  { _onProgress = cb; }

    /** @brief Registra callback de erro OTA. */
    void onOtaError(OtaErrorCb cb)        { _onError    = cb; }

    /** @brief Retorna true se o WiFi está conectado. */
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

    /** @brief Retorna o IP local do ESP. */
    IPAddress localIP() const { return WiFi.localIP(); }

    /** @brief Retorna o hostname registrado. */
    const char *hostname() const { return _hostname; }

    /** @brief Abre o portal de reconfiguração WiFi manualmente. */
    void resetSettings() { _wm.resetSettings(); }

private:
    char            _hostname[32] = "esp32";
    WiFiManager     _wm;
    bool            _mdnsOk       = false;

    OtaEventCb      _onStart      = nullptr;
    OtaEventCb      _onEnd        = nullptr;
    OtaProgressCb   _onProgress   = nullptr;
    OtaErrorCb      _onError      = nullptr;
};

inline LasecNet net; ///< Instância global de gerenciamento de rede.

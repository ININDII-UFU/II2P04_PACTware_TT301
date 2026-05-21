/**
 * @file iikit.h
 * @brief Orquestrador do kit industrial LASEC (ESP32).
 *
 * Delega todas as responsabilidades às libs do projeto:
 *   - Rede / OTA / mDNS  →  services/lasecNet.h   (net)
 *   - Serial / UDP        →  services/wserial.h    (wserial)
 *   - Display OLED        →  services/display_ssd1306.h (disp)
 *   - ADC ADS1115         →  services/ads1115.h    (ads1115)
 *   - GPIOs / debounce    →  util/lasecGPIOKit.h   (IIKit.gpio)
 */

#pragma once

#include <Arduino.h>
#include <EEPROM.h>

#include "services/wserial.h"
#include "services/display_ssd1306.h"
#include "services/ads1115.h"
#include "services/lasecNet.h"
#include "util/lasecGPIOKit.h"

// Definições de pinos: def_pin_* vêm de lasecGPIOKit.h

/**
 * @class IIKit_c
 * @brief Orquestrador do kit industrial — inicializa e atualiza todas as libs.
 */
class IIKit_c
{
private:
    char _hostname[15] = "iikit"; ///< Hostname mDNS/OTA (ex.: "iikit3").

    void errorMsg(const String &error, bool restart = true)
    {
        wserial.println(error);
        if (restart) {
            wserial.println("Rebooting now...");
            delay(2000);
            ESP.restart();
        }
    }

public:
    lasecGPIOKit gpio; ///< GPIOs do kit: gpio.rtn_1, gpio.rtn_2, gpio.push_1, gpio.push_2

    /** @brief Inicializa todas as libs na ordem correta. */
    void begin()
    {
        // ── Serial / UDP ──────────────────────────────────────────────────
        wserial.begin();

        // ── Display ───────────────────────────────────────────────────────
        if (!disp.begin(def_pin_SDA, def_pin_SCL)) {
            errorMsg("Display initialization failed.", true);
        }
        disp.setText(1, "Inicializando...");
        disp.setText(2, "WIFI not connected");
        disp.setText(3, "Starting Access Point Mode");

        // ── Rede (WiFi + mDNS + OTA) ─────────────────────────────────────
        net.begin(KIT_HOSTNAME);

        // ── Display: atualiza com IP e hostname ───────────────────────────
        disp.setText(1, (WiFi.localIP().toString() + " ID:" + String(KIT_ID)).c_str());
        disp.setText(2, KIT_HOSTNAME);
        disp.setText(3, "");

        // ── GPIOs + debounce ──────────────────────────────────────────────
        gpio.begin();

        // ── ADC ───────────────────────────────────────────────────────────
        if (!ads1115.begin()) errorMsg("ADS error.", true);
    }

    /** @brief Deve ser chamado em loop(). Atualiza todas as libs. */
    void update()
    {
        net.update();
        wserial.update();
        disp.update();
        gpio.tick(); // debounce de rtn_1, rtn_2, push_1, push_2
    }

    // ── Leituras analógicas (delegadas ao ads1115 global) ─────────────────
    uint16_t analogReadPot1()   { return ads1115.analogReadPot1();   }
    uint16_t analogReadPot2()   { return ads1115.analogReadPot2();   }
    uint16_t analogRead4a20_1() { return ads1115.analogRead4a20_1(); }
    uint16_t analogRead4a20_2() { return ads1115.analogRead4a20_2(); }
};

IIKit_c IIKit; ///< Instância global do kit industrial.
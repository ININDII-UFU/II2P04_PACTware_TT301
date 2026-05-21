#pragma once
#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#endif

// Garante que ARDUINO_ISR_ATTR exista em plataformas que não o definem (ex.: AVR)
#ifndef ARDUINO_ISR_ATTR
#define ARDUINO_ISR_ATTR
#endif

/**
 * @file lasecDebounce.h
 * @brief Debounce por integração para entrada digital.
 *
 * Método: integrador satura/zera — imune a glitches de qualquer duração.
 * A transição só é confirmada quando o integrador atinge o limite superior
 * (pressão) ou zero (soltura), exigindo `windowMs` amostras consecutivas
 * coerentes.
 *
 * Chamar tick() a cada ~1 ms (via loop() ou jtask periódica).
 *
 * Uso:
 *   lasecDebounce btn;
 *   btn.begin(PIN, INPUT_PULLUP, LOW, 20);   // pino, modo, nível ativo, janela ms
 *   btn.onPress([]{ Serial.println("press"); });
 *   btn.onRelease([]{ Serial.println("release"); });
 *
 *   // em loop() ou task periódica (período ≈ 1 ms):
 *   btn.tick();
 *   if (btn.pressed())  { /* borda de ativação (pulso de 1 tick) *\/ }
 *   if (btn.released()) { /* borda de desativação (pulso de 1 tick) *\/ }
 *   if (btn.held())     { /* nível ativo estável *\/ }
 */
class lasecDebounce {
public:
    using Callback = void (*)();

    lasecDebounce() = default;

    /**
     * @brief Configura o pino e os parâmetros de debounce.
     * @param pin        Número do pino GPIO.
     * @param mode       Modo do pino (INPUT, INPUT_PULLUP, INPUT_PULLDOWN).
     * @param activeLevel  Nível lógico considerado "ativo" (LOW ou HIGH).
     * @param windowMs   Número de amostras consecutivas para confirmar transição
     *                   (= tempo em ms se tick() for chamado a cada 1 ms).
     *                   Mínimo: 2.
     */
    void begin(uint8_t pin,
               uint8_t mode        = INPUT_PULLUP,
               uint8_t activeLevel = LOW,
               uint8_t windowMs    = 20)
    {
        _pin         = pin;
        _activeLevel = activeLevel;
        _window      = (windowMs < 2) ? 2 : windowMs;
        _integrator  = 0;
        _stableState = false;
        _pressedFlag = false;
        _releasedFlag= false;
        _onPress     = nullptr;
        _onRelease   = nullptr;
        pinMode(_pin, mode);
    }

    /**
     * @brief Registra callback chamado na borda de ativação (após debounce).
     * @note  Chamado a partir do contexto de tick() — não é ISR.
     */
    void onPress(Callback cb)   { _onPress   = cb; }

    /**
     * @brief Registra callback chamado na borda de desativação (após debounce).
     * @note  Chamado a partir do contexto de tick() — não é ISR.
     */
    void onRelease(Callback cb) { _onRelease = cb; }

    /**
     * @brief Processa uma amostra. Chamar a cada ~1 ms.
     *        Pode ser chamado de loop() ou de uma jtask periódica.
     */
    void tick()
    {
        _pressedFlag  = false;
        _releasedFlag = false;

        // Integrador: carrega enquanto ativo, descarrega enquanto inativo
        if (digitalRead(_pin) == _activeLevel) {
            if (_integrator < _window) ++_integrator;
        } else {
            if (_integrator > 0)       --_integrator;
        }

        // Detecção de borda de ativação
        if (!_stableState && (_integrator >= _window)) {
            _stableState = true;
            _pressedFlag = true;
            if (_onPress) _onPress();
        }
        // Detecção de borda de desativação
        else if (_stableState && (_integrator == 0)) {
            _stableState  = false;
            _releasedFlag = true;
            if (_onRelease) _onRelease();
        }
    }

    /** @brief true apenas no tick em que a borda de ativação foi detectada. */
    bool pressed()  const { return _pressedFlag; }

    /** @brief true apenas no tick em que a borda de desativação foi detectada. */
    bool released() const { return _releasedFlag; }

    /** @brief true enquanto o nível ativo está estável (após debounce). */
    bool held()     const { return _stableState; }

private:
    uint8_t  _pin          = 0;
    uint8_t  _activeLevel  = LOW;
    uint8_t  _window       = 20;
    uint8_t  _integrator   = 0;
    bool     _stableState  = false;
    bool     _pressedFlag  = false;
    bool     _releasedFlag = false;
    Callback _onPress      = nullptr;
    Callback _onRelease    = nullptr;
};

#pragma once

// ─── Compatibilidade: somente ESP32 original ─────────────────────────────────
// I2S_MODE_ADC_BUILT_IN, i2s_set_adc_mode() e a API legada driver/adc.h
// existem APENAS no ESP32 original (target esp32).
// ESP32-S3, S2 e C3 usam esp_adc/adc_continuous.h (IDF v5) para DMA de ADC.
#if !defined(ARDUINO_ARCH_ESP32)
#  error "AdcDmaEsp.h: somente suportado em ARDUINO_ARCH_ESP32."
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || \
      defined(CONFIG_IDF_TARGET_ESP32S2) || \
      defined(CONFIG_IDF_TARGET_ESP32C3) || \
      defined(CONFIG_IDF_TARGET_ESP32C6) || \
      defined(CONFIG_IDF_TARGET_ESP32H2)
#  error "AdcDmaEsp.h: I2S_MODE_ADC_BUILT_IN não existe neste target. " \
         "Use esp_adc/adc_continuous.h para DMA no ESP32-S3/S2/C3."
#endif
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include "driver/i2s.h"
#include "driver/adc.h"

/**
 * AdcDmaEsp (DMA contínuo + buffer circular + decimação opcional)
 *
 * - DMA contínuo via I2S + ADC interno
 * - Task interna lê blocos do DMA
 * - Se decimation == 1  → grava TODAS as amostras no BIGBUF
 * - Se decimation > 1   → grava a MÉDIA de cada N amostras no BIGBUF
 *
 * API:
 *   AdcDmaEsp adc;
 *   adc.beginGPIO(36, 20000);          // sem média (decimation = 1)
 *   adc.beginGPIO(36, 20000, 10);      // média de cada 10 amostras
 *
 *   size_t n = adc.read(buf, maxN);    // lê do buffer circular
 */

class AdcDmaEsp {
public:
    static constexpr size_t BIGBUF_LEN = 8192;   // tamanho do buffer circular
    static constexpr size_t DMA_BLK    = 512;    // tamanho do bloco lido do DMA

    AdcDmaEsp()
        : _port(I2S_NUM_0),
          _adc_channel(ADC1_CHANNEL_0),
          _sample_rate(1000),
          _started(false),
          _taskHandle(nullptr),
          _write_pos(0),
          _read_pos(0),
          _decimation(1),
          _accum(0),
          _accumCount(0)
    {}

    // ============================================================
    // beginGPIO — 3º parâmetro = decimation (1 = sem média)
    // ============================================================
    bool beginGPIO(int gpio,
                   int sample_rate_hz,
                   uint16_t decimation = 1)
    {
        adc1_channel_t ch;

        switch (gpio) {
            case 36: ch = ADC1_CHANNEL_0; break;
            case 39: ch = ADC1_CHANNEL_3; break;
            case 34: ch = ADC1_CHANNEL_6; break;
            case 35: ch = ADC1_CHANNEL_7; break;
            case 32: ch = ADC1_CHANNEL_4; break;
            case 33: ch = ADC1_CHANNEL_5; break;
            case 37: ch = ADC1_CHANNEL_1; break;
            case 38: ch = ADC1_CHANNEL_2; break;
            default:
                return false; // GPIO inválido
        }

        return begin(ch, sample_rate_hz, decimation);
    }

    // ============================================================
    // begin — mesma ideia, mas recebendo o canal ADC direto
    // ============================================================
    bool begin(adc1_channel_t channel,
               int sample_rate_hz,
               uint16_t decimation = 1,
               i2s_port_t port = I2S_NUM_0)
    {
        _adc_channel = channel;
        _sample_rate = sample_rate_hz;
        _port        = port;

        if (decimation == 0) decimation = 1;
        _decimation  = decimation;
        _accum       = 0;
        _accumCount  = 0;

        // -------- ADC --------
        adc1_config_width(ADC_WIDTH_BIT_12);
        if (adc1_config_channel_atten(_adc_channel, ADC_ATTEN_DB_11) != ESP_OK)
            return false;

        // -------- I2S / DMA --------
        i2s_config_t cfg = {};
        cfg.mode = (i2s_mode_t)(
            I2S_MODE_MASTER |
            I2S_MODE_RX |
            I2S_MODE_ADC_BUILT_IN
        );
        cfg.sample_rate = _sample_rate;
        cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        cfg.dma_buf_count = 8;
        cfg.dma_buf_len   = DMA_BLK;
        cfg.use_apll      = false;

        if (i2s_driver_install(_port, &cfg, 0, NULL) != ESP_OK)
            return false;

        if (i2s_set_adc_mode(ADC_UNIT_1, _adc_channel) != ESP_OK)
            return false;

        if (i2s_adc_enable(_port) != ESP_OK)
            return false;

        _write_pos = 0;
        _read_pos  = 0;
        _started   = true;

        // Task DMA em core 1, prioridade alta
        xTaskCreatePinnedToCore(
            _dmaTaskTrampoline,
            "dma_adc_task",
            4096,
            this,
            26,
            &_taskHandle,
            1
        );

        return true;
    }

    // ============================================================
    // read — lê do buffer circular (já com média aplicada se houver)
    // ============================================================
    size_t read(uint16_t* dest, size_t maxSamples)
    {
        if (!_started) return 0;

        uint32_t available = _write_pos - _read_pos;
        if (available == 0) return 0;

        if (available > BIGBUF_LEN) {
            _read_pos = _write_pos - BIGBUF_LEN;
            available = BIGBUF_LEN;
        }

        size_t toRead = (available < maxSamples) ? available : maxSamples;

        for (size_t i = 0; i < toRead; ++i) {
            uint32_t idx = (_read_pos + i) % BIGBUF_LEN;
            dest[i] = _bigbuf[idx];
        }

        _read_pos += toRead;
        return toRead;
    }

    size_t available() const {
        return _write_pos - _read_pos;
    }

    // ============================================================
    // end
    // ============================================================
    void end()
    {
        if (!_started) return;
        _started = false;

        if (_taskHandle) {
            vTaskDelete(_taskHandle);
            _taskHandle = nullptr;
        }

        i2s_adc_disable(_port);
        i2s_driver_uninstall(_port);
    }

private:
    // ============================================================
    // Task DMA
    // ============================================================
    static void _dmaTaskTrampoline(void* arg)
    {
        ((AdcDmaEsp*)arg)->_dmaTask();
    }

    void _dmaTask()
    {
        uint16_t tmp[DMA_BLK];

        while (_started) {

            size_t bytes_read = 0;
            esp_err_t err = i2s_read(
                _port,
                tmp,
                sizeof(tmp),
                &bytes_read,
                portMAX_DELAY
            );
            if (err != ESP_OK || bytes_read == 0)
                continue;

            size_t samples = bytes_read / sizeof(uint16_t);

            // Caminho rápido: sem média (decimation == 1)
            if (_decimation <= 1) {
                for (size_t i = 0; i < samples; i++)
                    tmp[i] &= 0x0FFF; // 12 bits válidos

                _pushToBigBuffer(tmp, samples);
            }
            else {
                // Caminho com média/decimação
                for (size_t i = 0; i < samples; i++) {
                    uint16_t v = tmp[i] & 0x0FFF;

                    _accum      += v;
                    _accumCount += 1;

                    if (_accumCount >= _decimation) {
                        uint16_t avg = _accum / _decimation;
                        _pushToBigBuffer(&avg, 1);

                        _accum      = 0;
                        _accumCount = 0;
                    }
                }
            }
        }

        vTaskDelete(NULL);
    }

    // ============================================================
    // Buffer circular
    // ============================================================
    void _pushToBigBuffer(const uint16_t* data, size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            uint32_t idx = _write_pos % BIGBUF_LEN;
            _bigbuf[idx] = data[i];
            _write_pos++;
        }

        uint32_t diff = _write_pos - _read_pos;
        if (diff > BIGBUF_LEN)
            _read_pos = _write_pos - BIGBUF_LEN;
    }

private:
    // Configuração
    i2s_port_t      _port;
    adc1_channel_t  _adc_channel;
    int             _sample_rate;
    bool            _started;

    // Task
    TaskHandle_t    _taskHandle;

    // Decimação / média
    uint16_t        _decimation;   // 1 = sem média; >1 = média de N
    uint32_t        _accum;
    uint16_t        _accumCount;

    // Buffer circular
    uint16_t        _bigbuf[BIGBUF_LEN];
    volatile uint32_t _write_pos;
    volatile uint32_t _read_pos;
};
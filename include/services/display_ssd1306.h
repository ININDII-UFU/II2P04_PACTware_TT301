#ifndef __DISPLAY_H
#define __DISPLAY_H

/**
 * @file Display_SSD1306.h
 * @brief Classe para manipulação de displays OLED utilizando a biblioteca Adafruit_SSD1306.
 *
 * Esta classe encapsula o funcionamento de displays OLED, fornecendo funcionalidades
 * para inicialização, atualização e configuração de texto com suporte a rolagem e modos de função.
 */

#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_ADDRESS 0x3C ///< Endereço I2C do display OLED.
#define SCREEN_WIDTH 128    ///< Largura do display em pixels.
#define SCREEN_HEIGHT 64    ///< Altura do display em pixels.
#define OLED_RESET -1       ///< Pino de reset (ou -1 para compartilhar com o reset do Arduino).

inline Adafruit_SSD1306 SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/**
 * @class Display_SSD1306
 * @brief Classe para gerenciamento de displays OLED.
 */
class Display_SSD1306 {
protected:
    /**
     * @brief Realiza a rolagem de texto na linha especificada.
     * @param index Índice da linha a ser rolada.
     */
    void rotaty(uint8_t index);
    bool isFuncMode = false; ///< Indica se o display está no modo de função.
    bool isChanged = true; ///< Indica se houve alteração no conteúdo do display.
    bool scrollLeft[3] = {false, false, false}; ///< Flags de rolagem para cada linha.
    char ca_lineTxt[3][20] = {"Inicializando...", "", ""}; ///< Conteúdo das linhas do display.
    uint8_t ui8_lineSize[3] = {16, 0, 0}; ///< Tamanho do texto de cada linha.
    uint8_t ui8_txtSize[3] = {2, 2, 2}; ///< Tamanho da fonte para cada linha.
    int16_t i16_lineWidth[3] = {12, 12, 12}; ///< Largura inicial do texto em cada linha.
    int16_t i16_lineMinWidth[3]; ///< Largura mínima para rolagem do texto.

public:
    /**
     * @brief Configura o texto a ser exibido em uma linha do display.
     * @param line Índice da linha (1 a 3).
     * @param txt Texto a ser exibido.
     * @param funcMode Modo de função (opcional).
     * @param txtSize Tamanho da fonte (opcional).
     */
    void setText(uint8_t line, const char txt[], bool funcMode = false, uint8_t txtSize = 2);

    /**
     * @brief Define o modo de função do display.
     * @param funcMode Modo de função (true ou false).
     */
    void setFuncMode(bool funcMode);

    /**
     * @brief Inicializa o display OLED.
     * @param SDA Pino SDA para comunicação I2C.
     * @param SCL Pino SCL para comunicação I2C.
     * @return true se a inicialização foi bem-sucedida, false caso contrário.
     */
    bool begin(const uint8_t &SDA = 0, const uint8_t &SCL = 0);

    /**
     * @brief Atualiza o conteúdo do display OLED.
     */
    void update(void);
};

bool Display_SSD1306::begin(const uint8_t &SDA, const uint8_t &SCL) {
    if (SDA != 0 && SCL != 0) {
        Wire.begin(SDA, SCL);
    } else {
        Wire.begin();
    }
    // periphBegin=false: evita que SSD1306.begin() reinicialize o Wire e sobrescreva os pinos
    if (!SSD1306.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, false, false)) {
        return false;
    }
    setText(1, ca_lineTxt[0]);
    setText(2, ca_lineTxt[1]);
    setText(3, ca_lineTxt[2]);
    return true;
}

void Display_SSD1306::update(void) {
    if (ui8_lineSize[0] > 10 || ui8_lineSize[1] > 10 || ui8_lineSize[2] > 10 || isChanged) {
        isChanged = false;
        SSD1306.clearDisplay();
        SSD1306.setTextWrap(false);
        SSD1306.setTextColor(SSD1306_WHITE);
        SSD1306.cp437(true);
        rotaty(0);
        rotaty(1);
        rotaty(2);
        SSD1306.display();
    }
}

void Display_SSD1306::rotaty(uint8_t index) {
    if (ui8_lineSize[index] > 10) {
        SSD1306.setTextSize(ui8_txtSize[index]);
        SSD1306.setCursor(i16_lineWidth[index], index * 20);
        SSD1306.print(ca_lineTxt[index]);
        if (scrollLeft[index]) {
            ++i16_lineWidth[index];
        } else {
            --i16_lineWidth[index];
        }
        if (i16_lineWidth[index] < i16_lineMinWidth[index]) {
            scrollLeft[index] = true;
        }
        if (i16_lineWidth[index] > 12) {
            scrollLeft[index] = false;
        }
    } else {
        SSD1306.setTextSize(ui8_txtSize[index]);
        SSD1306.setCursor(0, index * 20);
        SSD1306.println(ca_lineTxt[index]);
    }
}

void Display_SSD1306::setText(uint8_t line, const char txt[], bool funcMode, uint8_t txtSize) {
    if (this->isFuncMode == funcMode) {
        strcpy(ca_lineTxt[line - 1], txt);
        ui8_lineSize[line - 1] = strlen(ca_lineTxt[line - 1]);
        i16_lineMinWidth[line - 1] = -12 * (ui8_lineSize[line - 1] - 9);
        ui8_txtSize[line - 1] = txtSize;
        isChanged = true;
    }
    update();
}

void Display_SSD1306::setFuncMode(bool funcMode) {
    this->isFuncMode = funcMode;
}

inline Display_SSD1306 disp;    ///< Display OLED.
#endif 

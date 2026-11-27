#pragma once
// Pinagem e constantes globais.
// Spec: docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md

#include <Arduino.h>

#define FW_VERSION "0.1.0"

// ---- SPI compartilhado (TFT, SD, CC1101) ----
#define PIN_SPI_SCK   12
#define PIN_SPI_MOSI  11
#define PIN_SPI_MISO  13

// ---- TFT ST7789 240x320 (RST ligado ao EN) ----
#define PIN_TFT_CS    10
#define PIN_TFT_DC    9
#define PIN_TFT_BL    14

// ---- SD e CC1101 ----
#define PIN_SD_CS     21
#define PIN_CC1101_CS 47
#ifndef ENABLE_SD
#define ENABLE_SD 1          // 0 = SD desligado (isolar problemas no hardware)
#endif
#define SD_SPI_HZ     20000000
#define TRACK_CAPACITY 20000  // pontos da trilha em PSRAM (~160 KB)

// ---- I2C (OLED 0x3C, ICM-20948 0x68/0x69) ----
#define PIN_I2C_SDA            8
#define PIN_I2C_SCL            18
#define PIN_IMU_INT            7
#define PIN_MPU_INT            PIN_IMU_INT  // alias legado
#define I2C_ADDR_OLED          0x3C
#define I2C_ADDR_ICM_PRIMARY   0x68
#define I2C_ADDR_ICM_ALT       0x69
#define MAG_DECLINATION_DEFAULT (-21.5f)    // São Paulo (graus, negativo = Oeste)

// ---- GPS NEO-6M (Serial1, lado ESP) ----
#define PIN_GPS_RX    44
#define PIN_GPS_TX    43
#define GPS_BAUD      9600

// ---- LoRa DX-LR22 (Serial2, lado ESP) ----
#define PIN_LORA_RX   40
#define PIN_LORA_TX   39
#define PIN_LORA_M0   41   // sempre INPUT_PULLDOWN: com AT+SWITCH0 é saída do módulo
#define PIN_LORA_M1   42   // idem
#define PIN_LORA_AUX  2    // alto = ocupado
#define LORA_BAUD     9600
#define LORA_CHANNEL_HEX "47"  // 850.15 + 0x47 = 921.15 MHz (fixo de fábrica)

// ---- I2S MAX98357A ----
#define PIN_I2S_BCLK  15
#define PIN_I2S_LRC   16
#define PIN_I2S_DIN   17

// ---- Encoder ----
#define PIN_ENC_A     4
#define PIN_ENC_B     5
#define PIN_ENC_SW    6

// ---- Bateria (ADC1, divisor 100k/100k) ----
#define PIN_BAT_ADC   1

// ---- Log pela USB CDC ----
#ifndef DEBUG_LOG
#define DEBUG_LOG 1
#endif

#if DEBUG_LOG
#define LOG_(lvl, fmt, ...) Serial.printf("[" lvl "] %lu " fmt "\n", (unsigned long)millis(), ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG_("D", fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_("I", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_("W", fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_("E", fmt, ##__VA_ARGS__)
#else
#define LOGD(...) do {} while (0)
#define LOGI(...) do {} while (0)
#define LOGW(...) do {} while (0)
#define LOGE(...) do {} while (0)
#endif

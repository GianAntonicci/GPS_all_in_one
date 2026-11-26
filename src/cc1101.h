#pragma once
// Driver mínimo somente-RX do CC1101 no barramento FSPI compartilhado (CS = PIN_CC1101_CS).
// Nunca chama SPI.begin()/SPI.end() e não implementa nenhum comando de TX.
#include <cstdint>

bool cc1101Begin();                              // reset, probe VERSION, config RX e sleep
bool cc1101Ok();                                 // true se o CC1101 respondeu no boot
bool cc1101MeasureRssi(float mhz, int16_t& dbm); // sintonia RX + leitura de RSSI em dBm
void cc1101Sleep();                              // SIDLE + SPWD

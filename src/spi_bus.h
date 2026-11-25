#pragma once
// Mutex do barramento SPI compartilhado (TFT, SD, CC1101).
void spiBusInit();  // chamar no setup() antes de qualquer usuário do SPI
void spiLock();
void spiUnlock();

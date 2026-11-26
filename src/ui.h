#pragma once
// Task de UI (core 1): encoder, telas, overlays de SOS, banner e OLED.
#include "storage.h"

void uiStart(Settings& s);       // task "ui", core 1, prioridade 2, stack 8192
void uiRequestSosCountdown();    // thread-safe; usado pela detecção de queda (etapa 5)
void uiWakeScreen();             // thread-safe; acorda o TFT por movimento sem abrir SOS

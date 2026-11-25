#include "lora.h"

#include <Arduino.h>
#include "config.h"
#include "lora_at.h"

namespace {

bool auxBusy() { return digitalRead(PIN_LORA_AUX) == HIGH; }  // alto = ocupado

bool waitAuxLow(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (auxBusy()) {
    if (millis() - t0 >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return true;
}

void drainInput() {
  while (Serial2.available()) Serial2.read();
}

// Lê uma linha (sem CR/LF). Retorna false em timeout sem nenhuma linha.
bool readLine(char* buf, size_t cap, uint32_t timeoutMs) {
  size_t n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial2.available()) {
      int c = Serial2.read();
      if (c == '\n') {
        buf[n] = 0;
        if (n == 0) continue;  // linha vazia
        return true;
      }
      if (c == '\r') continue;
      if (n + 1 < cap) buf[n++] = (char)c;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  buf[n] = 0;
  return n > 0;
}

// Envia "+++" e espera "Entry AT" (ou "Exit AT", se já estava em modo AT).
// Retorna 1 = entrou, 0 = saiu, -1 = sem resposta.
int toggleAt() {
  drainInput();
  Serial2.print("+++");
  char line[48];
  const uint32_t t0 = millis();
  while (millis() - t0 < 1000) {
    if (!readLine(line, sizeof line, 1000 - (millis() - t0))) break;
    if (strstr(line, "Entry AT")) return 1;
    if (strstr(line, "Exit AT")) return 0;
  }
  return -1;
}

bool enterAt() {
  for (int i = 0; i < 2; ++i) {
    int r = toggleAt();
    if (r == 1) return true;
    if (r == 0) {  // estava em modo AT e saiu (reinicia): tenta de novo
      vTaskDelay(pdMS_TO_TICKS(300));
      waitAuxLow(2000);
      continue;
    }
    return false;
  }
  return false;
}

// Envia um comando e coleta a resposta até OK/ERROR. `value` recebe o "+KEY=valor" se houver.
bool command(const char* cmd, const char* key, char* value, size_t cap) {
  drainInput();
  Serial2.print(cmd);
  Serial2.print("\r\n");
  char line[64];
  bool got = false;
  const uint32_t t0 = millis();
  while (millis() - t0 < 500) {
    if (!readLine(line, sizeof line, 500)) break;
    if (key && value && atParseQuery(line, key, value, cap)) got = true;
    if (atIsOk(line)) return key ? got : true;
    if (atIsError(line)) {
      LOGW("LoRa: %s -> %s", cmd, line);
      return false;
    }
  }
  LOGW("LoRa: %s sem resposta", cmd);
  return false;
}

void dumpHelp() {
  drainInput();
  Serial2.print("AT+HELP\r\n");
  char line[64];
  while (readLine(line, sizeof line, 500)) {
    LOGD("LoRa HELP: %s", line);
    if (atIsOk(line) || atIsError(line)) break;
  }
}

}  // namespace

bool loraBegin() {
  // Com AT+SWITCH0, M0/M1 são saídas do módulo: o ESP nunca os aciona.
  pinMode(PIN_LORA_M0, INPUT_PULLDOWN);
  pinMode(PIN_LORA_M1, INPUT_PULLDOWN);
  pinMode(PIN_LORA_AUX, INPUT);
  Serial2.setRxBufferSize(1024);
  Serial2.begin(LORA_BAUD, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);

  if (!waitAuxLow(2000)) LOGW("LoRa: AUX continua alto apos 2 s");
  if (!enterAt()) {
    LOGE("LoRa sem resposta AT: seguindo em modo transparente com a config atual");
    return false;
  }

  AtSetting desired[8];
  size_t n = loraDesiredSettings(LORA_CHANNEL_HEX, desired, 8);
  bool ok = true;
  for (size_t i = 0; i < n; ++i) {
    char q[24], have[16] = "";
    snprintf(q, sizeof q, "AT+%s", desired[i].key);
    if (command(q, desired[i].key, have, sizeof have) &&
        atValueEquals(desired[i].key, have, desired[i].value)) {
      continue;  // já está certo: não grava (poupa a flash do módulo)
    }
    char set[32];
    snprintf(set, sizeof set, "AT+%s%s", desired[i].key, desired[i].value);
    LOGI("LoRa: %s (era '%s')", set, have);
    if (!command(set, nullptr, nullptr, 0)) ok = false;
  }
  dumpHelp();

  if (toggleAt() != 0) LOGW("LoRa: sem 'Exit AT'");
  vTaskDelay(pdMS_TO_TICKS(300));  // módulo reinicia ao sair do modo AT
  waitAuxLow(3000);
  drainInput();
  LOGI("LoRa pronto (config %s)", ok ? "ok" : "com erros");
  return ok;
}

void loraPoll(Framer& f, uint32_t now) {
  while (Serial2.available()) f.feed((uint8_t)Serial2.read(), now);
}

bool loraWrite(const char* d, size_t n) {
  if (!waitAuxLow(3000)) {
    LOGW("LoRa: AUX ocupado, frame descartado");
    return false;
  }
  Serial2.write((const uint8_t*)d, n);
  return true;
}

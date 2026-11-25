#include "input.h"

#include <Arduino.h>
#include <soc/gpio_reg.h>
#include "config.h"
#include "input_logic.h"

// A ISR só lê o registrador de entrada e guarda o estado A/B num buffer circular.
// Nada de código em flash dentro da ISR: gravações na NVS desligam o cache da flash.
static_assert(PIN_ENC_A < 32 && PIN_ENC_B < 32, "ISR le GPIO_IN_REG (pinos 0-31)");

namespace {
constexpr uint8_t kRing = 128;  // potência de 2
volatile uint8_t gRing[kRing];
volatile uint8_t gHead = 0, gTail = 0;
QuadDecoder gDec(ENC_STEPS_PER_DETENT);

void IRAM_ATTR isrEncoder() {
  uint32_t in = REG_READ(GPIO_IN_REG);
  uint8_t ab = (uint8_t)((((in >> PIN_ENC_A) & 1) << 1) | ((in >> PIN_ENC_B) & 1));
  uint8_t h = gHead;
  uint8_t n = (uint8_t)((h + 1) & (kRing - 1));
  if (n != gTail) {  // cheio: descarta (o decodificador rejeita o salto)
    gRing[h] = ab;
    gHead = n;
  }
}
}  // namespace

void inputBegin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  gDec.reset(digitalRead(PIN_ENC_A), digitalRead(PIN_ENC_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), isrEncoder, CHANGE);
}

int8_t inputTakeDetents() {
  int sum = 0;
  while (gTail != gHead) {
    uint8_t ab = gRing[gTail];
    gTail = (uint8_t)((gTail + 1) & (kRing - 1));
    sum += gDec.update(ab & 2, ab & 1);
  }
  if (sum > 127) sum = 127;
  if (sum < -127) sum = -127;
  return (int8_t)sum;
}

bool inputButtonPressed() { return digitalRead(PIN_ENC_SW) == LOW; }

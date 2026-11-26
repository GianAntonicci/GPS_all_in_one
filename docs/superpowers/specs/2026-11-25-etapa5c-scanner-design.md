# Design — Etapa 5c: Scanner CC1101 (somente RX)

Data: 2026-11-25 · Complementa `2026-11-25-dispositivo-gps-lora-design.md` e `2026-11-25-etapa3-ui-design.md`

Terceira e última parte da etapa 5 (5a áudio e 5b IMU concluídas, **5c scanner CC1101**).

## Intenção

Permitir inspecionar rapidamente a atividade de RF nas faixas ISM sub-GHz (433, 868 e 915 MHz) em campo — por exemplo, ver interferência na faixa de 915 MHz onde opera o LoRa ou detectar sinais próximos — **somente em recepção** e gastando praticamente zero bateria fora da tela do scanner.

## Decisões do usuário

| Tema | Decisão |
|---|---|
| Driver CC1101 | **Abordagem A:** driver mínimo próprio somente-RX em `src/cc1101` usando `SPI.beginTransaction`/`endTransaction` sob `spiLock()/spiUnlock()` (substitui `SmartRC-CC1101-Driver-Lib`, que chama `SPI.begin()`/`SPI.end()` a cada registrador e tem `while(digitalRead(MISO))` sem timeout no reset) |
| Controles na tela | Girar o encoder troca a faixa (`433` ↔ `868` ↔ `915 MHz`); clique curto volta ao menu |

## Decisões minhas, que o usuário pode corrigir

- **Faixas e resolução (60 pontos por faixa, 300 px úteis na tela 320×240):**
  - `433 MHz`: `430,0` a `435,9 MHz` (passo `0,1 MHz`);
  - `868 MHz`: `865,0` a `870,9 MHz` (passo `0,1 MHz`);
  - `915 MHz`: `902,0` a `931,5 MHz` (passo `0,5 MHz` — cobre 902–928 MHz incluindo o canal LoRa 921,15 MHz).
- **Varredura incremental sem travar a UI:** em vez de varrer os 60 pontos numa chamada bloqueante (~150 ms), a `uiTask` mede um lote pequeno (**4 pontos por iteração**, ~8 ms) apenas quando a tela ativa é `Screen::Scanner`, a tela está acesa e não há overlay de SOS. Isso mantém encoder e overlays de SOS responsivos.
- **Sleep agressivo:** o CC1101 entra em `SIDLE` + `SPWD` logo após o boot e sempre que sair da tela do Scanner, apagar a tela ou abrir um overlay de SOS.
- **Peak-hold com decaimento:** cada barra mostra o RSSI atual e uma marca de pico (peak-hold) que decai lentamente (~2 dB por varredura completa), com destaque no cabeçalho para a frequência e dBm do maior pico da faixa.
- **Módulo ausente:** se o CC1101 não responder no boot (`VERSION` inválido ou timeout de MISO), grava `state().cc1101Ok = false`, registra no log e a tela Scanner mostra `"CC1101 nao detectado"` sem travar o aparelho.
- **Limpeza do `platformio.ini`:** remove `lsatan/SmartRC-CC1101-Driver-Lib` já que o driver mínimo próprio substitui seu uso.

## Abordagens consideradas

- **A. Driver mínimo próprio somente-RX em `src/cc1101` + lógica pura em `lib/scanner/` (escolhida).** ~120 linhas auditáveis, usa `SPI.beginTransaction`/`endTransaction` sob `spiLock()`, tem timeout em MISO, nunca emite `STX` (garantidamente somente-RX) e permite testar todo o cálculo de frequência, conversão de RSSI e estado de varredura/peak-hold no host.
- **B. Patchear `SmartRC-CC1101-Driver-Lib` em `lib/`.** Rejeitada: carrega ~1.300 linhas de código de TX/GDO0 não usado só para ler o registrador `RSSI`.

## Lógica pura (`lib/scanner/`, testada no host)

- **`cc1101_math`:**
  - `cc1101FreqWord(float mhz)` → palavra de 24 bits para cristal de 26 MHz (`round(mhz * 65536.0 / 26.0)`), separada em `FREQ2`, `FREQ1`, `FREQ0`.
  - `cc1101RssiDbm(uint8_t raw)` → conversão oficial do datasheet (`raw >= 128 ? (raw - 256) / 2 - 74 : raw / 2 - 74`).
  - `rssiToBarHeight(int16_t dbm, uint16_t maxH)` → mapeia `-120..-30 dBm` para `0..maxH` com clamp.
- **`ScannerModel`:**
  - 3 faixas (`433`, `868`, `915 MHz`), `kBins = 60` pontos cada.
  - `setBand(uint8_t idx)` / `stepBand(int8_t delta)` (troca circular de faixa e limpa barras/picos).
  - `nextFreqMhz()` retorna a frequência do próximo ponto a medir.
  - `recordSample(int16_t dbm)` grava o ponto atual, atualiza `peak[i]` (com decaimento ao completar uma volta) e avança o cursor de varredura.
  - `peakBin()`, `peakFreqMhz()`, `peakDbm()` para o cabeçalho da tela.

## Glue (`src/cc1101`, `src/ui_screens`, `src/ui`)

- **`src/cc1101.{h,cpp}`:**
  - `bool cc1101Begin()`: reset (`SRES` com timeout em MISO), lê `VERSION` (`0x31`, aceita `0x14` ou `0x04`), configura registradores RX (BW ~203 kHz, `MCSM0 = 0x18` autocalibração IDLE→RX, `AGCCTRL`), chama `cc1101Sleep()`, atualiza `state().cc1101Ok`.
  - `bool cc1101MeasureRssi(float mhz, int16_t& outDbm)`: sob `spiLock()`, `SIDLE` → escreve `FREQ2/1/0` → `SRX` → aguarda ~1,5 ms para sintonia/AGC → lê registrador de status `RSSI` (`0x34 | 0xC0`) → converte com `cc1101RssiDbm()`.
  - `void cc1101Sleep()`: sob `spiLock()`, envia `SIDLE` + `SPWD`.
  - Nenhuma função ou strobe de TX (`STX`) existe no módulo.
- **Tela `ui::drawScanner(TFT_eSprite& f, const ScannerModel& m, bool hwOk)`:**
  - Cabeçalho com faixa ativa (`433 / 868 / 915 MHz`) e maior pico (`freq + dBm`);
  - Área de espectro 300×156 px com 60 barras + marcadores de peak-hold + linhas de referência (-100, -80, -60 dBm);
  - Rodapé com início/meio/fim da faixa e dica `"Giro: faixa  Clique: voltar"`;
  - Quando `!hwOk`: exibe aviso `"CC1101 nao detectado"`.
- **`src/ui.cpp` e `src/debug_console.cpp`:**
  - Chama `cc1101Begin()` na inicialização da `uiTask` (após `displayBegin()`).
  - Na `Screen::Scanner`: giro chama `stepBand(d)`; clique volta (`pop()`). Enquanto ativa (tela acesa, sem overlay, `cc1101Ok`), mede até 4 pontos por volta do loop e marca `gDirty = true`; ao sair da condição ativa, chama `cc1101Sleep()`.
  - `status` no `debug_console` inclui `cc1101=ok/ERRO`.

## Testes

- **Host (`test/test_scanner/`):**
  - `cc1101FreqWord`: valores exatos para 433,92 MHz, 868,0 MHz e 921,15 MHz;
  - `cc1101RssiDbm`: positivo, zero, negativo par e ímpar (complemento de 2);
  - `rssiToBarHeight`: clamp abaixo de -120 dBm, acima de -30 dBm e meio da escala;
  - `ScannerModel`: cálculo de frequência por bin nas 3 faixas, troca circular de faixa limpa os dados, peak-hold registra máximo e decai a cada volta completa, `peakBin()`/`peakFreqMhz()`/`peakDbm()`.
- **Firmware:** `pio run -e esp32s3` compila sem erros ou warnings novos.
- **Hardware (depois):** abrir Scanner e verificar barras em 433/868/915 MHz; transmitir com o segundo aparelho em 921,15 MHz e ver o pico subir próximo de 921 MHz; sair da tela e confirmar TFT e SD continuam funcionando normalmente no SPI compartilhado.

## Deixado ao builder

- Cores exatas das barras/linhas de grade e tempo fino de estabilização de RSSI (1–2 ms).
- Nomes internos auxiliares de SPI.

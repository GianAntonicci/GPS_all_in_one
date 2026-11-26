# Etapa 5c — Scanner CC1101 (somente RX) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scanner de espectro sub-GHz (433, 868 e 915 MHz) somente em recepção usando driver mínimo próprio do CC1101 no SPI compartilhado, com barras de RSSI e peak-hold na UI e sleep (`SIDLE` + `SPWD`) fora da tela.

**Architecture:** A lógica pura de cálculo de frequência (26 MHz), conversão de RSSI e modelo de varredura/peak-hold fica em `lib/scanner/scanner_model` (sem Arduino, testada no host). O driver `src/cc1101` fala direto via `SPI.beginTransaction`/`endTransaction` sob `spiLock()/spiUnlock()` com timeout em MISO e sem nenhum comando de TX. A `uiTask` varre 4 pontos por iteração apenas quando `Screen::Scanner` está visível e sem overlay de SOS, colocando o rádio em sleep imediatamente ao sair.

**Tech Stack:** ESP32-S3, Arduino-ESP32 2.0.x, `SPI`, `TFT_eSPI`, FreeRTOS, Unity.

**Spec:** `docs/superpowers/specs/2026-11-25-etapa5c-scanner-design.md`

## Global Constraints

- Commits semânticos em português, **sem** `Co-Authored-By` nem "Generated with Claude Code".
- `lib/` sem Arduino, compila no `native` (`-std=gnu++17 -Wall`).
- **Somente recepção:** nenhuma função ou strobe de TX (`STX = 0x35`) existe em `src/cc1101`.
- **SPI compartilhado:** `src/cc1101` nunca chama `SPI.begin()` nem `SPI.end()`; toda transação SPI usa `spiLock()` + `SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0))` + `SPI.endTransaction()` + `spiUnlock()`.
- **Timeout em MISO:** nenhuma espera de `MISO == LOW` sem timeout; módulo ausente marca `state().cc1101Ok = false` e nunca trava o boot ou a UI.
- **Sleep fora da tela:** `cc1101Sleep()` (`SIDLE` + `SPWD`) no fim de `cc1101Begin()` e sempre que sair da tela Scanner, apagar o TFT ou abrir overlay de SOS.
- Faixas fixas (60 pontos cada):
  - `433 MHz`: `430.0f` MHz, passo `0.1f` MHz;
  - `868 MHz`: `865.0f` MHz, passo `0.1f` MHz;
  - `915 MHz`: `902.0f` MHz, passo `0.5f` MHz.

## Review Focus

1. **Conversão de RSSI complemento de 2 (positivo, zero, negativo par e ímpar):** erro de sinal faria ruído baixo aparecer como sinal forte. Teste no Task 1 (`test_rssi_conversion`).
2. **Troca circular de faixa com delta negativo (`stepBand(-1)` a partir de 0):** sem underflow nem lixo da faixa anterior. Teste no Task 1 (`test_band_switching_wraps_and_resets`).
3. **Peak-hold com decaimento:** ao completar uma volta de 60 pontos, os picos decaem sem nunca cair abaixo do RSSI atual nem do piso (`-120 dBm`). Teste no Task 1 (`test_peak_hold_and_decay`).
4. **SPI compartilhado sem travar `spiLock()` durante espera de sintonia:** `cc1101MeasureRssi()` solta `spiUnlock()` durante a espera de ~1,5 ms de sintonia/AGC e readquire apenas para ler o registrador `RSSI`; timeout em MISO evita travamento sem hardware. Verificação no Task 2.
5. **Sleep garantido na saída da tela / tela apagada / overlay de SOS:** transição ativo → inativo em `uiTask` sempre chama `cc1101Sleep()`, e nunca varre durante overlay de SOS. Verificação no Task 3.

---

### Task 1: Lógica pura do scanner (`lib/scanner/scanner_model`)

**Files:**
- Create: `lib/scanner/scanner_model.h`, `lib/scanner/scanner_model.cpp`
- Test: `test/test_scanner/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  uint32_t cc1101FreqWord(float mhz);                     // 26 MHz XOSC: round(mhz * 65536 / 26.0)
  int16_t cc1101RssiDbm(uint8_t raw);                     // datasheet: >=128 ? (raw-256)/2 - 74 : raw/2 - 74
  uint16_t rssiToBarHeight(int16_t dbm, uint16_t maxH);   // -120..-30 dBm -> 0..maxH

  struct BandDef {
    const char* label;
    float startMhz;
    float stepMhz;
  };

  class ScannerModel {
   public:
    static constexpr uint8_t kBandCount = 3;
    static constexpr uint8_t kBins = 60;
    static constexpr int16_t kFloorDbm = -120;
    static constexpr int16_t kCeilDbm = -30;
    static constexpr int16_t kPeakDecayDb = 2;

    ScannerModel();
    void setBand(uint8_t idx);
    void stepBand(int8_t delta);
    void reset();
    uint8_t bandIndex() const;
    const BandDef& band() const;
    uint8_t cursor() const;
    float binFreqMhz(uint8_t bin) const;
    float nextFreqMhz() const;
    void recordSample(int16_t dbm);
    int16_t rssi(uint8_t bin) const;
    int16_t peak(uint8_t bin) const;
    bool hasData() const;
    uint8_t peakBin() const;
    float peakFreqMhz() const;
    int16_t peakDbm() const;
  };
  ```

- [ ] **Step 1: Write the failing tests** em `test/test_scanner/test_main.cpp`:
  - `test_freq_word_known_frequencies`: 433.92 MHz → `0x10B071` (±1), 868.0 MHz → `0x216276` (±1), 921.15 MHz → `0x236D95` (±1).
  - `test_rssi_conversion` (Review Focus 1): `raw=0` → `-74`, `raw=20` → `-64`, `raw=128` → `-138`, `raw=200` → `-102`, `raw=255` → `-74` (ou `-75`, verificar divisão inteira consistente `(int16_t)(raw - 256) / 2 - 74`).
  - `test_rssi_to_bar_height`: `-130` → `0`, `-120` → `0`, `-75` → `maxH / 2`, `-30` → `maxH`, `0` → `maxH`.
  - `test_band_frequencies`: verifica `binFreqMhz(0)` e `binFreqMhz(59)` nas 3 faixas (`433`: 430.0..435.9, `868`: 865.0..870.9, `915`: 902.0..931.5).
  - `test_band_switching_wraps_and_resets` (Review Focus 2): `stepBand(-1)` a partir de 0 vai para 2 (`915 MHz`), `stepBand(1)` volta a 0 (`433 MHz`), e limpa `hasData()` / barras / picos.
  - `test_peak_hold_and_decay` (Review Focus 3): grava `-50 dBm` no bin 10 na volta 1 → `peakBin() == 10`, `peakDbm() == -50`; na volta 2 grava `-100 dBm` em todos os bins → ao completar a volta, `peak(10)` decai `kPeakDecayDb` (para `-52 dBm`), mas nunca cai abaixo do `rssi(i)` atual nem de `kFloorDbm`.

- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_scanner`
  Expected: FAIL (arquivo não existe)

- [ ] **Step 3: Implement** `lib/scanner/scanner_model.h` e `lib/scanner/scanner_model.cpp`.

- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native`
  Expected: PASS na suíte inteira

- [ ] **Step 5: Commit**
  ```bash
  git add lib/scanner/scanner_model.h lib/scanner/scanner_model.cpp test/test_scanner/test_main.cpp
  git commit -m "feat(scanner): adiciona logica pura do CC1101 e modelo de varredura"
  ```

---

### Task 2: Driver mínimo somente-RX (`src/cc1101`) e limpeza do `platformio.ini`

**Files:**
- Create: `src/cc1101.h`, `src/cc1101.cpp`
- Modify: `src/state.h`, `src/debug_console.cpp`, `platformio.ini`

**Interfaces:**
- Consumes: Task 1 (`cc1101FreqWord`, `cc1101RssiDbm`), `spiLock`/`spiUnlock` (`src/spi_bus.h`), `stateLock`/`stateUnlock`/`state` (`src/state.h`), `PIN_CC1101_CS`/`PIN_SPI_MISO` (`src/config.h`).
- Produces:
  ```cpp
  // src/cc1101.h
  bool cc1101Begin();                              // reset + probe VERSION + config RX + sleep
  bool cc1101Ok();                                 // true se o módulo respondeu no boot
  bool cc1101MeasureRssi(float mhz, int16_t& dbm); // sintonia RX + leitura RSSI (somente RX)
  void cc1101Sleep();                              // SIDLE + SPWD
  ```
- **Detalhes:**
  - `SPISettings(4000000, MSBFIRST, SPI_MODE0)`.
  - `waitMisoLow(uint32_t timeoutUs = 5000)`: espera `digitalRead(PIN_SPI_MISO) == LOW` com `micros()` timeout; retorna `false` se expirar.
  - `cc1101Begin()`:
    - Pulso de reset manual em `PIN_CC1101_CS` (HIGH → LOW → HIGH → LOW + `waitMisoLow()` + `SRES` `0x30` + `waitMisoLow()`), tudo sob `spiLock()`.
    - Lê `VERSION` (`0x31 | 0xC0`): aceita `0x14` ou `0x04` (revisões do CC1101). Se inválido ou timeout: `LOGE("CC1101 nao respondeu (ver=0x%02X)")`, `cc1101Ok = false`, retorna `false`.
    - Configura registradores RX para varredura RSSI (cristal 26 MHz, `FSCTRL1 = 0x06`, `MDMCFG4 = 0x8B` ~203 kHz BW, `MDMCFG2 = 0x00` 2-FSK sem sync, `MCSM0 = 0x18` autocalibração IDLE→RX, `FOCCFG = 0x16`, `AGCCTRL2 = 0x43`, `AGCCTRL1 = 0x40`, `AGCCTRL0 = 0x91`, `FREND1 = 0x56`, `FSCAL3/2/1/0` padrão TI).
    - Chama `cc1101Sleep()`, grava `state().cc1101Ok = true`, retorna `true`.
  - `cc1101MeasureRssi(float mhz, int16_t& dbm)`:
    - Se `!gOk` retorna `false`.
    - Sob `spiLock()`: `SIDLE`, escreve `FREQ2`, `FREQ1`, `FREQ0` (de `cc1101FreqWord(mhz)`), envia `SRX` (`0x34`).
    - Solta `spiUnlock()` e espera `delayMicroseconds(1500)` (tempo de sintonia/calibração/AGC sem segurar o SPI de TFT/SD).
    - Sob `spiLock()`: lê status `RSSI` (`0x34 | 0xC0`) e converte via `cc1101RssiDbm(raw)`.
  - `cc1101Sleep()`: se `!gOk` retorna; sob `spiLock()`, envia `SIDLE` (`0x36`) e `SPWD` (`0x39`).
  - `src/state.h`: adiciona `bool cc1101Ok = false;` em `SharedState`.
  - `src/debug_console.cpp`: inclui `cc1101=%s` no `printStatus()`.
  - `platformio.ini`: remove `lsatan/SmartRC-CC1101-Driver-Lib@^2.5.7`.

- [ ] **Step 1: Implement** `src/cc1101.h`, `src/cc1101.cpp` e atualize `src/state.h`, `src/debug_console.cpp`, `platformio.ini`.

- [ ] **Step 2: Compile firmware**
  Run: `pio run -e esp32s3 2>&1 | tail -5`
  Expected: `SUCCESS`, sem warnings novos em `src/`

- [ ] **Step 3: Run host suite**
  Run: `pio test -e native`
  Expected: PASS

- [ ] **Step 4: Commit**
  ```bash
  git add src/cc1101.h src/cc1101.cpp src/state.h src/debug_console.cpp platformio.ini
  git commit -m "feat(scanner): adiciona driver minimo somente-RX do CC1101 no SPI compartilhado"
  ```

---

### Task 3: Tela de espectro e integração na UI (`src/ui_screens`, `src/ui`)

**Files:**
- Modify: `src/ui_screens.h`, `src/ui_screens.cpp`, `src/ui.cpp`, `docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md`

**Interfaces:**
- Consumes: Task 1 (`ScannerModel`, `rssiToBarHeight`), Task 2 (`cc1101Begin`, `cc1101Ok`, `cc1101MeasureRssi`, `cc1101Sleep`).
- Produces:
  ```cpp
  // src/ui_screens.h
  void drawScanner(TFT_eSprite& f, const ScannerModel& m, bool hwOk);
  ```
- **Detalhes:**
  - `ui::drawScanner`:
    - Se `!hwOk`: chama `drawSoon(f, "Scanner", "CC1101 nao detectado")` e retorna.
    - Cabeçalho (y = 0..34): título `"Scanner  " + band.label` à esquerda; se `m.hasData()`, mostra `"Pico %.1f  %ddBm"` à direita.
    - Área do gráfico (`x = 10..310`, `y = 40..196`, altura `156 px`):
      - Linhas horizontais sutis para `-100`, `-80`, `-60` dBm;
      - 60 barras (`w = 4 px`, passo `5 px`), altura `rssiToBarHeight(m.rssi(i), 156)`;
      - Marcador horizontal de 2 px no topo de `rssiToBarHeight(m.peak(i), 156)` em cor destacada (amarelo/laranja);
      - Linha fina sutil no bin atual `m.cursor()` indicando a varredura viva.
    - Rodapé (`y = 202..236`): labels de frequência início / meio / fim da faixa (`startMhz`, `midMhz`, `endMhz`) e dica `"Giro: faixa   Clique: voltar"`.
  - `src/ui.cpp`:
    - `ScannerModel gScanner;` e `bool gScannerActive = false;`.
    - Em `uiTask`: chama `cc1101Begin()` logo após `displayBegin()` e `mapInit()`.
    - Em `onScreenInput`: caso `s == Screen::Scanner` (igual a `Screen::Map`): `if (d) { gScanner.stepBand(d); gDirty = true; }`, `if (g == Gesture::Click) pop(); return;`.
    - No loop de `uiTask` (após atualizar `on = gWake.screenOn()`):
      - `bool scanNow = (top() == Screen::Scanner) && on && !gOverlay.active() && cc1101Ok();`
      - Se `scanNow`:
        - `gScannerActive = true;`
        - Varre até 4 bins (`constexpr uint8_t kScanBatch = 4`): mede `cc1101MeasureRssi(gScanner.nextFreqMhz(), dbm)`; se ok chama `gScanner.recordSample(dbm)`; marca `gDirty = true`.
      - Senão se `gScannerActive`:
        - `cc1101Sleep(); gScannerActive = false;`
    - Em `draw()`: substitui `ui::drawSoon(f, "Scanner", ...)` por `ui::drawScanner(f, gScanner, s.cc1101Ok)`.
  - `docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md`: atualiza a nota sobre o driver CC1101 (driver mínimo próprio somente-RX sob `spiLock()`, sem `SmartRC-CC1101-Driver-Lib`) e aponta para `2026-11-25-etapa5c-scanner-design.md`.

- [ ] **Step 1: Implement** `ui::drawScanner`, integração em `src/ui.cpp` e atualização da spec principal.

- [ ] **Step 2: Compile firmware**
  Run: `pio run -e esp32s3 2>&1 | tail -5`
  Expected: `SUCCESS`, sem warnings novos em `src/`

- [ ] **Step 3: Run host suite**
  Run: `pio test -e native`
  Expected: PASS

- [ ] **Step 4: Commit**
  ```bash
  git add src/ui_screens.h src/ui_screens.cpp src/ui.cpp docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md
  git commit -m "feat(scanner): adiciona tela de espectro RSSI com peak-hold e sleep automatico"
  ```

# Etapa 5a — Áudio — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tons por I2S (MAX98357A): clique, mensagem, alarme do SOS do amigo, contagem e "recebido" e "visto" do meu SOS. Os sons de SOS ignoram o volume.

**Architecture:** A lógica pura fica em `lib/audio/`:
- `tone_seq`: padrões, precedência e amplitude;
- `audio_cues`: decide quando tocar a partir do estado da UI.

A glue `src/audio` é uma task dona do I2S, alimentada por fila. A UI só repassa o estado e os eventos.

**Tech Stack:** ESP32-S3, Arduino-ESP32 2.0.x, I2S legado (`driver/i2s.h`), FreeRTOS, Unity.

**Spec:** `docs/superpowers/specs/2026-11-25-etapa5a-audio-design.md`

## Global Constraints

- Commits semânticos em português, **sem** `Co-Authored-By` nem "Generated with Claude Code".
- `lib/` sem Arduino, compila no `native` (`-std=gnu++17 -Wall`).
- **Os padrões `PeerAlarm`, `SosSeen`, `SosReceived`, `CountdownFinal` e `CountdownTick` tocam com amplitude alta fixa (26000), mesmo com o volume em 0.** `Message`, `Hold` e `Click` seguem o volume (0 = mudo).
- Prioridades (1 = maior):
  - `PeerAlarm` 1;
  - `SosSeen` e `SosReceived` 2;
  - `CountdownFinal` e `CountdownTick` 3;
  - `Message` 4;
  - `Hold` e `Click` 5.
- Um pedido de prioridade menor que a do padrão tocando é ignorado. Um pedido de prioridade igual ou maior substitui o atual. `play` do padrão que já está tocando não faz nada (não reinicia a fase).
- A UI nunca bloqueia no áudio. A task `audio` roda no core 1, prioridade 3, stack 4096, com fila de 8.
- I2S: 16 kHz, 16 bit, stereo com o mesmo valor nos dois canais; BCLK 15, LRC 16, DIN 17; DMA de 4 × 256. Ociosa: `i2s_zero_dma_buffer` + `i2s_stop`.

## Review Focus

1. **Volume 0 com o amigo pedindo SOS:** o alarme precisa tocar alto. Teste no Task 1 (`test_loud_patterns_ignore_volume`).
2. **Um clique durante o alarme:** o clique não pode interromper nem reiniciar o alarme. Teste no Task 1.
3. **Contagem cancelada e reiniciada:** o bipe do "10" precisa tocar de novo no novo início, sem ficar preso no último número da contagem anterior. Teste no Task 2.
4. **O amigo cancela o SOS enquanto o alarme toca:** o alarme para; sem um `stop`, ele tocaria para sempre. Teste no Task 2.
5. **Tela apagada:** a UI continua chamando `AudioCues.update` com a tela apagada (o laço não depende de `on`). Fica para a revisão do Task 3.

---

### Task 1: Sequenciador de tons (`lib/audio/tone_seq`)

**Files:**
- Create: `lib/audio/tone_seq.h`, `lib/audio/tone_seq.cpp`
- Test: `test/test_tone_seq/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class Pattern : uint8_t { None, PeerAlarm, SosSeen, SosReceived, CountdownFinal,
                                 CountdownTick, Message, Hold, Click };
  struct Tone { uint16_t freqHz; bool loud; };  // freqHz 0 = silêncio
  uint8_t patternPriority(Pattern p);           // 1..5; None = 255
  bool patternLoud(Pattern p);
  class ToneSequencer {
   public:
    void play(Pattern p, uint32_t now);
    void stop(Pattern p);         // só para se p é o que está tocando
    Tone current(uint32_t now);   // padrão de uma vez que acabou vira None
    Pattern playing() const;
  };
  int16_t toneAmplitude(uint8_t volumePct, bool loud);  // loud → 26000; senão 26000·v²/10000; v>100 = 100
  ```
- Passos de cada padrão `{freqHz, ms}` (freq 0 = pausa):
  - `PeerAlarm` {900,250} {1400,250}, que repete;
  - `SosSeen` {1600,300} {0,120} {1600,300};
  - `SosReceived` {800,120} {1200,120} {1600,120};
  - `CountdownFinal` {2000,150};
  - `CountdownTick` {1000,80};
  - `Message` {1000,100} {0,60} {1500,100};
  - `Hold` {1500,80};
  - `Click` {2000,15}.

- [ ] **Step 1: Write the failing tests:**
  - `test_idle_is_silent`: `current(0).freqHz` == 0 e `playing()` == None.
  - `test_message_steps`:
    1. `play(Message, 0)`.
    2. Freq 1000 em t=0 e t=99; 0 em t=100 e t=159; 1500 em t=160 e t=259.
    3. Em t=260 a freq é 0 e `playing()` == None.
    4. `loud` é false.
  - `test_alarm_repeats`: `play(PeerAlarm,0)` → 900 em t=0, 1400 em t=250, 900 em t=500 e t=10_000_010. Sempre `loud`.
  - `test_click_does_not_interrupt_alarm` (Review Focus 2):
    1. Alarme em t=0; `play(Click, 300)`.
    2. Em t=300, `playing()` == PeerAlarm e a freq é 1400 (a fase não reinicia).
  - `test_play_same_pattern_keeps_phase`: alarme em 0; `play(PeerAlarm, 300)` → em t=300 a freq ainda é 1400.
  - `test_sos_interrupts_message`: Message em 0; `play(SosReceived, 50)` → em t=50 a freq é 800 e `playing()` == SosReceived.
  - `test_equal_priority_replaces`: Hold em 0; `play(Click, 10)` → `playing()` == Click.
  - `test_stop_only_matching`:
    1. Alarme tocando; `stop(Message)` → continua no alarme.
    2. `stop(PeerAlarm)` → None.
  - `test_loud_patterns_ignore_volume` (Review Focus 1):
    - `toneAmplitude(0, true)` == 26000 e `toneAmplitude(100, true)` == 26000.
    - `toneAmplitude(0, false)` == 0, `toneAmplitude(100, false)` == 26000 e `toneAmplitude(50, false)` == 6500.
    - `patternLoud(PeerAlarm)` e `patternLoud(CountdownTick)` são true; `patternLoud(Click)` é false.
  - `test_priorities`: os valores das Global Constraints.

- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_tone_seq`
  Expected: FAIL (não compila)
- [ ] **Step 3: Implement** com uma tabela estática de passos por padrão. `current` calcula o passo por `(now - start) mod duraçãoTotal` quando o padrão repete.
- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native -f test_tone_seq`
  Expected: PASS
- [ ] **Step 5: Commit**
  ```bash
  git add lib/audio/tone_seq.* test/test_tone_seq
  git commit -m "feat(audio): adiciona sequenciador de tons com prioridade e volume do SOS"
  ```

---

### Task 2: Quando tocar (`lib/audio/audio_cues`)

**Files:**
- Create: `lib/audio/audio_cues.h`, `lib/audio/audio_cues.cpp`
- Test: `test/test_audio_cues/test_main.cpp`

**Interfaces:**
- Consumes:
  - `Pattern` (Task 1);
  - `Overlay` (`lib/ui_logic/sos_overlay.h`);
  - `Gesture` (`lib/ui_logic/input_logic.h`);
  - `SosState` (`lib/protocol/lora_link.h`).
- Produces:
  ```cpp
  struct CueInput { Overlay top; bool counting; uint8_t countdownLeft; SosState mine; };
  struct CueOut { Pattern play[2]; uint8_t n; bool stopAlarm; };
  class AudioCues {
   public:
    CueOut update(const CueInput& in);     // chamar a cada iteração da UI
    static Pattern forGesture(Gesture g);  // Click → Click, Hold2s → Hold, outros → None
  };
  ```
- **Regras de `update`:**
  - **Alarme:**
    - `top == PeerSos` → inclui `PeerAlarm` em `play`, em toda chamada (a reafirmação é inofensiva).
    - Na transição de PeerSos para outro valor → `stopAlarm = true`.
  - **Contagem:**
    - Se `counting` e (não contava antes ou `countdownLeft` mudou) → inclui `CountdownFinal` se `countdownLeft ≤ 3`, senão `CountdownTick`.
    - Quando `counting` fica false, o último número é esquecido.
  - **Meu SOS:**
    - Na transição de `mine` para `Received` (de outro estado) → `SosReceived`.
    - Para `Seen` → `SosSeen`.
  - O estado anterior é guardado dentro de `AudioCues`.

- [ ] **Step 1: Write the failing tests:**
  - `test_nothing_when_idle`: `{None,false,0,Idle}` → `n == 0` e `!stopAlarm`.
  - `test_alarm_while_peer_overlay`: duas chamadas com `top=PeerSos` → as duas têm `PeerAlarm`.
  - `test_alarm_stops_when_peer_cancels` (Review Focus 4): PeerSos → None → `stopAlarm` true e `n == 0`. Na chamada seguinte, `stopAlarm` é false.
  - `test_countdown_ticks_each_number`:
    1. `counting` com left 10 → `CountdownTick`.
    2. Left 10 de novo → nada.
    3. Left 9 → `CountdownTick`.
    4. Left 3 → `CountdownFinal`.
  - `test_countdown_restart_beeps_again` (Review Focus 3): contando com 10 → `counting` false → `counting` com 10 → `CountdownTick` na terceira chamada.
  - `test_sos_received_and_seen_once`:
    1. `mine` Sending → nada.
    2. Received → `SosReceived`.
    3. Received → nada.
    4. Seen → `SosSeen`.
  - `test_alarm_and_tick_together`: `top=PeerSos`, `counting`, left 10 → `n == 2`, com os dois padrões.
  - `test_gestures`: `forGesture(Click)` == Click, `forGesture(Hold2s)` == Hold, `forGesture(Hold3s)` == None e `forGesture(None)` == None.
- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_audio_cues`
  Expected: FAIL (não compila)
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native`
  Expected: PASS na suíte inteira
- [ ] **Step 5: Commit**
  ```bash
  git add lib/audio/audio_cues.* test/test_audio_cues
  git commit -m "feat(audio): adiciona regras de quando tocar cada som"
  ```

---

### Task 3: Task de áudio e integração na UI

**Files:**
- Create: `src/audio.h`, `src/audio.cpp`
- Modify:
  - `src/ui.cpp` (início, gestos, `AudioCues` por iteração, MSG recebido, volume em Configurações);
  - `docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md` (seção Áudio: os sons de SOS ignoram o volume, e entram os sons novos).

**Interfaces:**
- Consumes: Task 1 (`ToneSequencer`, `toneAmplitude`, `Pattern`) e Task 2 (`AudioCues`, `CueInput`, `CueOut`).
- Produces:
  ```cpp
  void audioBegin(uint8_t volumePct);  // cria a task "audio" (core 1, prio 3, stack 4096) e o I2S
  void audioPlay(Pattern p);           // não bloqueia; descarta se a fila (8) estiver cheia
  void audioStop(Pattern p);
  void audioSetVolume(uint8_t pct);
  ```
- **Task `audio`:**
  - Com o I2S parado, espera a fila sem timeout. Tocando, lê a fila sem bloquear.
  - Tocando, gera blocos de 128 frames (8 ms):
    1. obtém o `Tone` de `seq.current(millis())`;
    2. gera o seno por tabela de 256 entradas com acumulador de fase;
    3. aplica a rampa de 48 amostras (3 ms) na troca de tom ou de silêncio;
    4. usa a amplitude `toneAmplitude(volume, tone.loud)`;
    5. escreve com `i2s_write(..., portMAX_DELAY)`.
  - Depois de 50 ms com `playing() == None`: `i2s_zero_dma_buffer` + `i2s_stop`. O próximo `Play` chama `i2s_start`.
  - Configuração do I2S: `I2S_NUM_0`, master TX, `I2S_COMM_FORMAT_STAND_I2S`, `I2S_CHANNEL_FMT_RIGHT_LEFT`, `dma_buf_count` 4, `dma_buf_len` 256, `tx_desc_auto_clear` true.
- **`ui.cpp`:**
  - `audioBegin(gSet->volume)` no `uiTask`, depois do `inputBegin`.
  - Depois do filtro do `WakePolicy` (só os gestos processados) e também no Hold2s: `audioPlay(AudioCues::forGesture(g))` quando não for None.
  - Logo após o `for` do `gOverlay.tick`:
    ```cpp
    CueOut o = gCues.update({gOverlay.top(), gOverlay.counting(), gOverlay.countdownLeft(now), s.mySos});
    ```
    Para cada `play[i]` chama `audioPlay`; se `stopAlarm`, chama `audioStop(PeerAlarm)`. Isso roda a cada iteração, com a tela acesa ou apagada.
  - No `onRadioEvent`, pacote `MSG` → `audioPlay(Pattern::Message)`.
  - Em `onSettingsInput`, ao mudar o volume (editing 3, com `d != 0`) → `audioSetVolume(v.volume)` + `audioPlay(Click)`.

- [ ] **Step 1: Implement** `src/audio.h/.cpp` e a integração em `ui.cpp`.
- [ ] **Step 2: Compile**
  Run: `pio run -e esp32s3 2>&1 | tail -3`
  Expected: `SUCCESS`, sem warnings novos em `src/`
- [ ] **Step 3: Run host suite**
  Run: `pio test -e native`
  Expected: PASS
- [ ] **Step 4: Atualizar a seção Áudio da spec principal.**
- [ ] **Step 5: Commit**
  ```bash
  git add src docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md
  git commit -m "feat(audio): adiciona task de áudio I2S e sons na interface"
  ```

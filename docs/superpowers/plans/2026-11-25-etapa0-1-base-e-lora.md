# Etapas 0 e 1 — Base do projeto + troca de mensagens LoRa — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Projeto PlatformIO configurado para ESP32-S3 N16R8 e dois aparelhos trocando MSG/PING/POS/SOS pelo DX-LR22, com ACK, reenvio e SOS em dois níveis, controlados por um console serial de teste.

**Architecture:** A lógica pura (protocolo, framing, enlace com ACK/reenvio/SOS, comandos AT) mora em `lib/` sem dependências de Arduino e é testada no PC (`pio test -e native`). O driver Arduino (`src/lora.*`) só move bytes e configura o módulo. Uma task FreeRTOS de rádio no core 0 liga as duas pontas e conversa com o resto por filas.

**Tech Stack:** PlatformIO, espressif32 6.9.0 (Arduino-ESP32 2.0.x), FreeRTOS, Unity (testes nativos), Preferences (NVS).

**Spec:** `docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md`

## Global Constraints

- Placa `esp32-s3-devkitc-1`, `board_build.arduino.memory_type = qio_opi`, `-DBOARD_HAS_PSRAM`, flash 16 MB.
- TFT_eSPI configurado só por `build_flags` (`USER_SETUP_LOADED`), nunca editando `User_Setup.h`.
- Logs pela USB CDC com `LOGD/LOGI/LOGE`, desligados com `-DDEBUG_LOG=0`.
- Nada de `delay()` longo no loop. Dentro das tasks, usar `vTaskDelay`.
- Pinos (spec): LoRa ESP_RX=40, ESP_TX=39, M0=41, M1=42, AUX=2, `Serial2`, 9600 8N1.
- M0/M1 **sempre** `INPUT_PULLDOWN`; o ESP nunca os aciona.
- AUX alto = módulo ocupado. Escrever só com AUX baixo.
- Protocolo `$<TIPO>,<ID>,<SEQ>,<campos>*<CK>\n`, ASCII puro (bytes 0x20–0x7E, mais `\n`). CK = XOR dos bytes entre `$` e `*`, em 2 hex maiúsculos.
- MSG com no máximo 48 caracteres. lat/lon com 5 casas decimais.
- Configuração do rádio: `AT+SWITCH0`, `AT+MODE0`, `AT+LEVEL0`, `AT+CHANNEL47`, `AT+POWE22`, `AT+DRSSI1`, `AT+PACKET3`, `AT+BAUD3`.
- Canal fixo em `config.h` (`LORA_CHANNEL_HEX "47"`). Não fica no menu nem na NVS.
- Tempos do enlace: timeout de ACK 4000 ms + jitter de 0–1000 ms; MSG/PING com 3 tentativas; SOS a cada 10 000 ms e, depois de 6 tentativas sem ACK ou ao receber ACK, a cada 30 000 ms; SEEN/SOSOFF com a mesma cadência do SOS até receber ACK.
- Commit semântico ao fim de cada task, terminando com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Texto com acentos ou com `$`, `*`, `,`, `\n`** ("você", "ação*") não pode quebrar o framing nem colidir com o byte de RSSI (≥ 0x80). O esperado é que vire ASCII com `?` no lugar e chegue legível. Teste: Task 2, `test_sanitize_*`.
2. **Coordenadas negativas** (o Brasil está no hemisfério sul e oeste): `-23.55052,-46.63331` têm que ir e voltar exatas. Teste: Task 2, `test_pos_roundtrip_negative`.
3. **Lixo no UART:** a linha `Power On` do boot do módulo, bytes soltos, linha longa demais ou frame cortado ao meio têm que ser descartados, e o frame seguinte tem que chegar intacto. Teste: Task 3.
4. **O amigo reinicia e repete números de sequência:** um pacote novo dele não pode ser descartado como duplicado. Por isso a seq inicial é aleatória e a deduplicação expira em 60 s. Teste: Task 4, `test_dedup_expires`.
5. **Os dois disparam SOS ao mesmo tempo:** cada aparelho confirma o SOS do outro com ACK e toca o alarme, e receber o SOS do amigo não apaga o meu. Teste: Task 5, `test_simultaneous_sos`.

---

## File Structure

```
platformio.ini                 envs esp32s3 + native
src/config.h                   pinos, constantes, macros de log
src/main.cpp                   setup/loop: inicializa storage, rádio, console
src/storage.h/.cpp             Settings (ID, brilho, volume) em Preferences. O SD e o GPX entram em etapas futuras.
src/state.h/.cpp               SharedState + mutex
src/lora.h/.cpp                driver DX-LR22: pinos, sessão AT, leitura e escrita com AUX
src/radio_task.h/.cpp          task do core 0: lora + Framer + Link + filas
src/debug_console.h/.cpp       comandos pela Serial USB para testar sem UI
lib/protocol/protocol.h/.cpp   Packet, encode/decode, checksum, sanitize
lib/protocol/framer.h/.cpp     fluxo de bytes -> frames + RSSI
lib/protocol/link.h/.cpp       ACK, reenvio, deduplicação, SOS em dois níveis
lib/protocol/lora_at.h/.cpp    lista de comandos AT e parser de respostas
test/test_protocol/test_main.cpp
test/test_framer/test_main.cpp
test/test_link/test_main.cpp
test/test_sos/test_main.cpp
test/test_lora_at/test_main.cpp
```

Decisão do builder: os módulos puros ficam em `lib/protocol` (e não em `src/`) para o env `native` conseguir compilá-los sem o `main.cpp` do Arduino.

---

### Task 1: Base do projeto (etapa 0)

**Files:**
- Modify: `platformio.ini`
- Create: `src/config.h`
- Modify: `src/main.cpp`
- Modify: `.gitignore` (adicionar `.vscode/`)

**Interfaces:**
- Produces: `config.h` com `PIN_*` de toda a pinagem do spec, `LORA_CHANNEL_HEX`, `LORA_BAUD 9600` e as macros `LOGD(fmt, ...)`, `LOGI`, `LOGW`, `LOGE` (prefixo `[D]/[I]/[W]/[E] <millis> `, `Serial.printf`, no-op quando `DEBUG_LOG == 0`).

- [ ] **Step 1: Escrever o `platformio.ini`**

```ini
[platformio]
default_envs = esp32s3

[env:esp32s3]
platform = espressif32@6.9.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.partitions = default_16MB.csv
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DDEBUG_LOG=1
    ; ---- TFT_eSPI (sem editar User_Setup.h) ----
    -DUSER_SETUP_LOADED=1
    -DST7789_DRIVER=1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_MISO=13
    -DTFT_MOSI=11
    -DTFT_SCLK=12
    -DTFT_CS=10
    -DTFT_DC=9
    -DTFT_RST=-1
    -DTFT_BL=14
    -DTFT_BACKLIGHT_ON=HIGH
    -DLOAD_GLCD=1
    -DLOAD_FONT2=1
    -DLOAD_FONT4=1
    -DSMOOTH_FONT=1
    -DSPI_FREQUENCY=40000000
    -DSPI_READ_FREQUENCY=20000000
    -DSUPPORT_TRANSACTIONS=1
lib_deps =
    bodmer/TFT_eSPI@^2.5.43
    bodmer/TJpg_Decoder@^1.1.0
    adafruit/Adafruit SSD1306@^2.5.13
    adafruit/Adafruit GFX Library@^1.11.11
    mikalhart/TinyGPSPlus@^1.1.0
    electroniccats/MPU6050@^1.4.1
    lsatan/SmartRC-CC1101-Driver-Lib@^2.5.7

[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -DUNIT_TEST -Wall
```

- [ ] **Step 2: Criar `src/config.h`** com todos os pinos da tabela do spec (`PIN_SPI_SCK 12`, …, `PIN_BAT_ADC 1`), `LORA_CHANNEL_HEX "47"` e as macros de log.

- [ ] **Step 3: `src/main.cpp` mínimo.** `setup()` abre a Serial, espera a USB por até 2 s com `millis()` e registra com `LOGI` a versão, `ESP.getPsramSize()` e `ESP.getFlashChipSize()`. `loop()` fica vazio, só com `vTaskDelay(1)`.

- [ ] **Step 4: Compilar**

Run: `pio run -e esp32s3`
Expected: `SUCCESS`, com todas as `lib_deps` instaladas sem erro de nome.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini src/config.h src/main.cpp .gitignore
git commit -m "chore: configura PlatformIO para ESP32-S3 N16R8 com PSRAM OPI e logs"
```

---

### Task 2: Protocolo — encode, decode, checksum, sanitize

**Files:**
- Create: `lib/protocol/protocol.h`, `lib/protocol/protocol.cpp`
- Test: `test/test_protocol/test_main.cpp`

**Interfaces:**
- Produces:
```cpp
constexpr size_t MSG_MAX_LEN = 48;
constexpr size_t FRAME_MAX = 96;           // inclui '\n'
enum class PktType : uint8_t { MSG, POS, PING, ACK, SOS, SEEN, SOSOFF };
struct Packet {
  PktType type; char id[3]; uint8_t seq;
  char text[MSG_MAX_LEN + 1];   // MSG
  double lat, lon;              // POS, SOS
  int32_t alt;                  // POS (m)
  uint8_t bat;                  // POS, SOS (%)
  uint8_t ackSeq;               // ACK
};
uint8_t protoChecksum(const char* s, size_t n);
size_t  protoEncode(const Packet& p, char* out, size_t cap);   // retorna o tamanho incluindo '\n', ou 0
bool    protoDecode(const char* line, size_t len, Packet& out); // line sem '\n'
void    protoSanitize(const char* in, char* out, size_t cap);  // ASCII 0x20..0x7E; '$' '*' e não-ASCII viram '?'; corta em MSG_MAX_LEN
bool    protoNeedsAck(PktType t);  // MSG, PING, SOS, SEEN, SOSOFF
```
Campos por tipo, conforme o spec: `MSG,id,seq,texto` · `POS,id,seq,lat,lon,alt,bat` · `PING,id,seq` · `ACK,id,seq,ackSeq` · `SOS,id,seq,lat,lon,bat` · `SEEN,id,seq` · `SOSOFF,id,seq`. O texto de MSG é o último campo e pode conter vírgulas: o decode pega tudo até o `*`.

- [ ] **Step 1: Escrever os testes**

```cpp
void test_checksum_known() {            // XOR de "PING,A1,7"
  const char* s = "PING,A1,7";
  uint8_t x = 0; for (const char* c = s; *c; ++c) x ^= *c;
  TEST_ASSERT_EQUAL_HEX8(x, protoChecksum(s, strlen(s)));
}
void test_encode_ping_exact() {
  Packet p{}; p.type = PktType::PING; strcpy(p.id, "A1"); p.seq = 7;
  char buf[FRAME_MAX]; size_t n = protoEncode(p, buf, sizeof buf);
  char expect[32]; snprintf(expect, sizeof expect, "$PING,A1,7*%02X\n",
                            protoChecksum("PING,A1,7", 9));
  TEST_ASSERT_EQUAL_STRING(expect, buf); TEST_ASSERT_EQUAL(strlen(expect), n);
}
void test_pos_roundtrip_negative() {    // Review Focus 2
  Packet p{}; p.type = PktType::POS; strcpy(p.id, "B2"); p.seq = 255;
  p.lat = -23.55052; p.lon = -46.63331; p.alt = 760; p.bat = 87;
  char buf[FRAME_MAX]; size_t n = protoEncode(p, buf, sizeof buf);
  Packet q{}; TEST_ASSERT_TRUE(protoDecode(buf, n - 1, q));
  TEST_ASSERT_EQUAL(PktType::POS, q.type); TEST_ASSERT_EQUAL_STRING("B2", q.id);
  TEST_ASSERT_EQUAL_UINT8(255, q.seq);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -23.55052, q.lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -46.63331, q.lon);
  TEST_ASSERT_EQUAL_INT32(760, q.alt); TEST_ASSERT_EQUAL_UINT8(87, q.bat);
}
void test_msg_with_commas_roundtrip()   // "oi, tudo bem, e ai" chega igual
void test_all_types_roundtrip()         // MSG, PING, ACK(ackSeq=12), SOS, SEEN, SOSOFF
void test_decode_rejects_bad_checksum() // troca 1 dígito do CK -> false
void test_decode_rejects_unknown_type() // "$FOO,A1,1*..." com CK válido -> false
void test_decode_rejects_missing_fields() // "$POS,A1,3,-23.5*.." com CK válido -> false
void test_decode_rejects_seq_over_255()   // seq "300" -> false
void test_sanitize_accents_and_specials() { // Review Focus 1
  char out[MSG_MAX_LEN + 1];
  protoSanitize("voc\xC3\xAA a$b*c", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("voc?? a?b?c", out);
}
void test_sanitize_truncates_to_48()     // entrada de 60 'x' -> strlen == 48
void test_encode_msg_never_emits_high_bytes() // texto sanitizado: todo byte de buf < 0x80
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `pio test -e native -f test_protocol`
Expected: FAIL de compilação (`protocol.h` não existe).

- [ ] **Step 3: Implementar `lib/protocol/protocol.cpp`.** Encode com `snprintf` (`%.5f` para lat/lon). Decode: confere o `$`, acha o `*`, compara o CK e separa os campos por vírgula, menos o último campo de MSG. Números com `strtol`/`strtod`, checando que o número consumiu o campo inteiro. O ID tem 1–2 caracteres alfanuméricos.

- [ ] **Step 4: Rodar e ver passar**

Run: `pio test -e native -f test_protocol`
Expected: todos `PASS`.

- [ ] **Step 5: Commit**

```bash
git add lib/protocol/protocol.* test/test_protocol
git commit -m "feat(protocol): adiciona formato de pacotes LoRa com checksum"
```

---

### Task 3: Framer — fluxo de bytes para frames com RSSI

**Files:**
- Create: `lib/protocol/framer.h`, `lib/protocol/framer.cpp`
- Test: `test/test_framer/test_main.cpp`

**Interfaces:**
- Consumes: `FRAME_MAX`.
- Produces:
```cpp
struct Frame { char line[FRAME_MAX]; size_t len; bool hasRssi; int16_t rssi; }; // line sem '\n', terminada em '\0'
class Framer {
 public:
  void feed(uint8_t b, uint32_t nowMs);
  void tick(uint32_t nowMs);   // libera um frame pendente sem RSSI depois de 50 ms
  bool pop(Frame& out);        // fila interna de 4 frames
};
```
Regras: bytes antes de `$` são descartados. No `\n`, o frame fica *pendente*. Se o próximo byte for ≥ 0x80, ele é o RSSI (`rssi = -(255 - b)`) e o frame é emitido. Se vier `$` ou passarem 50 ms, o frame é emitido sem RSSI. Uma linha que passa de `FRAME_MAX - 1` é descartada inteira, até o próximo `$`. Um `$` no meio de uma linha reinicia a linha.

- [ ] **Step 1: Escrever os testes**

```cpp
static void feedStr(Framer& f, const char* s, uint32_t t) { while (*s) f.feed((uint8_t)*s++, t); }
void test_frame_with_rssi() {
  Framer f; feedStr(f, "$PING,A1,7*00\n", 0); f.feed(0xAB, 1);
  Frame fr; TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,7*00", fr.line);
  TEST_ASSERT_TRUE(fr.hasRssi); TEST_ASSERT_EQUAL_INT16(-84, fr.rssi);
}
void test_frame_without_rssi_after_timeout()   // pop false antes de tick(51); depois true, hasRssi false
void test_back_to_back_frames_without_rssi()   // "$A..\n$B..\n" -> o 1º sai quando chega o '$' do 2º
void test_garbage_and_power_on_discarded() {   // Review Focus 3
  Framer f; feedStr(f, "Power On\r\nxx", 0); feedStr(f, "$PING,A1,1*00\n", 0); f.feed(0xC0, 0);
  Frame fr; TEST_ASSERT_TRUE(f.pop(fr)); TEST_ASSERT_EQUAL_STRING("$PING,A1,1*00", fr.line);
  TEST_ASSERT_FALSE(f.pop(fr));
}
void test_overlong_line_dropped_next_ok()      // '$' + 200 'x' + "\n" e depois frame válido -> só o válido sai
void test_truncated_frame_restarts_on_dollar() // "$MSG,A1,3,ol" + "$PING,A1,4*00\n" -> só o PING sai
```

- [ ] **Step 2: Rodar e ver falhar** — `pio test -e native -f test_framer`, com FAIL de compilação.
- [ ] **Step 3: Implementar `framer.cpp`.** Buffer de linha, estado `pending` com `pendingSince`, fila circular de 4 frames (se encher, descarta o mais antigo).
- [ ] **Step 4: Rodar e ver passar** — `pio test -e native -f test_framer`, todos PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(protocol): adiciona framer com leitura do byte de RSSI"`

---

### Task 4: Enlace — ACK, reenvio, deduplicação

**Files:**
- Create: `lib/protocol/link.h`, `lib/protocol/link.cpp`
- Test: `test/test_link/test_main.cpp`

**Interfaces:**
- Consumes: `Packet`, `protoEncode`, `protoDecode`, `protoNeedsAck`, `Frame`.
- Produces:
```cpp
enum class Delivery : uint8_t { Delivered, Failed };
enum class SosState : uint8_t { Idle, Sending, Received, Seen };
struct LinkTiming {
  uint32_t ackTimeoutMs = 4000, jitterMaxMs = 1000;
  uint8_t  msgMaxAttempts = 3, sosFastAttempts = 6;
  uint32_t sosFastMs = 10000, sosSlowMs = 30000, dedupTtlMs = 60000;
};
class LinkSink {   // implementada pela radio task (e por fakes nos testes)
 public:
  virtual void sendFrame(const char* data, size_t len) = 0;
  virtual void onPacket(const Packet& p, const Frame& f) = 0;  // pacote novo do amigo (nunca ACK nem duplicata)
  virtual void onDelivery(uint8_t seq, Delivery d) = 0;          // MSG/PING
  virtual void onSosState(SosState s) = 0;                       // o MEU SOS
  virtual void onPeerSos(bool active) = 0;                       // SOS do AMIGO (alarme)
  virtual ~LinkSink() = default;
};
class Link {
 public:
  Link(LinkSink& sink, const char* myId, uint8_t initialSeq,
       uint32_t (*rand32)(), LinkTiming t = {});
  void onFrame(const Frame& f, uint32_t now);
  void tick(uint32_t now);
  int  sendMsg(const char* text, uint32_t now);   // seq, ou -1 se já houver MSG/PING pendente
  int  sendPing(uint32_t now);
  void sendPos(double lat, double lon, int32_t alt, uint8_t bat, uint32_t now); // sem ACK
  // SOS: Task 5
  uint32_t lastPeerRxMs() const; bool hasPeer() const;
};
```
Regras:
- Frames com o meu próprio ID são ignorados.
- Todo pacote com `protoNeedsAck` recebe ACK imediato (`$ACK,<meuId>,<meuSeq>,<seqDele>`), inclusive quando é duplicata.
- Deduplicação por (id, seq) durante `dedupTtlMs`, em um anel de 16 entradas.
- Um reenvio repete **o mesmo seq**. Cada pacote novo incrementa o seq com wrap em 255.
- Jitter: `rand32() % (jitterMaxMs+1)`, somado a cada timeout.
- ACK tardio (de seq que já falhou ou não está pendente) é ignorado.
- `lastPeerRxMs` é atualizado com qualquer frame válido do amigo, inclusive ACK.

- [ ] **Step 1: Escrever os testes.** Use um `FakeSink` que guarda os frames enviados (decodificados) e os eventos, e um `rand32` que retorna 0. Ponha dois `Link` ("A1" e "B2") ligados por uma função `pump()` que entrega os frames de um ao outro.

```cpp
void test_msg_delivered_with_ack()        // A.sendMsg("oi",0); pump; A recebe onDelivery(seq, Delivered); B onPacket MSG "oi"
void test_msg_retries_same_seq_then_fails() {
  // A envia sem entregar nada a B
  int s = A.sendMsg("oi", 0);
  A.tick(3999);  TEST_ASSERT_EQUAL(1, sinkA.sent.size());
  A.tick(4000);  TEST_ASSERT_EQUAL(2, sinkA.sent.size()); TEST_ASSERT_EQUAL_UINT8(s, sinkA.sent[1].seq);
  A.tick(8000);  TEST_ASSERT_EQUAL(3, sinkA.sent.size());
  A.tick(12000); TEST_ASSERT_EQUAL(Delivery::Failed, sinkA.lastDelivery);
  TEST_ASSERT_EQUAL(3, sinkA.sent.size());  // não manda a 4ª
}
void test_lost_ack_duplicate_acked_not_redelivered() // B recebe o mesmo MSG 2x: 2 ACKs, onPacket 1x
void test_dedup_expires()                 // Review Focus 4: mesmo (id,seq) depois de 60001 ms -> onPacket de novo
void test_own_frames_ignored()            // A.onFrame(frame com id "A1") -> nada
void test_late_ack_ignored()              // ACK depois de Failed -> nenhum onDelivery extra
void test_second_msg_while_pending_rejected() // sendMsg retorna -1
void test_pos_no_ack()                    // B recebe POS: onPacket e nenhum ACK enviado
void test_seq_wraps()                     // initialSeq 255: próximos seqs 255, 0
void test_jitter_applied()                // rand32 retornando 500: o reenvio sai em 4500, não em 4000
```

- [ ] **Step 2: Rodar e ver falhar** — `pio test -e native -f test_link`.
- [ ] **Step 3: Implementar `link.cpp`** (sem SOS ainda).
- [ ] **Step 4: Rodar e ver passar** — `pio test -e native -f test_link`.
- [ ] **Step 5: Commit** — `git commit -m "feat(protocol): adiciona enlace com ACK, reenvio e deduplicação"`

---

### Task 5: SOS em dois níveis

**Files:**
- Modify: `lib/protocol/link.h`, `lib/protocol/link.cpp`
- Test: `test/test_sos/test_main.cpp`

**Interfaces:**
- Produces (no `Link`):
```cpp
void startSos(double lat, double lon, uint8_t bat, uint32_t now);
void updateSosPosition(double lat, double lon, uint8_t bat);
void cancelSos(uint32_t now);      // envia SOSOFF (até ACK), estado -> Idle
void confirmSeen(uint32_t now);    // quem recebe aperta "Vi": envia SEEN (até ACK), onPeerSos(false)
SosState sosState() const;
bool peerSosActive() const;
```
Máquina do remetente:
- `startSos` → `Sending`, enviando na hora. Cada envio tem **seq novo** e a posição atual.
- Intervalo de 10 s nas primeiras 6 tentativas e de 30 s depois delas.
- ACK de qualquer um dos últimos 8 seqs de SOS → `Received` (intervalo de 30 s, continua enviando).
- SEEN recebido → `Seen` e para de enviar.
- `cancelSos` → `Idle` e começa o envio de SOSOFF.
- `onSosState` só é chamado quando o estado muda.

Receptor:
- SOS novo → `onPacket` (para a posição) e, se `peerSosActive` era falso, `onPeerSos(true)`.
- SOSOFF → `onPeerSos(false)`.
- `confirmSeen` → `onPeerSos(false)` e envio de SEEN com a cadência do SOS até receber ACK.
- Um SOS do amigo que chega **depois** do `confirmSeen` (de antes de ele receber o SEEN) recebe ACK mas não reacende o alarme. Um SOS dele que chega depois de um SOSOFF reacende.

- [ ] **Step 1: Escrever os testes**

```cpp
void test_sos_resends_10s_then_30s()     // sem entrega: envios em 0,10k,..,50k (6) e depois em 80k
void test_sos_ack_moves_to_received_keeps_sending() // pump uma vez -> Received; próximo envio só em +30k
void test_sos_new_seq_and_position_each_send()       // updateSosPosition entre envios -> lat nova, seq diferente
void test_seen_stops_sos()               // B.confirmSeen -> A em Seen, sem envios depois; B onPeerSos(false)
void test_seen_retried_until_ack()       // SEEN perdido: B reenvia em +10k
void test_cancel_sends_sosoff_and_stops_peer_alarm() // A.cancelSos -> B onPeerSos(false); A Idle, sem SOS depois
void test_simultaneous_sos() {           // Review Focus 5
  A.startSos(-23.5, -46.6, 80, 0); B.startSos(-23.6, -46.7, 70, 0); pumpAll(0);
  TEST_ASSERT_TRUE(A.peerSosActive()); TEST_ASSERT_TRUE(B.peerSosActive());
  TEST_ASSERT_EQUAL(SosState::Received, A.sosState());
  TEST_ASSERT_EQUAL(SosState::Received, B.sosState());
}
void test_peer_alarm_raised_once()       // 3 SOS seguidos -> onPeerSos(true) só 1x
```

- [ ] **Step 2: Rodar e ver falhar** — `pio test -e native -f test_sos`.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Rodar tudo** — `pio test -e native`, com todas as suítes PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(protocol): adiciona SOS com confirmação de recebido e visto"`

---

### Task 6: Comandos AT (puro)

**Files:**
- Create: `lib/protocol/lora_at.h`, `lib/protocol/lora_at.cpp`
- Test: `test/test_lora_at/test_main.cpp`

**Interfaces:**
- Produces:
```cpp
struct AtSetting { const char* key; const char* value; };   // ex. {"LEVEL","0"}
size_t loraDesiredSettings(const char* channelHex, AtSetting* out, size_t cap);
// ordem: SWITCH 0, MODE 0, LEVEL 0, CHANNEL <hex>, POWE 22, DRSSI 1, PACKET 3, BAUD 3
bool atParseQuery(const char* line, const char* key, char* value, size_t cap);
// "+LEVEL =2" / "+ CHANNEL=41" -> ignora espaços, compara key sem diferenciar maiúsculas, copia o valor
bool atIsOk(const char* line);     // "OK"
bool atIsError(const char* line);  // começa com "ERROR" ou "EEROR" (grafia do manual)
bool atValueEquals(const char* key, const char* have, const char* want); // CHANNEL compara como hex ("47" == "47", "041" == "41"); os demais como inteiro
```

- [ ] **Step 1: Testes:** `test_desired_settings_order_and_values`, `test_parse_query_with_spaces` (`"+LEVEL =2"` → `"2"`, `"+ CHANNEL=41"` → `"41"`), `test_parse_query_wrong_key_false`, `test_ok_and_error` (inclusive `"EEROR=3"`), `test_value_equals_hex_channel`.
- [ ] **Step 2: Rodar e ver falhar** — `pio test -e native -f test_lora_at`.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Rodar e ver passar.**
- [ ] **Step 5: Commit** — `git commit -m "feat(lora): adiciona tabela de configuração AT do DX-LR22"`

---

### Task 7: Driver, task de rádio, settings, estado e console

**Files:**
- Create: `src/lora.h/.cpp`, `src/radio_task.h/.cpp`, `src/storage.h/.cpp`, `src/state.h/.cpp`, `src/debug_console.h/.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: tudo de `lib/protocol`.
- Produces:
```cpp
// storage.h
struct Settings { char id[3]; uint8_t brightness; uint8_t volume; };
void settingsLoad(Settings& s);   // Preferences "cfg"; id padrão = 2 hex maiúsculos do último byte do MAC; brightness 180; volume 60
void settingsSave(const Settings& s);
// state.h
struct SharedState {
  bool loraOk; bool peerKnown; double peerLat, peerLon; int32_t peerAlt; uint8_t peerBat;
  uint32_t peerPosMs, peerRxMs; bool hasRssi; int16_t rssi;
  SosState mySos; bool peerSos;
};
SharedState& state(); void stateLock(); void stateUnlock();   // mutex FreeRTOS
// lora.h
bool loraBegin();                        // pinos, Serial2, sessão AT; retorna false se o módulo não respondeu
void loraPoll(Framer& f, uint32_t now);  // lê os bytes disponíveis
bool loraWrite(const char* d, size_t n); // espera AUX baixo (até 3000 ms, com vTaskDelay(2)); depois UM Serial2.write
// radio_task.h
enum class RadioCmdKind : uint8_t { Msg, Ping, Pos, SosStart, SosCancel, SosUpdate, Seen };
struct RadioCmd { RadioCmdKind kind; char text[MSG_MAX_LEN + 1]; double lat, lon; int32_t alt; uint8_t bat; };
enum class RadioEvtKind : uint8_t { Packet, Delivery, MySos, PeerSos };
struct RadioEvt { RadioEvtKind kind; Packet pkt; bool hasRssi; int16_t rssi; uint8_t seq; Delivery delivery; SosState sos; bool peerSos; };
void radioStart(const Settings& s);  // cria a task "radio", core 0, prioridade 3, stack 6144
bool radioSend(const RadioCmd& c);   // xQueueSend, timeout 0
bool radioNextEvent(RadioEvt& e);    // xQueueReceive, timeout 0 (fila de 16)
// debug_console.h
void consolePoll();   // comandos pela Serial
```

Sessão AT em `loraBegin()`:
1. `pinMode(M0/M1, INPUT_PULLDOWN)`, `pinMode(AUX, INPUT)`, `Serial2.begin(9600, SERIAL_8N1, 40, 39)`.
2. Espera AUX baixo (até 2 s).
3. Envia `+++` sem CRLF e espera `Entry AT` por até 1 s. Se vier `Exit AT`, envia `+++` de novo. Se não vier nada, loga `LOGE` e retorna false; o rádio continua em modo transparente.
4. Para cada `AtSetting`: consulta `AT+<KEY>\r\n` e lê as linhas até `OK`/`ERROR` (timeout de 500 ms). Grava `AT+<KEY><valor>\r\n` só se `!atValueEquals`, para evitar desgaste da flash do módulo.
5. Loga `AT+HELP` inteiro com `LOGD`, para conferir no hardware.
6. Envia `+++` (o módulo sai e reinicia) e espera AUX baixo, até 3 s.

A task de rádio roda em laço: `loraPoll` → `framer.tick` → `pop` → `link.onFrame` → `link.tick` → drena a fila de comandos → `vTaskDelay(pdMS_TO_TICKS(5))`. O `LinkSink` da task atualiza `SharedState` (sob o lock) e publica `RadioEvt`. A seq inicial vem de `esp_random() & 0xFF`, e o `rand32` é `esp_random`.

Comandos do console (uma linha, `\n`):
- `ping`
- `msg <texto>`
- `pos <lat> <lon>`: envia POS falso, para testar antes da etapa do GPS
- `sos <lat> <lon>`
- `cancel`
- `vi`
- `status`: imprime `SharedState`

Cada `RadioEvt` vira uma linha legível, por exemplo `[RX] MSG de B2: oi (rssi -84)`, `[TX] seq 12 ENTREGUE`, `[SOS] meu: RECEBIDO`, `[SOS] AMIGO PEDIU SOCORRO`.

- [ ] **Step 1: Implementar `storage`, `state` e `lora`.** Rode `pio run -e esp32s3` e espere SUCCESS.
- [ ] **Step 2: Implementar `radio_task` e `debug_console` e ligar no `main.cpp`.** O `setup` faz `settingsLoad` → `radioStart`, e o `loop` faz `consolePoll` + `vTaskDelay(10)`. Rode `pio run -e esp32s3` e espere SUCCESS.
- [ ] **Step 3: Rodar as suítes de novo** — `pio test -e native`, todas PASS.
- [ ] **Step 4: Commit** — `git commit -m "feat(lora): adiciona driver DX-LR22, task de rádio e console de teste"`

**Como testar no hardware (quando os aparelhos estiverem montados):**
1. Gravar os dois aparelhos (`pio run -e esp32s3 -t upload`) e abrir `pio device monitor` em cada um.
2. No boot, conferir no log o `AT+HELP` com LEVEL 0, CHANNEL 47, POWE 22 e DRSSI 1. Se aparecer `LoRa sem resposta AT`, revisar a fiação de TX/RX (cruzados) e o VCC do módulo.
3. Em A: `ping`. Esperado: em B, `[RX] PING de A1`; em A, `[TX] ... ENTREGUE`.
4. Em A: `msg oi, tudo bem?`. Esperado: o texto chega igual em B, com RSSI.
5. Desligar B e, em A, `msg teste`. Esperado: 3 tentativas com cerca de 4 s de intervalo e depois `FALHOU`.
6. Em A: `sos -23.55 -46.63`. Esperado: B mostra `AMIGO PEDIU SOCORRO` e A mostra `RECEBIDO`. Em B, `vi`. Esperado: A mostra `VISTO` e para de enviar.
7. Teste de alcance: afastar os aparelhos com `ping` repetido, anotando RSSI e distância.

---

## Próximas etapas (planos separados, um por vez)

2. GPS (inclui o POS automático a cada 30 s) · 3. UI + encoder + OLED + mutex SPI · 4. Mapa · 5. IMU, áudio e CC1101

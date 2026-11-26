# Design — Comunicador de emergência GPS + LoRa (ESP32-S3)

Data: 2026-11-25

## Intenção e porquê

Dois aparelhos portáteis idênticos, guardados **desligados** como kit de
emergência. Numa emergência são ligados e precisam: trocar posição e
mensagens por LoRa com o **maior alcance possível**, permitir que um encontre
o outro (mapa offline + tela "Amigo") e disparar **SOS** de forma confiável.
Bateria importa: gastar o mínimo enquanto ligados, e zero enquanto guardados.

## Objetivos

- SOS confiável: reenviado até ACK, com última posição conhecida, alarme
  sonoro no aparelho que recebe. Prioridade sobre qualquer outra função.
- Alcance máximo do LoRa (menor taxa no ar, 22 dBm, pacotes curtos).
- Pronto para uso poucos segundos após ligar.
- Todas as funções do pedido original (mensagens, GPS, mapa, Amigo, GPX,
  OLED, IMU/queda, áudio, scanner CC1101 RX, menu, NVS).

## Não-objetivos

- Modo de espera ligado / wake-on-radio / deep sleep aguardando mensagens.
- Mesh, mais de 2 aparelhos, criptografia.
- Wi-Fi/Bluetooth.
- Transmissão, replay ou jamming com o CC1101 (somente recepção).
- RadioLib/Meshtastic (o LoRa é UART transparente).

## Anti-objetivos (falha mesmo que "funcione")

- Um SOS perdido ou atrasado porque a UI estava redesenhando o mapa.
- Bateria drenando com o aparelho guardado.
- Os dois aparelhos com configuração de rádio diferente sem o usuário saber.

## Restrições

- ESP32-S3-DevKitC-1 N16R8 (16 MB flash QIO, 8 MB PSRAM OPI).
- TFT 2,0" **ST7789 240x320**, SPI, **sem touch** (`ST7789_DRIVER`,
  `TFT_WIDTH=240`, `TFT_HEIGHT=320`). OLED SSD1306 128x32 I2C.
- **Hardware ainda não montado.** Toda lógica testável sem hardware
  (protocolo, checksum, haversine/bearing, conversão lat/lon→tile, parser de
  configuração) fica em módulos puros testados no host (`pio test -e native`).
  Cada etapa precisa compilar para o ESP32-S3; os testes em hardware entram
  no roteiro de teste da etapa para quando os aparelhos estiverem prontos.
  Limiares (queda, AUX, buffer do módulo) começam com estimativas em
  constantes e são ajustados depois, medindo.
- PlatformIO + Arduino. `board_build.arduino.memory_type = qio_opi`,
  `-DBOARD_HAS_PSRAM`. TFT_eSPI configurado **só** por `build_flags`.
- LLCC68 tem SFs menores que SX1262 → alcance limitado pelo rádio; antena
  e altura importam mais que o firmware.
- Sem magnetômetro: direção vem do `gps.course`; parado, mantém o último
  rumo (a seta pode ficar errada se o usuário girar parado — limitação
  aceita, mostrar indicação "rumo antigo" na tela).
- Chave física corta a bateria (hardware). Firmware não implementa "desligar".

## Pinagem (aprovada)

| Função | GPIO |
|---|---|
| SPI SCK / MOSI / MISO | 12 / 11 / 13 (FSPI) |
| TFT CS / DC / BL | 10 / 9 / 14 (BL com PWM) |
| TFT RST | ligado ao EN (`TFT_RST=-1`) |
| SD CS | 21 |
| CC1101 CS | 47 (GDO0 não usado; RSSI lido via SPI) |
| I2C SDA / SCL | 8 / 18 (OLED 0x3C, MPU6050 0x68) |
| MPU6050 INT | 7 |
| GPS RX / TX (lado ESP) | 44 / 43 — `Serial1`, 9600 |
| LoRa RX / TX / M0 / M1 / AUX (lado ESP) | 40 / 39 / 41 / 42 / 2 — `Serial2` |
| I2S BCLK / LRC / DIN | 15 / 16 / 17 |
| Encoder A / B / SW | 4 / 5 / 6 |
| Bateria (ADC1) | 1, divisor 100k/100k, Li-ion 1S |

Evitados: 0/3/45/46 (strapping), 19/20 (USB nativo, logs), 26–37 (flash/PSRAM
OPI), 38/48 (LED RGB da placa). MAX98357A SD ligado a VIN. Logs pela USB CDC
(`ARDUINO_USB_CDC_ON_BOOT=1`); ROM do boot escreve lixo no GPS em 43 —
inofensivo.

## Abordagens consideradas

- **A (escolhida):** duas tasks FreeRTOS — *rádio* (core 0: GPS, LoRa,
  protocolo, ACK/reenvio de SOS, POS a cada 30 s, GPX) e *UI* (core 1:
  encoder, TFT, OLED, mapa, áudio, scanner) — mais uma task pequena do IMU
  acordada pelo INT. Etapa 4: o GPX saiu do rádio para uma task *sd*
  (core 0, prioridade 1). O rádio só decide o ponto e o enfileira; a task
  de SD grava o arquivo e a trilha em PSRAM, e o rádio nunca espera o SPI.
  Ver `2026-11-25-etapa4-mapa-gpx-design.md`. Comunicação por filas (pacotes recebidos, pedidos de
  envio, eventos) e struct de estado compartilhada protegida por mutex.
  Escolhida porque decodificar JPG (~100 ms/tile) nunca atrasa SOS/ACK.
- **B:** loop único com `millis()`. Rejeitada: JPG/SD bloqueiam o loop,
  contraria requisito.
- **C:** uma task por módulo. Rejeitada: sincronização e RAM sem ganho.

**Configuração do DX-LR22:** feita pelo firmware a cada boot (modo de
configuração via M0/M1, aguardando AUX), com canal/taxa/potência vindos de
constantes em `config.h`. Garante os dois aparelhos idênticos mesmo se o
módulo for trocado. Alternativa rejeitada: configurar uma vez pela
ferramenta do fabricante.

## Design

### Módulos (um par .h/.cpp cada, em `src/`)

`config.h` (pinos, constantes, macro de log), `display`, `oled`, `gps`,
`lora`, `protocol` (montagem/parse/checksum — separado do driver para ser
testável no host), `map`, `input`, `audio`, `imu`, `cc1101`, `storage`
(SD + GPX + Preferences), `ui` (menu e telas), `state` (estado compartilhado
+ mutex), `main.cpp`.

### Log de debug

`LOGD/LOGI/LOGE(fmt, ...)` em `config.h`, ativados por `-DDEBUG_LOG=1`
no `platformio.ini`; com 0 viram no-op.

### SPI compartilhado

TFT, SD e CC1101 no mesmo FSPI. Um mutex global (`spiLock()/spiUnlock()`)
envolve todo acesso; TFT_eSPI com `SUPPORT_TRANSACTIONS`. Etapa 5c: em vez da
lib SmartRC (que chama `SPI.begin()`/`SPI.end()` a cada registrador), o CC1101
usa driver mínimo próprio somente-RX (`src/cc1101`) com `SPI.beginTransaction`
sob `spiLock()` e timeout em MISO. CC1101 colocado em sleep (`SIDLE`+`SPWD`)
fora da tela do scanner, com tela apagada ou durante overlay de SOS. Ver
`2026-11-25-etapa5c-scanner-design.md`.

### Protocolo LoRa (texto, curto)

Formato: `$<TIPO>,<ID>,<SEQ>,<campos...>*<CK>\n`

- `ID`: 1–2 chars configuráveis (padrão derivado do MAC).
- `SEQ`: 0–255, incrementa por envio.
- `CK`: XOR de todos os bytes entre `$` e `*`, em 2 hex (estilo NMEA).
- Tipos:
  - `MSG,<id>,<seq>,<texto>` — texto limitado a **48 caracteres**.
  - `POS,<id>,<seq>,<lat>,<lon>,<alt>,<bat%>` — lat/lon 5 casas decimais
    (~1 m), alt em m inteiro.
  - `PING,<id>,<seq>` / `ACK,<id>,<seq>,<seqConfirmado>`.
  - `SOS,<id>,<seq>,<lat>,<lon>,<bat%>` — ver "SOS em dois níveis".
  - `SEEN,<id>,<seq>,<seqDoSos>` — confirmação humana do SOS (o usuário apertou "Vi"); só vale para um SOS do episódio atual.
  - `SOSOFF,<id>,<seq>` — remetente cancelou o SOS (para o alarme do outro).
- MSG, PING, SOS, SEEN e SOSOFF pedem ACK. MSG: até 3 tentativas, depois
  marcada "não entregue". Duplicatas (mesmo ID+SEQ recente) são confirmadas
  mas ignoradas.

#### SOS em dois níveis (decisão do usuário)

1. Remetente envia SOS a cada 10 s (cada pacote com posição atual).
2. ACK automático do outro aparelho → remetente mostra **"RECEBIDO"** e
   passa a reenviar a cada 30 s (posição atualizada), sem parar.
3. Receptor toca alarme até o usuário apertar **"Vi"** → envia `SEEN`
   (com reenvio até ACK). Remetente mostra **"VISTO"** e para o SOS.
4. Remetente pode cancelar a qualquer momento → envia `SOSOFF`.
5. Após "VISTO", o POS normal a cada 30 s continua informando a posição.
O receptor atualiza a posição do amigo tanto por POS quanto por SOS.

Tempos (revisados após a revisão de código, considerando o airtime em SF11):
timeout de ACK de MSG/PING 7 s; jitter de SOS/SEEN/SOSOFF de 0–5 s e atraso
inicial aleatório de 0–2 s, para que dois SOS simultâneos não colidam sempre;
depois de "Vi", SOS em trânsito não reacende o alarme por 40 s. Um frame com o
meu próprio ID indica outro aparelho com o mesmo ID (o módulo não recebe o
próprio TX) e gera um aviso. O ID padrão é um hash do MAC inteiro (base 36).
- Pacotes próprios (mesmo ID) ignorados. Checksum inválido → descartado.
- Envio respeita AUX (só escreve com AUX alto) e espaçamento mínimo entre
  pacotes para não estourar o buffer do módulo.
- RSSI: se o módulo suportar anexar RSSI ao pacote, habilitar e usar;
  senão, mostrar "—" e usar só "tempo desde a última mensagem".

### Rádio — parâmetros

Menor taxa no ar suportada, 22 dBm, modo transparente. **Canal fixo de
fábrica** (decisão do usuário): constante `LORA_CHANNEL` em `config.h`
(915 MHz, faixa permitida no Brasil); não aparece no menu nem na NVS, só
muda recompilando. Isso evita os dois aparelhos ficarem em canais
diferentes. O OLED mostra o canal em uso. O builder deve
confirmar no datasheet do DX-LR22 o formato de configuração (registradores
tipo `C0 ...` ou comandos AT) e os valores de taxa/potência.

### DX-LR22 — o que o manual diz (2025-11-25)

Fontes: manual técnico do DX-LR22-900T22D v2.0 e guia UART do irmão
DX-LR21-900T22S (o manual do LR22 remete a um guia UART com a mesma
numeração de seções; assumimos o mesmo conjunto AT e confirmamos no
hardware).

- Configuração por **comandos AT**, terminados em `\r\n`. Entrar e sair
  do modo AT com `+++` (resposta `Entry AT` / `Exit AT`; ao sair o módulo
  reinicia). Sucesso termina em `OK`, erro em `ERROR=<n>`; consultas
  respondem `+CHAVE=valor` (às vezes com espaços, ex. `+LEVEL =2`).
- **M0/M1:** com `AT+SWITCH0` (padrão) são **saídas do módulo**, em nível
  baixo. O ESP nunca deve acioná-los: GPIO 41/42 ficam como
  `INPUT_PULLDOWN` (com SWITCH=1 os pull-downs também mantêm o modo de
  alta eficiência). O firmware garante `AT+SWITCH0`.
- **AUX:** nível **alto = ocupado** (TX/RX/troca de modo), baixo = livre.
  Isso é o contrário dos módulos Ebyte.
- **Alcance:** `AT+LEVEL0` = SF11, BW 125 kHz, CR 4/8, **336 bit/s**
  (~8 km em visada direta). Um pacote de 40 bytes leva cerca de 1 s no ar.
- **Canal:** `AT+CHANNEL<hex>` = 850,15 MHz + n × 1 MHz. Usar **0x47
  (921,15 MHz)**, dentro de 915–928 MHz e fora do padrão de fábrica 0x41,
  que é compartilhado por todo mundo (decisão do builder).
- **RSSI:** `AT+DRSSI1` anexa **1 byte** após cada pacote recebido;
  dBm = −(255 − byte). Esse byte é sempre ≥ 0x80, então o protocolo
  precisa ser **ASCII puro (< 0x80)**.
- `AT+POWE22`, `AT+MODE0` (transparente), `AT+PACKET3` (230 B),
  `AT+BAUD3` (9600). As alterações só valem depois de reiniciar.
- **Alimentação (hardware):** 3,3–5,5 V; **≥ 5 V garante os 22 dBm**.
  Com Li-ion 1S é preciso um boost de 5 V para o módulo, senão o alcance
  cai. Prever capacitores de 100 µF + 0,1 µF junto ao VCC.

### GPS

TinyGPSPlus em `Serial1` 9600. Estado: lat, lon, alt, velocidade,
satélites, fix, hora UTC → exibida em UTC-3. Rumo: `gps.course` quando
velocidade > 2 km/h; senão mantém último rumo com flag "antigo".
POS automático a cada 30 s somente com fix válido.

### UI

- Menu raiz: Mapa, Mensagens, Amigo, GPS, Scanner, Configurações.
- Encoder por interrupção (A/B com tabela de estados, debounce por tempo;
  SW com debounce, clique curto / longo).
- **Clique longo (≥2 s) em qualquer tela → contagem de SOS** (10 s,
  cancelável com clique). Também disparada por detecção de queda.
- Mensagens: lista recebidas/enviadas, mensagens rápidas pré-definidas
  ("Estou bem", "Preciso de ajuda", "Onde você está?", "Venha até mim",
  "Ok", "Ferido"), teclado na tela (caracteres em fila, encoder seleciona).
- Amigo: distância (haversine), seta = bearing até o amigo − meu rumo,
  RSSI, tempo desde a última mensagem, idade da última posição.
- GPS: dados crus + estado do fix.
- Scanner: varre faixas de 433/868/915 MHz e desenha barras de RSSI.
- Configurações: ID, brilho, volume (salvos na NVS ao sair). Canal LoRa
  só exibido, não editável.
- TFT apaga após 30 s sem interação; acende por encoder, por movimento
  (INT do MPU) ou por mensagem/SOS recebido. OLED sempre ligado.

### OLED (128x32)

Hora local, bateria %, satélites, barra de sinal LoRa (RSSI ou idade do
último pacote) e ícone de mensagem nova.

### Mapa

Tiles `/tiles/{z}/{x}/{y}.jpg` (256x256, slippy map OSM, JPEG baseline;
preparados por `tools/prepare_tiles.py`). Centro na minha posição; desenha
os tiles que cobrem o viewport 320x240 em paisagem (até 3x2 tiles). A trilha
da sessão atual é desenhada como linha sobre o mapa.
Cache LRU em PSRAM de tiles decodificados (RGB565, 128 KB cada; ~24 tiles
= 3 MB). Marca minha posição e a do amigo (se fora da tela, seta na
borda). Zoom pelo encoder, limitado aos níveis que existirem no SD.
Sem fix: centra na última posição salva ou no amigo. Tile ausente: fundo
cinza com "sem mapa".

### GPX

Arquivo por sessão `/tracks/AAAAMMDD_HHMMSS.gpx` (nome pela hora do GPS ao
primeiro fix). Ponto a cada 10 s ou 20 m, o que vier primeiro. Flush a cada
ponto e fechamento válido do XML (reescrever o rodapé) para sobreviver a
corte de energia.

### IMU

MPU6050 com interrupção de movimento no GPIO 7 → acorda tela. Queda:
pico ≥ 3 g seguido de assentamento (~1,5 s) e imobilidade por 30 s → contagem
de SOS cancelável (sempre ativa, sem opção de desligar). Acelerômetro em ±8 g
com giroscópio em standby; barramento I2C protegido por mutex compartilhado com
o OLED. Limiares em constantes ajustáveis. Ver
`2026-11-25-etapa5b-imu-design.md`.

### Áudio

Tons gerados via I2S (sem arquivos): bipe de clique, bipe de mensagem
recebida, alarme de SOS (alternado, contínuo até o usuário confirmar).
Volume da NVS. Etapa 5a: também bipes da contagem de SOS e tons de
"recebido"/"visto" do meu SOS; **todos os sons de SOS ignoram o volume**
(tocam alto mesmo em 0). Task `audio` dedicada (core 1). Ver
`2026-11-25-etapa5a-audio-design.md`.

### Bateria

ADC1 no GPIO 1 com `analogReadMilliVolts`, × 2, média móvel; curva Li-ion
simples para %. Aviso abaixo de 15%.

### Persistência (Preferences / NVS)

ID, brilho, volume, último rumo, última posição conhecida (para o
mapa sem fix).

## Ordem de entrega e testes

Cada etapa termina com explicação de como testar e **um commit semântico**
(`feat:`, `fix:`, `docs:`, `chore:` ...):

0. `platformio.ini` + estrutura + `config.h`/log.
1. LoRa: driver, configuração por M0/M1, protocolo, ACK/reenvio
   (teste: protocolo no host com `pio test -e native`; dois aparelhos
   trocando PING/MSG pelo monitor serial).
2. GPS.
3. UI + encoder + OLED.
4. Mapa.
5. IMU, áudio, CC1101.

## Deixado ao builder

Fontes e layout exato das telas, ícones, faixas e passos exatos do
scanner, tamanho final do cache de tiles, valores de debounce, estrutura
interna das filas.

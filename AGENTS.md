# AGENTS.md — Comunicador de Emergência GPS + Mapa Offline + LoRa (ESP32-S3)

Instruções e contexto do repositório para agentes de IA.

## Estado Atual do Projeto

Todas as etapas planejadas (**0 a 5c**) estão implementadas e mergeadas em `main`:
- **0–1:** Base PlatformIO, LoRa DX-LR22 (AT no boot), protocolo ASCII, ACK/reenvio e SOS em 2 níveis.
- **2:** GPS NEO-6M (UTC-3, rumo com flag "antigo"), Haversine/bearing, bateria ADC1 Li-ion.
- **3:** UI no TFT ST7789 240×320, encoder rotativo, OLED SSD1306 128×32, NVS, overlays de SOS.
- **4:** Mapa offline (`/tiles/{z}/{x}/{y}.jpg` JPEG baseline) com cache LRU em PSRAM, trilha GPX em task dedicada, sonda de remoção do SD e script `tools/prepare_tiles.py`.
- **5a:** Áudio I2S (MAX98357A) em task dedicada com sequenciador de tons e prioridade.
- **5b:** IMU 9DoF (ICM-20948 `0x68/0x69`, WoM no GPIO 7, detecção de queda e bússola AK09916 com calibração hard/soft-iron e declinação na NVS) + Barômetro BME280 (`0x76/0x77`, altitude barométrica calibrada pelo GPS, temperatura, umidade, pressão e tendência 3 h) na task `imu` sob mutex I2C + scan I2C no boot.
- **5c:** Scanner CC1101 somente-RX em driver mínimo próprio no SPI compartilhado.

> **Hardware ainda não montado fisicamente.** Toda lógica pura fica em `lib/` (testada no host via `pio test -e native`) e o firmware inteiro precisa compilar limpo via `pio run -e esp32s3`. Limiares físicos (IMU, RF, debounce) estão em constantes ajustáveis para calibração na primeira montagem.

## Comandos Essenciais

```bash
pio test -e native                                  # Suíte completa no host (269 testes Unity, 25 suítes)
pio test -e native -f test_<nome>                   # Rodar uma suíte específica
pio run -e esp32s3                                  # Compilar firmware ESP32-S3 N16R8
pio run -e esp32s3 -t upload                        # Gravar via USB CDC
python3 -m pytest tools/test_prepare_tiles.py -q    # Testes do script de preparação de tiles
```

## Arquitetura

- **`lib/` (C++17 puro, SEM Arduino):** `protocol`, `geo`, `power`, `ui_logic`, `map`, `track`, `audio`, `imu` (`FallDetector` + `compass`), `env` (`BaroAltimeter` + `PressureHistory`), `scanner`. Deve sempre compilar em `[env:native]` com `-std=gnu++17 -Wall`.
- **`src/` (ESP32-S3 / Arduino / FreeRTOS):** drivers, barramentos, estado compartilhado e 5 tasks:
  - Core 0: `radio` (prio 2: LoRa, GPS, protocolo, SOS, bateria), `sd` (prio 1: GPX e trilha em PSRAM).
  - Core 1: `audio` (prio 3: síntese I2S), `ui` (prio 2: encoder, TFT, OLED, mapa, scanner), `imu` (prio 1: ICM-20948 acordado por GPIO 7 + bússola + BME280 a cada 5 s).
- **Estado compartilhado:** `src/state.{h,cpp}` protegido por `stateLock()`/`stateUnlock()`.
- **Pinagem e constantes:** [src/config.h](src/config.h).

## Regras Críticas (Não Quebrar)

1. **Prioridade de SOS:** nenhum redesenho de mapa, acesso ao SD ou varredura do scanner pode bloquear/atrasar SOS/ACK.
2. **Volume no SOS:** `PeerAlarm`, `SosSeen`, `SosReceived`, `CountdownFinal` e `CountdownTick` ignoram o volume do usuário e tocam sempre alto (mesmo com volume `0`).
3. **Queda sempre ativa:** detecção de queda (pico ≥ 3 g → 1,5 s assentamento → 30 s imobilidade) não tem botão de desligar em Configurações/NVS.
4. **LoRa DX-LR22:**
   - Canal `LORA_CHANNEL_HEX` (`"47"` = 921,15 MHz) é **fixo de fábrica** em `src/config.h` (não editável no menu/NVS).
   - `M0`/`M1` (GPIOs 41/42) ficam sempre em `INPUT_PULLDOWN` (são saídas do módulo com `AT+SWITCH0`).
   - `AUX` (GPIO 2) **alto = ocupado** (inverso de Ebyte).
   - Protocolo é **estritamente ASCII (< 0x80)** porque `AT+DRSSI1` anexa 1 byte de RSSI ≥ 0x80.
5. **CC1101 estritamente somente-RX:** nunca adicionar `STX` ou transmissão/replay/jamming. Não reintroduzir `SmartRC-CC1101-Driver-Lib` (ela chama `SPI.begin()`/`SPI.end()` a cada registrador). O módulo deve entrar em `SIDLE` + `SPWD` fora da tela Scanner, com TFT apagado ou com overlay de SOS.
6. **Barramentos compartilhados:**
   - **FSPI** (TFT, SD, CC1101): todo acesso sob `spiLock()`/`spiUnlock()` (`src/spi_bus`).
   - **I2C** (OLED `0x3C`, ICM-20948 `0x68/0x69`, BME280 `0x76/0x77`): inicializado uma vez em `i2cBusInit()` (com scan no boot via `i2cBusScan()`); todo acesso sob `i2cLock()`/`i2cUnlock()` (`src/i2c_bus`). Sensor ausente é tolerado.
7. **TFT_eSPI:** configurado **apenas** por `build_flags` em `platformio.ini` (não editar `User_Setup.h`).
8. **Mapas offline:** apenas JPEG **baseline** 256×256 em `/tiles/{z}/{x}/{y}.jpg` (PNG e JPEG progressivo falham no `TJpg_Decoder`).
9. **Commits:** mensagens semânticas em português (`feat:`, `fix:`, `docs:`, `chore:`), **sem** `Co-Authored-By`.

## Documentação de Referência

- [README.md](README.md) — guia completo de hardware, instalação, uso, console serial e roteiro de bancada.
- [docs/mapas-offline.md](docs/mapas-offline.md) — preparação de tiles no microSD.
- `docs/superpowers/specs/` — especificações detalhadas de cada etapa (`dispositivo-gps-lora`, `etapa3-ui`, `etapa4-mapa-gpx`, `etapa5a-audio`, `etapa5b-imu`, `etapa5c-scanner`).

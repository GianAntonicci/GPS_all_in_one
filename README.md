# Comunicador de Emergência GPS + Mapa Offline + LoRa (ESP32-S3)

Firmware completo em **PlatformIO (Arduino / FreeRTOS)** para um par de dispositivos portáteis idênticos ("tudo em um") projetados como **kit de emergência**: ficam guardados desligados (consumo zero via chave física na bateria) e, quando ligados, trocam posição, mensagens curtas e alertas de **SOS** via LoRa com alcance máximo, além de oferecer mapa offline, bússola/seta até o amigo, gravação de trilha GPX, detecção de queda e scanner RF somente-RX.

---

## Funcionalidades

- **Comunicação LoRa de longo alcance (DX-LR22 / LLCC68):**
  - Configurado automaticamente pelo firmware a cada boot (`AT+LEVEL0` — SF11, BW 125 kHz, CR 4/8, 22 dBm, canal fixo de fábrica `0x47` = **921,15 MHz**, RSSI anexado).
  - Protocolo ASCII compacto com checksum XOR (`$TIPO,ID,SEQ,...*CK\n`), deduplicação, ACK e reenvio automático.
  - Posição automática (`POS`) a cada 30 s quando há fix GPS.
- **SOS em dois níveis (prioridade máxima):**
  - Disparado segurando o encoder por **≥ 2 s** em qualquer tela ou automaticamente por **detecção de queda** (contagem cancelável de 10 s).
  - Reenviado a cada 10 s até ACK automático (**RECEBIDO**) e depois a cada 30 s com posição atualizada até o outro usuário confirmar **"Vi"** (**VISTO**), ou até ser cancelado (`SOSOFF`).
- **Interface e Navegação (TFT 2,0" ST7789 + OLED 128×32 + Encoder):**
  - **Amigo:** distância (Haversine), seta direcional relativa ao seu rumo (usa `gps.course` em movimento ≥ 1,5 km/h e bússola calibrada parado/sem fix, sem aviso de "rumo antigo"; mantém fallback com "rumo antigo" se a bússola não estiver calibrada), RSSI e idade do último contato.
  - **Mensagens:** 6 mensagens rápidas pré-definidas, teclado na tela pelo encoder e histórico com status de entrega (pendente / entregue / falhou + reenvio).
  - **Mapa offline:** tiles 256×256 do cartão microSD (`/tiles/{z}/{x}/{y}.jpg`) com cache LRU em PSRAM (~24 tiles), sua posição (seta orientada pelo rumo unificado), posição do amigo (ou seta na borda se fora da tela), linha da trilha da sessão e zoom pelo encoder.
  - **GPS:** coordenadas, altitude barométrica calibrada pelo GPS (com fallback para altitude GPS), velocidade, satélites, HDOP, temperatura, umidade, pressão com seta de tendência (`↑ / → / ↓`) e alerta de queda rápida (`> 2 hPa` em 3 h), hora local (UTC-3) e estado da gravação GPX.
  - **Scanner CC1101 (somente RX):** espectro de 60 barras + peak-hold nas faixas **433**, **868** e **915 MHz**; entra em sleep (`SIDLE` + `SPWD`) fora da tela.
  - **Configurações:** ID do aparelho (2 caracteres), brilho do TFT, volume, declinação magnética (padrão `-21,5°` para São Paulo) e modo **Calibrar bússola** (girar em 8 por ~20 s), salvos na NVS; canal LoRa exibido apenas para leitura.
  - **OLED sempre ligado:** hora local, bateria %, satélites, barra de sinal/idade do amigo, ícone de mensagem nova e avisos de SOS.
  - **Economia de energia:** TFT apaga após 30 s sem interação e acorda por encoder, movimento (IMU) ou mensagem/SOS recebido.
- **Trilha GPX (`/tracks/AAAAMMDD_HHMMSS.gpx`):** grava ponto a cada 10 s ou 20 m com flush e fechamento válido de XML a cada ponto (resiste a corte abrupto de energia).
- **Áudio I2S (MAX98357A):** tons gerados em software com rampa anti-estalo; **todos os sons de SOS ignoram o volume** e tocam alto mesmo com volume em 0.
- **IMU 9DoF (ICM-20948) + Barômetro (BME280):** autodetecção I2C com scan no boot (cada sensor ausente é tolerado e exibe `"--"`). O ICM-20948 acorda o TFT por movimento (WoM no GPIO 7), mantém detecção de queda **sempre ativa** (pico ≥ 3 g → 1,5 s de assentamento → 30 s de imobilidade → contagem de SOS) e fornece bússola com compensação de inclinação (AK09916). O BME280 mede temperatura, umidade, pressão (tendência de 3 h) e altitude barométrica a cada 5 s.
- **Monitoramento de bateria:** leitura via ADC1 (divisor 100k/100k), média móvel, curva Li-ion 1S e aviso abaixo de 15%.

---

## Hardware Necessário

| Componente | Modelo / Especificação |
|---|---|
| MCU | **ESP32-S3-DevKitC-1 N16R8** (16 MB Flash QIO, 8 MB PSRAM OPI) |
| Display principal | TFT 2,0" **ST7789** 240×320 SPI (sem touch) |
| Display de status | OLED **SSD1306** 128×32 I2C (`0x3C`) |
| Rádio LoRa | **DX-LR22-900T22D** (UART transparente, 915 MHz, 22 dBm) |
| GPS | **NEO-6M** (UART 9600 bps) |
| IMU / Bússola | **ICM-20948** 9DoF I2C (`0x68` ou `0x69`, autodetectado) com pino INT |
| Barômetro / Clima | **BME280** I2C (`0x76` ou `0x77`, autodetectado) |
| Áudio | Amplificador I2S **MAX98357A** + alto-falante (pino `SD` ligado a `VIN`) |
| Scanner RF | **CC1101** SPI (somente recepção; `GDO0` não utilizado) |
| Armazenamento | Leitor **microSD** SPI (cartão formatado em FAT32) |
| Entrada | **Encoder rotativo** com botão (A / B / SW) |
| Alimentação | Bateria Li-ion 1S + **chave física** de corte + **boost 5 V** para o DX-LR22 + divisor resistivo **100k/100k** no ADC |

> **Atenção na alimentação do LoRa:** o DX-LR22 aceita 3,3–5,5 V, mas **≥ 5 V é necessário para atingir os 22 dBm** reais. Com bateria Li-ion 1S, use um regulador boost de 5 V e capacitores de **100 µF + 0,1 µF** junto ao VCC do módulo.

### Pinagem (ESP32-S3)

Definida em [src/config.h](src/config.h):

| Função | GPIO | Observações |
|---|---|---|
| SPI `SCK` / `MOSI` / `MISO` | `12` / `11` / `13` | Barramento FSPI compartilhado (TFT, SD, CC1101) |
| TFT `CS` / `DC` / `BL` | `10` / `9` / `14` | `TFT_RST` ligado ao `EN` (`-1`); `BL` com PWM |
| microSD `CS` | `21` | Compartilha FSPI sob mutex `spiLock()` |
| CC1101 `CS` | `47` | Compartilha FSPI; somente RX (`GDO0` não usado) |
| I2C `SDA` / `SCL` | `8` / `18` | Compartilhado (OLED `0x3C`, ICM-20948 `0x68/0x69`, BME280 `0x76/0x77`) sob `i2cLock()` |
| ICM-20948 `INT` | `7` | Interrupção Wake-on-Motion (ativo alto, latched) |
| GPS `RX` / `TX` (lado ESP) | `44` / `43` | `Serial1`, 9600 bps |
| LoRa `RX` / `TX` (lado ESP) | `40` / `39` | `Serial2`, 9600 bps |
| LoRa `M0` / `M1` / `AUX` | `41` / `42` / `2` | `M0`/`M1` sempre `INPUT_PULLDOWN`; `AUX` alto = ocupado |
| I2S `BCLK` / `LRC` / `DIN` | `15` / `16` / `17` | 16 kHz, 16-bit estéreo (MAX98357A mistura L+R) |
| Encoder `A` / `B` / `SW` | `4` / `5` / `6` | Interrupção + tabela de quadratura + debounce |
| Bateria (`ADC1`) | `1` | Divisor 100k/100k (Li-ion 1S) |

*Pinos evitados propositalmente:* `0, 3, 45, 46` (strapping), `19, 20` (USB CDC / logs), `26–37` (Flash/PSRAM OPI), `38, 48` (LED RGB).

---

## Arquitetura do Firmware

O firmware separa **lógica pura testável no host** (`lib/`, sem dependência de Arduino) do **código embarcado e drivers** (`src/`):

```text
├── platformio.ini          # Ambientes esp32s3 (firmware) e native (testes Unity)
├── src/                    # Tasks FreeRTOS, drivers de hardware e telas
│   ├── main.cpp            # Inicialização de state, spi_bus, i2c_bus, NVS e tasks
│   ├── config.h            # Pinagem, constantes e macros LOGD/LOGI/LOGW/LOGE
│   ├── radio_task.*        # Core 0: GPS, LoRa, protocolo, SOS, POS e bateria
│   ├── sd_task.*           # Core 0: gravação GPX no SD e trilha em PSRAM
│   ├── ui.*, ui_screens.*  # Core 1: encoder, TFT, OLED, mapa, scanner e overlays
│   ├── audio.*             # Core 1: síntese I2S e fila de padrões sonoros
│   ├── imu.*, bme.*        # Core 1: ICM-20948 (WoM, queda, bússola) e BME280 (a cada 5 s)
│   ├── cc1101.*            # Driver mínimo somente-RX do CC1101 no SPI compartilhado
│   ├── spi_bus.*, i2c_bus.*# Mutexes globais dos barramentos compartilhados + scan I2C no boot
│   └── debug_console.*     # Console serial interativo (USB CDC 115200)
├── lib/                    # Módulos puros (C++17, sem Arduino)
│   ├── protocol/           # protocol (incl. baroAlt opcional no POS), framer, lora_at, lora_link
│   ├── geo/                # Haversine, bearing, conversão UTC-3, rumo
│   ├── power/              # Curva de bateria Li-ion
│   ├── ui_logic/           # Navegação, teclado, histórico, overlays de SOS, OLED
│   ├── map/                # Projeção slippy map, seta de borda, cache LRU
│   ├── track/              # Política de pontos, buffer de trilha, GPX, sonda SD
│   ├── audio/              # ToneSequencer (prioridades/volume) e AudioCues
│   ├── imu/                # FallDetector e bússola (tilt compensation, calibração, fonte de rumo)
│   ├── env/                # BaroAltimeter (calibração GPS + EMA) e PressureHistory (3 h)
│   └── scanner/            # Frequência 26 MHz, RSSI dBm e ScannerModel
├── test/                   # 25 suítes Unity (269 testes rodando no PC)
├── tools/                  # Script Python para converter tiles de mapa (JPEG baseline)
└── docs/                   # Guia de mapas offline, specs de design e planos
```

### Tasks FreeRTOS

| Task | Core | Prioridade | Responsabilidade |
|---|---|---|---|
| `radio` | 0 | 2 | LoRa (AT/TX/RX), protocolo, SOS/ACK, GPS, bateria |
| `sd` | 0 | 1 | Gravação de GPX no microSD e buffer da trilha em PSRAM |
| `audio` | 1 | 3 | Síntese de tons I2S (acima da UI para nunca picotar) |
| `ui` | 1 | 2 | Encoder, telas TFT, OLED, mapa e varredura incremental do scanner |
| `imu` | 1 | 1 | ICM-20948 (WoM via GPIO 7, queda a 50 Hz, bússola com TFT aceso) e BME280 a cada 5 s |

---

## Como Instalar e Compilar

### 1. Pré-requisitos

- **PlatformIO** ([PlatformIO Core CLI](https://platformio.org/install/cli) ou extensão PlatformIO no VS Code)
- **Git**
- **Python 3** (opcional — apenas para preparar os tiles de mapa offline)

### 2. Clonar o repositório

```bash
git clone https://github.com/GianAntonicci/GPS_all_in_one.git
cd GPS_all_in_one
```

### 3. Rodar os testes unitários no PC (não precisa do hardware)

```bash
pio test -e native
```

Executa todas as 25 suítes (`269` testes) validando protocolo LoRa, SOS, GPS, bateria, UI, mapa, GPX, áudio, detector de queda, bússola, barômetro e scanner.

### 4. Compilar o firmware para o ESP32-S3

```bash
pio run -e esp32s3
```

### 5. Gravar nos dois aparelhos e abrir o monitor serial

Conecte cada ESP32-S3 pela porta **USB nativa** (CDC) e execute:

```bash
pio run -e esp32s3 -t upload
pio device monitor -b 115200
```

> **Dicas de build (`platformio.ini`):**
> - `LORA_CHANNEL_HEX` em [src/config.h](src/config.h) define o canal LoRa fixo de fábrica (`"47"` = 921,15 MHz). **Os dois aparelhos precisam ser gravados com o mesmo valor.**
> - Para desativar logs de debug em produção, troque `-DDEBUG_LOG=1` por `-DDEBUG_LOG=0` em [platformio.ini](platformio.ini).
> - Para isolar o cartão SD durante testes de bancada, adicione `-DENABLE_SD=0` em `build_flags`.

---

## Preparando Mapas Offline (Cartão microSD)

O aparelho lê tiles **JPEG baseline** `256×256` em `/tiles/{z}/{x}/{y}.jpg` (PNG e JPEG progressivo aparecem como `"sem mapa"`).

1. Baixe a região desejada no formato **"OSMTracker tile storage"** (ex.: usando o MOBAC com uma fonte que permita uso offline, zooms recomendados `10` a `16`).
2. Converta e valide com o script incluso:

```bash
pip install -r tools/requirements.txt
python3 tools/prepare_tiles.py /caminho/do/atlas /caminho/saida
```

3. Formate o cartão microSD em **FAT32** e copie a pasta `/caminho/saida/tiles` para a **raiz** do cartão (`/tiles/{z}/{x}/{y}.jpg`).
4. As trilhas gravadas pelo aparelho ficam automaticamente em `/tracks/AAAAMMDD_HHMMSS.gpx`.

Consulte o guia completo em [docs/mapas-offline.md](docs/mapas-offline.md).

---

## Como Utilizar o Aparelho

### Controles do Encoder

| Gesto | Onde funciona | Ação |
|---|---|---|
| **Girar** | Menu / listas / teclado | Move o cursor |
| **Girar** | Tela **Mapa** | Muda o nível de zoom (entre os zooms existentes no SD) |
| **Girar** | Tela **Scanner** | Alterna a faixa (`433 MHz` ↔ `868 MHz` ↔ `915 MHz`) |
| **Clique curto** | Qualquer tela | Seleciona / confirma / volta (no Mapa e Scanner, volta ao menu) |
| **Clique curto** | Tela apagada | Apenas acorda o TFT (evita cliques acidentais no bolso) |
| **Segurar ≥ 2 s** | **Qualquer tela** (mesmo apagada) | Inicia contagem de **SOS** (10 s) |
| **Clique curto** | Durante contagem de SOS | **Cancela** o disparo do SOS |
| **Segurar ≥ 3 s** | Com **meu SOS ativo** | **Cancela** o SOS e envia `SOSOFF` ao amigo |
| **Clique curto** | Alarme **SOS do amigo** | Envia confirmação **"Vi"** (`SEEN`), silencia o alarme e abre a tela **Amigo** |

### Primeiro Uso (Configurar os 2 Aparelhos)

1. Ligue os dois aparelhos. Cada um gera um ID padrão de 2 caracteres a partir do MAC (ex.: `A1`, `B7`).
2. Se por acaso os dois tiverem o mesmo ID (o OLED e as Configurações exibem alerta de conflito), abra **Configurações → ID**, ajuste um deles e confirme (o aparelho reinicia automaticamente).
3. Ajuste **Brilho**, **Volume** e **Declinação magnética** (padrão `-21,5°` para São Paulo) em **Configurações** (o volume dá prévia sonora ao girar; lembrar que os sons de SOS tocam alto mesmo com volume 0).
4. Execute **Configurações → Calibrar bússola** girando o aparelho em formato de **8** em todas as direções por ~20 s para salvar offsets/escalas na NVS.

---

## Console de Debug Serial (USB 115200 bps)

Com `-DDEBUG_LOG=1`, você pode operar e inspecionar o aparelho pelo monitor serial:

| Comando | Ação |
|---|---|
| `status` | Exibe estado de `lora`, `imu` (e endereço), `mag`, `bme` (e endereço), `cc1101`, SOS, último contato, RSSI e distância/rumo até o amigo |
| `sensors` | Exibe detalhes do ICM-20948 (bússola, calibração, declinação), BME280 (temp, umidade, pressão, tendência 3 h, altitude barométrica) e rumo atual + fonte |
| `gps` | Exibe fix, satélites, coordenadas, altitude, velocidade, rumo, hora UTC-3 e bateria |
| `ping` | Envia um `PING` ao outro aparelho |
| `msg <texto>` | Envia uma mensagem curta (até 48 caracteres ASCII) |
| `pos <lat> <lon>` | Envia uma posição manual (útil para testar em ambiente fechado sem fix GPS) |
| `sos [<lat> <lon>]` | Inicia SOS imediatamente (usa sua posição atual se lat/lon forem omitidos) |
| `cancel` | Cancela seu SOS ativo (`SOSOFF`) |
| `vi` | Envia confirmação `SEEN` para o SOS do amigo |
| `fall` | Simula uma queda detectada pelo IMU (abre contagem de SOS de 10 s com bipes) |

---

## Roteiro de Testes na Bancada (Primeira Montagem)

Como o firmware foi desenvolvido e validado com testes nativos antes da montagem física, siga esta ordem ao ligar o hardware pela primeira vez:

1. **Boot & Serial:** conecte o USB, verifique o scan I2C no boot (esperados `0x3C`, `0x68/0x69`, `0x76/0x77`) e rode `status` e `sensors` — confira `lora=ok`, `imu=ok`, `bme=ok`, `cc1101=ok` e leitura de bateria (`gps`). Teste também desconectar ICM ou BME para confirmar que o aparelho continua funcionando mostrando `"--"`.
2. **LoRa (2 aparelhos):** teste `ping`, `msg ola` e `sos` / `vi` / `cancel` pelo serial; confirme que o `AT+DRSSI1` traz o RSSI e que a tensão no VCC do DX-LR22 se mantém ≥ 5 V durante TX.
3. **Display, OLED e Encoder:** confirme orientação/cores do ST7789, leitura do OLED `0x3C`, direção do giro do encoder e clique curto/longo.
4. **Áudio I2S:** teste clique em Configurações (volume 0 deve silenciar clique/mensagem, mas `fall` / SOS do amigo devem tocar alto, sem estalos).
5. **Mapa e microSD:** insira o cartão FAT32 com `/tiles/`, abra **Mapa**, teste zoom, remova/reinsira o cartão e confira a gravação de `/tracks/*.gpx` após obter fix GPS ao ar livre.
6. **IMU e Bússola (ICM-20948):** execute **Configurações → Calibrar bússola** girando em 8 por 20 s, confirme que a seta na tela **Amigo** gira em tempo real parado (sem aviso "rumo antigo") e verifique o rumo com `sensors`. Aguarde o TFT apagar (30 s) e mexa o aparelho (deve acordar via WoM no GPIO 7); simule queda numa almofada e aguarde 30 s imóvel (contagem de SOS) e pegue antes dos 30 s (cancela). Ajuste `kWomThreshold`, `impactG` e `stillBandG` se necessário.
7. **Barômetro (BME280):** confira temperatura, umidade, pressão e seta de tendência na tela **GPS** e em `sensors`. Ao ar livre com fix bom (`>= 6` satélites, `HDOP <= 2.0`), verifique a calibração automática da altitude barométrica e o envio no pacote `POS`.
8. **Scanner CC1101:** abra **Scanner**, alterne entre 433 / 868 / 915 MHz, transmita uma mensagem no segundo aparelho em 921,15 MHz para ver o pico subir na faixa de 915 MHz, e confirme que TFT e SD seguem funcionando normalmente após sair da tela.

---

## Documentação de Design

- [Design geral do dispositivo](docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md)
- [Etapa 3 — Interface, encoder e OLED](docs/superpowers/specs/2026-11-25-etapa3-ui-design.md)
- [Etapa 4 — Mapa offline, trilha GPX e SD](docs/superpowers/specs/2026-11-25-etapa4-mapa-gpx-design.md)
- [Etapa 5a — Áudio I2S](docs/superpowers/specs/2026-11-25-etapa5a-audio-design.md)
- [Etapa 5b — IMU (MPU6050)](docs/superpowers/specs/2026-11-25-etapa5b-imu-design.md)
- [Etapa 5c — Scanner CC1101 (somente RX)](docs/superpowers/specs/2026-11-25-etapa5c-scanner-design.md)
- [Guia de mapas offline](docs/mapas-offline.md)

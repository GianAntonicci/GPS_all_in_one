# Design — Etapa 5b: IMU 9DoF (ICM-20948) e Barômetro (BME280)

Data: 2026-11-25 (atualizado em 2026-11-27: migração MPU6050 → ICM-20948 + adição do BME280) · Complementa `2026-11-25-dispositivo-gps-lora-design.md` e `2026-11-25-etapa5a-audio-design.md`

Segunda parte da etapa 5 (5a áudio concluída, **5b IMU**, 5c scanner CC1101).

## Intenção

Em uma emergência, quem usa o aparelho pode cair e ficar desacordado. O IMU precisa:

- acordar o TFT automaticamente ao pegar ou mover o aparelho;
- detectar uma queda seguida de imobilidade e disparar a contagem cancelável de SOS de 10 s (com os bipes altos da etapa 5a), mesmo que o usuário não consiga apertar o botão.

## Decisões do usuário

| Tema | Decisão |
|---|---|
| Detecção de queda | **Sempre ativa** (sem opção de desligar em Configurações, para não correr o risco de esquecer desligada numa emergência) |
| Abordagem | **Task `imu` dedicada acordada pelo INT (GPIO 7)** + mutex `i2c_bus` compartilhado com o OLED + lógica pura `FallDetector` em `lib/imu/` testada no host |

## Decisões minhas, que o usuário pode corrigir

- **Configuração do MPU6050:** acelerômetro em **±8 g** (mede impactos bem acima de 3 g sem saturar em 4 g, mantendo boa resolução em torno de 1 g), **giroscópio em standby** (X/Y/Z) e sensor de temperatura desligado para economizar bateria.
- **Sensor ausente/falho no boot:** se o MPU6050 não responder em `0x68`, registra erro no log, marca `state().imuOk = false` e a task encerra limpa sem atrapalhar o resto do aparelho.
- **Assentamento após o impacto (~1,5 s):** logo depois de um pico > 3 g, o detector ignora os rebotes iniciais por ~1,5 s antes de começar a contar os 30 s de imobilidade.
- **Filtro contra ruído durante a imobilidade:** uma única amostra ruidosa não cancela a contagem de imobilidade — só movimento sustentado (~300 ms fora da banda de repouso) cancela a suspeita de queda.
- **Cooldown após disparo (60 s):** evita disparar uma segunda contagem imediatamente sobre o mesmo evento.
- **Console de debug:** adiciona comando `fall` (simula queda acionando `uiRequestSosCountdown()`) e inclui `imu=ok/ERRO` no `status` para facilitar testes antes de montar o hardware.

## Abordagens consideradas

- **A. Task `imu` dedicada acordada pelo INT (GPIO 7) + mutex `i2c_bus` (escolhida).** Em repouso a task dorme em `ulTaskNotifyTake` (zero CPU e zero tráfego I2C). Ao receber movimento no GPIO 7, acorda o TFT e amostra a ~50 Hz enquanto houver movimento ou suspeita de queda; depois volta a dormir. Nunca perde um pico curto (~30 ms) mesmo quando a UI está decodificando tiles JPEG do mapa (~100 ms/tile).
- **B. Polling contínuo na `uiTask`.** Rejeitada: mantém o I2C ativo o tempo todo (pior para bateria), desperdiça o pino INT da pinagem e pode perder o impacto da queda durante a decodificação de tiles do mapa.

## Barramento I2C compartilhado (`src/i2c_bus`)

O OLED (`0x3C`) e o MPU6050 (`0x68`) dividem `Wire` (SDA 8, SCL 18, 400 kHz). Assim como `src/spi_bus`:

- `i2cBusInit()` é chamado uma vez no `setup()` (inicializa `Wire` e cria o mutex).
- `i2cLock()` / `i2cUnlock()` envolvem todo acesso em `src/oled.cpp` e `src/imu.cpp`.

## Lógica pura (`lib/imu/`, testada no host)

**`FallDetector`:** recebe amostras `(axG, ayG, azG, nowMs)` em `g` e devolve um resultado (`None`, `SosCountdown`) mais uma flag `wantsSampling()` indicando se a task deve continuar amostrando ou pode voltar a dormir esperando o próximo INT.

Máquina de estados:

1. **`Idle`:**
   - Se `|a| >= kImpactG` (padrão `3.0f` g) → vai para `Settling`.
   - Enquanto não houver impacto, acompanha se há movimento (`||a| - 1.0f| > kStillBandG` ou `|Δa| > kStillDeltaG`); após `kQuietToSleepMs` (padrão `3000 ms`) quieto, `wantsSampling()` passa a `false` e a task volta a dormir.
2. **`Settling` (`kSettleMs = 1500 ms`):**
   - Ignora rebotes do impacto; ao terminar o tempo, entra em `StillWait`.
3. **`StillWait` (`kStillMs = 30000 ms`):**
   - Considera a amostra imóvel se `||a| - 1.0f| <= kStillBandG` (`0.25f` g) e `|Δa| <= kStillDeltaG` (`0.15f` g).
   - Se amostras fora dessa faixa durarem `kMoveCancelMs` (`300 ms`) contínuos → cancela e volta para `Idle`.
   - Se completar `30000 ms` imóvel → emite `SosCountdown` e entra em `Cooldown` (`60000 ms`).
4. **`Cooldown` (`kCooldownMs = 60000 ms`):**
   - Ignora novos impactos até expirar, depois volta para `Idle`.

Todos os limiares ficam em uma struct `FallConfig` com valores padrão em constantes, fáceis de ajustar depois no hardware.

## Glue (`src/imu` e `src/ui`)

- **`uiWakeScreen()` em `src/ui`:** `std::atomic<bool> gWakeRequest{false}`, consumido no loop da `uiTask` chamando `gWake.wake(now)` (só acorda a tela, sem abrir SOS).
- **Task `imu`:** core 1, prioridade 1 (abaixo de UI=2 e áudio=3), stack 4096.
- **Hardware MPU6050 (`electroniccats/MPU6050`):**
  - Configura acelerômetro ±8 g, giroscópio em standby, DHPF 5 Hz para o detector de movimento, limiar e duração de movimento em constantes (`kMotionThr`, `kMotionDur`), interrupção latched ativa em nível alto (limpa ao ler `INT_STATUS`).
  - ISR no `PIN_MPU_INT` (GPIO 7, `RISING`) acorda a task via `vTaskNotifyGiveFromISR`.
- **Ciclo da task:**
  - Dorme em `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`.
  - Ao acordar: lê `INT_STATUS` (limpa o latch), chama `uiWakeScreen()` (com cooldown de 1 s) e entra no laço de amostragem a 20 ms (`50 Hz`) lendo `getAcceleration()` sob `i2cLock()`.
  - Passa a amostra convertida em `g` para `FallDetector::update(...)`. Se retornar `SosCountdown`, chama `uiRequestSosCountdown()`.
  - Quando `!detector.wantsSampling()`, limpa `INT_STATUS` e volta a dormir.

## Testes

- **Host (`test/test_fall_detector/`):**
  - impacto > 3 g + 30 s imóvel dispara `SosCountdown`;
  - impacto abaixo do limiar (< 3 g) não dispara;
  - rebotes durante o assentamento (1,5 s) não cancelam a detecção;
  - movimento sustentado (> 300 ms) durante os 30 s cancela;
  - ruído isolado (< 300 ms) durante os 30 s não cancela;
  - cooldown de 60 s impede segundo disparo imediato;
  - `wantsSampling()` vai a `false` após 3 s quieto em `Idle` e volta a `true` em `onMotionWake()`.
- **Firmware:** `pio run -e esp32s3` compila sem erros ou warnings novos.
- **Hardware (depois):**
  - com TFT apagado, pegar/mexer o aparelho acorda a tela;
  - derrubar o aparelho numa almofada e deixar 30 s imóvel inicia contagem de SOS com bipes altos;
  - pegar o aparelho antes dos 30 s cancela a suspeita;
  - calibrar `kMotionThr`, `kImpactG` e `kStillBandG` medindo no conjunto montado.

## Atualização de Hardware (2026-11-27): ICM-20948 + BME280

- **ICM-20948 (substitui MPU6050):** biblioteca `sparkfun/SparkFun 9DoF IMU Breakout - ICM 20948 - Arduino Library`, autodetectado em `0x68` (AD0=0) ou `0x69` (AD0=1). Mantém acelerômetro em ±8 g (saída convertida de mg para g para o `FallDetector`) e Wake-on-Motion (`WOMLogic` / `WOMThreshold`) no GPIO 7 (ativo alto, latched).
- **Bússola (AK09916 interno):** lógica pura em `lib/imu/compass.{h,cpp}` (compensação de inclinação usando o acelerômetro, calibração hard-iron/soft-iron em modo "Calibrar bússola" de ~20 s, declinação magnética configurável com padrão `-21,5°` para São Paulo, ambos salvos na NVS). Seleção de fonte de rumo: `gps.course` quando há fix e velocidade ≥ 1,5 km/h; bússola calibrada quando parado (< 1,5 km/h) ou sem fix (removendo o aviso `"rumo antigo"`).
- **BME280 (`src/bme.{h,cpp}` + `lib/env/baro_logic.{h,cpp}`):** biblioteca `adafruit/Adafruit BME280 Library`, autodetectado em `0x76` (fallback `0x77`). Lido a cada 5 s em modo *forced* pela task `imu` (mesmo se o ICM-20948 estiver ausente). Fornece temperatura, umidade, pressão com histórico circular de 3 h (tendência `↑ / → / ↓` e alerta de queda rápida > 2 hPa/3 h) e altitude barométrica suavizada e calibrada pelo GPS (`sats >= 6`, `HDOP <= 2.0`), exibida como principal na tela GPS e enviada como campo opcional no pacote `POS`.
- **Orientação de montagem configurável (`lib/imu/mount.{h,cpp}`):** remapeamento de eixos em 24 rotações próprias de 90° (`det = +1`) aplicado a acelerômetro, giroscópio e magnetômetro (após correção Y/Z do AK09916) antes de qualquer cálculo. Padrão `IMU_MOUNT_DEFAULT` em `src/config.h` sobrescrito pela NVS; configurável em **Configurações → Orientacao IMU** (preset manual ou modo assistido em 2 passos: tela para cima → `+Z`, topo para baixo → `-X`). Alterar a orientação invalida a calibração da bússola e avisa `"Recalibre a bussola"`. Comando serial `imu axes` exibe aceleração remapeada e rumo a 1 Hz até Enter.
- **Scan I2C no boot e tolerância a falhas:** `i2cBusScan()` registra todos os endereços presentes no boot; a ausência do ICM-20948 ou do BME280 é tolerada e exibida como `"--"` na UI e no comando serial `sensors`.

## Deixado ao builder

- Valores finos de `kMotionThr` / `kMotionDur` iniciais do registrador do MPU6050.
- Detalhes internos da struct de configuração e nomes de funções auxiliares.

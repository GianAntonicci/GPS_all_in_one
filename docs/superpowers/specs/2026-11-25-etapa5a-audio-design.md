# Design — Etapa 5a: áudio (tons por I2S)

Data: 2026-11-25 · Complementa `2026-11-25-dispositivo-gps-lora-design.md` e `2026-11-25-etapa3-ui-design.md`

A etapa 5 foi dividida em três partes independentes, feitas nesta ordem: **5a áudio**, 5b IMU e 5c scanner CC1101. O áudio vem primeiro porque o alarme é o que mais importa numa emergência e porque a contagem da queda (5b) precisa ser ouvida.

## Intenção

Com o aparelho no bolso, ninguém olha a tela. O som precisa avisar:

- que o amigo pediu socorro;
- que o meu SOS vai sair (contagem);
- que o meu SOS chegou e foi visto.

## Decisões do usuário

| Tema | Decisão |
|---|---|
| Volume no SOS | Os sons de SOS **ignoram o volume** e tocam sempre num nível alto fixo, mesmo com o volume em 0. Clique e mensagem seguem o volume (0 = mudo) |
| Sons | Os três da spec (clique, mensagem, alarme do SOS do amigo), **mais** os bipes da contagem e os tons de "recebido" e "visto" do meu SOS |
| Abordagem | **Task de áudio dedicada**, dona do I2S, com fila de pedidos e sequenciador de padrões puro, testado no host |

## Decisões minhas, que o usuário pode corrigir

- Girar o encoder **não** faz som. O clique dá um "tic" curto e o Hold2s dá um bipe de confirmação.
- O alarme do SOS do amigo toca enquanto o overlay "AMIGO PEDIU SOCORRO" está em cima (`Overlay::PeerSos`). Para quando o usuário clica (envia o "Vi") ou quando o amigo cancela. Toca também com a tela apagada.

## Abordagens consideradas

- **A. Task de áudio dedicada (escolhida).** Recebe "tocar padrão X" ou "parar" por uma fila e alimenta o I2S. A UI nunca bloqueia.
- **B. Gerar as amostras na task de UI.** Rejeitada: `i2s_write` bloqueia quando o buffer enche, o que trava o encoder e a tela. Se a UI atrasa, o som sai picotado.
- **C. Tocar WAV do SD.** Rejeitada: o alarme dependeria do cartão, e a spec pede tons gerados.

## Padrões

Os valores são iniciais e ajustáveis em constantes. A prioridade vai de 1 (maior) a 5.

| Padrão | Tons | Repete | Nível | Prioridade |
|---|---|---|---|---|
| `PeerAlarm` | 900 Hz 250 ms / 1400 Hz 250 ms | sim, até parar | alto fixo | 1 |
| `SosSeen` | 1600 Hz 300 ms, pausa 120, 1600 Hz 300 ms | não | alto fixo | 2 |
| `SosReceived` | 800 → 1200 → 1600 Hz, 120 ms cada | não | alto fixo | 2 |
| `CountdownFinal` (últimos 3 s) | 2000 Hz 150 ms | não | alto fixo | 3 |
| `CountdownTick` | 1000 Hz 80 ms | não | alto fixo | 3 |
| `Message` | 1000 Hz 100 ms, pausa 60, 1500 Hz 100 ms | não | volume | 4 |
| `Hold` | 1500 Hz 80 ms | não | volume | 5 |
| `Click` | 2000 Hz 15 ms | não | volume | 5 |

- Os bipes da contagem tocam alto porque fazem parte do SOS. Na queda, quem caiu precisa ouvir.
- **Precedência:**
  - Um pedido de prioridade menor (número maior) que o padrão tocando é ignorado.
  - Um pedido de prioridade igual ou maior substitui o atual.
  - Quando um padrão de uma vez acaba enquanto o alarme deveria tocar, o alarme volta. A UI reafirma o alarme a cada iteração enquanto ele for desejado.
- `stop(PeerAlarm)` só para o alarme. Os outros padrões terminam sozinhos.

## Lógica pura (`lib/audio/`, testada no host)

- **`ToneSequencer`:**
  - `play(Pattern, now)` e `stop(Pattern)`.
  - `current(now)` retorna `{freqHz, loud}` ou silêncio.
  - `busy(now)` e `playing()` dizem o que está ativo.
  - Aplica a precedência e a repetição acima.
- **`toneAmplitude(volumePct, loud)`:**
  - `loud` → amplitude fixa alta (~80% da escala).
  - Senão, a amplitude é proporcional ao volume, com uma curva perceptual simples (quadrática).
  - Volume 0 → 0.
- **Decisão de quando tocar** (`AudioCues`), alimentada pela UI a cada iteração:
  - Alarme desejado = `top() == Overlay::PeerSos`.
  - Bipe da contagem quando o número da contagem muda (`CountdownFinal` se ≤ 3).
  - `SosReceived` / `SosSeen` na **transição** do meu SOS para Received / Seen.
  - `Message` num MSG recebido.
  - `Click` / `Hold` nos gestos.
  - Com isso, a UI só repassa eventos, e as regras são testadas.

## Glue (`src/audio`)

- **Task `audio`:** core 1, prioridade 3 (acima da UI, para não faltar amostra), stack 4096.
- **I2S legado** (`driver/i2s.h`):
  - 16 kHz, 16 bit, stereo com o mesmo valor nos dois canais (o MAX98357A mistura L+R);
  - pinos BCLK 15, LRC 16, DIN 17;
  - DMA de 4 × 256 frames.
- Gera seno por tabela, com rampa de ~3 ms no início e no fim de cada tom para não estalar.
- **Ocioso:** zera o DMA e chama `i2s_stop`. Sem BCLK, o MAX98357A entra em shutdown e gasta menos. Ao chegar um pedido, `i2s_start`.
- **API thread-safe** (fila de 8):
  - `audioBegin(volumePct)`;
  - `audioPlay(Pattern)`;
  - `audioStop(Pattern)`;
  - `audioSetVolume(pct)`.
- Ao mudar o volume em Configurações, toca um `Click` no novo volume, como prévia.

## Testes

- **Host:**
  - sequência de tons e tempos de cada padrão;
  - repetição do alarme;
  - precedência (clique não interrompe o alarme, SOS interrompe a mensagem);
  - o alarme volta após um padrão de uma vez;
  - amplitude com volume 0 e `loud`;
  - `AudioCues`: transições do SOS, contagem, MSG e gestos.
- **Firmware:** `pio run -e esp32s3` compila.
- **Hardware (depois):** ouvir cada padrão; volume 0 silencia o clique mas não o alarme; tela apagada com alarme tocando; sem estalos.

## Deixado ao builder

- Frequências e tempos finos.
- Forma exata da curva de volume.
- Tamanho da tabela de seno.
- Nomes internos.

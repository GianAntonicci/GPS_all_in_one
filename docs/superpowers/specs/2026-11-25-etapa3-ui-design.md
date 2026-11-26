# Design — Etapa 3: UI, encoder, OLED e SPI compartilhado

Data: 2026-11-25 · Complementa `2026-11-25-dispositivo-gps-lora-design.md`

## Intenção

Dar ao aparelho de emergência uma interface que funcione **sob estresse**: tela legível, poucos gestos, SOS a 2 s de distância de qualquer tela e o estado do enlace sempre visível no OLED. Até agora tudo era operado pelo console serial. Esta etapa substitui esse uso; o console continua só para debug.

## Objetivos

- Menu raiz e as telas Amigo, Mensagens (com mensagens rápidas, teclado e histórico), GPS e Configurações.
- Fluxo completo de SOS na tela, como emissor e como receptor.
- OLED de status sempre ligado.
- Economia de energia: TFT apagado após 30 s.
- Mutex do SPI pronto para o SD (etapa 4) e para o CC1101 (etapa 5).

## Não-objetivos (ficam para outras etapas)

- Mapa e Scanner: nesta etapa são telas "em breve".
- Áudio: alarme e bipes ficam para a etapa 5. Aqui o alerta é só visual.
- Acordar por movimento (IMU) fica para a etapa 5.
- GPX fica para a etapa 4.

## Anti-objetivos

- Um gesto ambíguo que dispara ou cancela o SOS sem querer.
- A tela piscar ou deixar resto de tela antiga.
- A UI atrasar o rádio. A UI nunca bloqueia a task de rádio, e o SPI nunca é segurado por mais de um quadro.

## Decisões do usuário

| Tema | Decisão |
|---|---|
| Voltar | item **"← Voltar"** no topo de cada tela interna; no menu raiz não há Voltar |
| Orientação | TFT em **paisagem 320×240** |
| Barra de status | **não existe no TFT**; hora, bateria, satélites e sinal ficam no OLED |
| Estilo | **escuro**; menu raiz em **grade 3×2 de ícones**; o selecionado fica em amarelo (#FFD400) sobre azul-escuro (#243040); fundo #101418 |
| OLED | linha 1: `HH:MM  ⌖sats  bat%`; linha 2: `ID barras idade ✉`; durante SOS a linha 2 vira o aviso |
| Teclado | **grade**: A–Z, 0–9 e `. , ? !`, mais espaço, ⌫, ABC/abc e ENVIAR; o giro percorre as teclas linha por linha |
| Mensagens rápidas | "Estou bem", "Preciso de ajuda", "Onde voce esta?", "Venha ate mim", "Ok", "Ferido" |
| Telas de SOS | contagem de 10 s (qualquer clique cancela) → ENVIANDO → RECEBIDO → VISTO ✓; o receptor vê "AMIGO PEDIU SOCORRO" e clicar envia "Vi" e abre a tela Amigo. Durante ENVIANDO e RECEBIDO, cancelar exige **segurar 3 s** |
| Abordagem gráfica | **quadro inteiro em PSRAM** (sprite 320×240×16 bit), enviado inteiro ao TFT |

Os mockups aprovados estão em `.superpowers/brainstorm/` (não versionado): `estilo-telas-paisagem.html` (opção C), `teclado.html` (opção A) e `sos.html`.

## Abordagens consideradas

- **A. Desenho parcial direto no TFT.** Rejeitada: pisca e obriga cada tela a controlar o que já está desenhado.
- **B. LVGL.** Rejeitada: pesada, fora da lista de bibliotecas, com estilo próprio difícil de adaptar ao visual aprovado, e complica o mapa de tiles.
- **C. Quadro inteiro em PSRAM (escolhida).** Não pisca e cada tela só desenha o seu estado atual. O mapa da etapa 4 desenha no mesmo quadro. Custa cerca de 30 ms de SPI por quadro, o que é irrelevante a poucos quadros por segundo.

## Arquitetura

### Tasks

| Task | Core | Função |
|---|---|---|
| radio | 0 | já existe (LoRa, GPS, bateria) |
| ui | 1 | entrada do encoder, telas, quadro do TFT, OLED, consumo dos `RadioEvt` |
| loop() | 1 | só o console de debug (`consolePoll`) |

A UI passa a ser a **única consumidora** de `radioNextEvent`. Quando `DEBUG_LOG` está ligado, ela repassa cada evento ao console para impressão (`consolePrintEvent`).

### Novos eventos do rádio

A UI precisa saber qual seq corresponde à mensagem que ela pediu para enviar. A task de rádio passa a publicar:

- `MsgQueued{seq, text}` quando `sendMsg` aceita;
- `MsgBusy` quando já há uma MSG ou PING pendente.

A entrega continua chegando por `Delivery{seq, Delivered/Failed}`.

### SPI compartilhado

`src/spi_bus.h`: `spiLock()` e `spiUnlock()` com mutex FreeRTOS. Todo acesso de TFT, SD e CC1101 acontece entre os dois. O TFT_eSPI usa o barramento FSPI nos pinos do spec. Cada `pushSprite` é uma seção crítica.

### Quadro e backlight

- `TFT_eSprite` de 320×240 em 16 bit, alocado em PSRAM, com `setRotation(1)` no TFT.
- **Redesenho:**
  - sob demanda: entrada, evento ou mudança de estado relevante;
  - 1× por segundo enquanto a tela mostrada tiver relógio ou idades.
  - Máximo de 20 quadros por segundo.
- **Backlight:** PWM LEDC no GPIO 14. O brilho vem das Configurações (10–100%, passos de 10).
- **Tela apagada:** após 30 s sem entrada, o backlight vai a 0 e o redesenho para.
- **Enquanto houver overlay de SOS** (meu SOS ativo, contagem ou SOS do amigo), **a tela não apaga**. Nesta etapa a tela é o único alerta. *(Decisão do builder.)*
- **Acordar a tela:**
  - O primeiro giro ou clique com a tela apagada **só acorda**: não navega e não clica.
  - Segurar 2 s funciona mesmo com a tela apagada. O SOS nunca exige acordar antes.
  - Mensagem recebida, SOS do amigo ou mudança de estado do meu SOS também acendem a tela.

### Fontes e texto

As fontes nativas do TFT_eSPI (GLCD, Font2 e Font4, com setTextSize) são **ASCII**. Os rótulos da UI ficam **sem acento** ("Configuracoes", "Posicao"), coerentes com as mensagens, que já são ASCII. Os ícones (seta, envelope, satélite, barras) são desenhados com primitivas.

### Encoder

**Pinos:** A=4, B=5 e SW=6, todos `INPUT_PULLUP`.

**Giro:**
- ISR por mudança em A e B, com tabela de estados de quadratura (rejeita transições inválidas e bounce).
- `ENC_STEPS_PER_DETENT = 4` (constante, ajustável no hardware). A ISR acumula passos num contador atômico.
- A UI converte passos em "cliques de giro", ±1 por detent.

**Botão:**
- ISR registra a mudança. A máquina de gestos roda na task de UI a cada 5 ms, com debounce de 30 ms.
- Gestos:

| Gesto | Quando dispara |
|---|---|
| `Click` | botão solto antes de 2 s |
| `Hold2s` | **quando completa 2 s ainda pressionado** (não espera soltar) |
| `Hold3s` | quando completa 3 s ainda pressionado, usado no cancelamento do SOS |

- Depois de um Hold, soltar não gera Click.

### Navegação

- **Menu raiz:** grade 3×2 na ordem Amigo, Mensagens, Mapa, GPS, Scanner, Configuracoes. O giro percorre os blocos em ordem de leitura e dá a volta.
- **Telas internas:** são listas de itens selecionáveis, sendo o item 0 sempre "← Voltar".
- **Telas sem outro item** (Amigo, GPS, placeholders): o cursor fica parado no Voltar e qualquer clique volta.
- **Pilha de telas:** profundidade máxima de 3, por exemplo Menu → Mensagens → Teclado.
- **Overlays:** SOS e banner de mensagem ficam por cima da pilha.

### Telas

- **Amigo:**
  - à esquerda, seta grande para `relativeBearingDeg(bearing, meuRumo)`, com "+38° do rumo" embaixo;
  - **rumo antigo (parado): a seta continua relativa ao último rumo**, com o aviso "rumo antigo - ande alguns passos" *(decisão do usuário)*. O mesmo vale para as telas VISTO e AMIGO PEDIU SOCORRO;
  - sem nenhum rumo conhecido (nunca andou desde o 1º boot), a seta aponta para o norte absoluto, com o texto "N↑ (sem rumo)";
  - à direita, a distância em fonte grande ("850 m" abaixo de 1 km, "1,8 km" de 1 a 10 km, "23 km" acima), sinal em dBm com barras, "Ult. msg ha 25 s", "Posicao ha 31 s" e a bateria do amigo;
  - sem posição do amigo: "Sem posicao do amigo"; sem a minha: "Sem minha posicao".
- **Mensagens:**
  - itens: "Escrever mensagem…", as 6 rápidas e o histórico, que é rolável e mostra as mais novas primeiro;
  - o histórico guarda as **últimas 20 mensagens em RAM**: enviadas (✓ entregue, ✗ falhou, … pendente) e recebidas, com o horário local;
  - clicar numa rápida envia na hora. Com uma mensagem pendente aparece "Aguarde…" e nada é enviado.
  - Cada mensagem do histórico ocupa uma linha, cortada com "…" se não couber. *(Decisão do builder.)*
  - Clicar numa mensagem abre o **Detalhe**: texto inteiro em várias linhas, remetente, hora e estado. Uma mensagem minha que falhou ganha o item "Reenviar". A pilha fica em Menu → Mensagens → Detalhe, profundidade 3.
- **Teclado:**
  - linha de texto com contador `n/48`;
  - grade de 10 colunas: A–J, K–T, U–Z e 0–3, 4–9 e `. , ? !`, e a última linha com espaço (3 colunas), ⌫ (2), ABC/abc (2) e ENVIAR (3);
  - o giro dá a volta na grade. ENVIAR com o texto vazio não faz nada.
  - Depois de enviar, volta para Mensagens.
- **GPS:** satélites, fix, lat/lon (posição atual ou "ultima posicao"), altitude, velocidade, rumo (com "(antigo)"), data e hora local e bateria em mV e %.
- **Configuracoes:**
  - **ID:** edita 2 caracteres [A-Z0-9]. Clicar entra no modo de edição do 1º caractere, girar troca, clicar passa ao 2º e clicar de novo confirma.
    - **Salvar um ID novo reinicia o aparelho** (`ESP.restart()` após "Reiniciando…"), porque o Link guarda o ID no boot.
  - **Brilho** e **Volume:** clicar entra no modo de edição, girar ajusta em passos de 10 e clicar salva na NVS. O brilho tem efeito imediato.
  - **Canal:** só leitura ("921,15 MHz (fixo)").
  - Se houver conflito de ID detectado: aviso em vermelho "ID repetido! Mude o ID".
- **Mapa e Scanner:** "Em breve (etapa 4)" e "Em breve (etapa 5)", com Voltar.

### Overlays de SOS (sobre qualquer tela)

Iniciar a contagem:
- `Hold2s` em qualquer tela, sem nenhum overlay de SOS ativo, abre a contagem.
- A etapa 5 chama a mesma entrada (`uiRequestSosCountdown()`) quando detectar uma queda.

| Estado | Tela | Entrada |
|---|---|---|
| Contagem 10→1 | fundo #600000, número grande, "CLIQUE = CANCELAR" | Click cancela; ao chegar a 0 envia `SosStart` |
| Sending | fundo #B00000, "SOS ENVIANDO", tentativa e posição (com "(fix atual)" ou "(ultima posicao)") | Hold3s envia `SosCancel` |
| Received | fundo #A06000, "RECEBIDO", "aguardando ele ver…" | Hold3s envia `SosCancel` |
| Seen | fundo #006000, "VISTO ✓", distância e seta até o amigo | Click fecha o overlay |
| Peer SOS | fundo #FFD400 com texto preto, "AMIGO PEDIU SOCORRO", seta, distância, idade da posição e bateria | Click envia `Seen` e abre Amigo |

- Os overlays seguem `SharedState.mySos` e `peerSos`, que mudam pelos eventos do rádio.
- Se os dois estiverem ativos, **Peer SOS tem prioridade na tela**, e o meu estado aparece numa faixa de 20 px embaixo. Nessa tela, Click envia "Vi" e Hold3s cancela o **meu** SOS. Os dois gestos continuam distintos. *(Decisão do builder.)*
- O número da tentativa vem de um novo campo, `SharedState.sosAttempts`, preenchido pela task de rádio.

### Dados novos no estado

*(Decisões do builder.)*
- `SharedState.peerId[3]`: o ID do amigo, preenchido com qualquer pacote válido recebido. Usado no banner e no histórico.
- `SharedState.sosAttempts`: o número da tentativa do meu SOS, mostrado na tela ENVIANDO.
- Brilho: a NVS continua guardando 0–255 (`bri`). A UI converte para 10–100% (passos de 10) e de volta; um valor antigo é arredondado para o passo mais próximo.

### Banner de mensagem recebida

- Mostra "B2: texto" numa faixa de 40 px no topo durante 5 s, por cima da tela atual.
- O ícone ✉ fica ativo no OLED até a tela Mensagens ser aberta.

### OLED (SSD1306 128×32, I2C 0x3C, SDA 8 / SCL 18, 400 kHz)

- Atualiza 2× por segundo, a partir da task de UI.
- **Linha 1:**
  - `HH:MM`, ou `--:--` sem hora;
  - ícone de satélite e o número de satélites;
  - `NN%` da bateria, piscando quando estiver baixa.
- **Linha 2:**
  - o meu ID;
  - barras de sinal: 4 a partir de −90 dBm, 3 a partir de −100, 2 a partir de −110, 1 a partir de −120, senão 0; `----` sem contato;
  - idade do último pacote do amigo: `25s`, `12m`, `3h` ou `--`;
  - `✉` piscando quando há mensagem nova.
- Prioridade da linha 2, da maior para a menor *(decisão do builder: um erro nunca fica escondido atrás de "SOS ENVIANDO")*:
  1. `LORA ERRO` (`loraOk == false`)
  2. `ID REPETIDO` (`idConflict`)
  3. `AMIGO SOS!` (piscando)
  4. `SOS ENVIANDO`
  5. `SOS RECEBIDO`
  6. `SOS VISTO` (durante 30 s após o estado Seen)
  7. linha normal
- Quando o MPU6050 entrar no I2C (etapa 5), o I2C ganha um mutex. Nesta etapa só a UI usa o barramento.

## Lógica pura (testada no PC, em `lib/ui_logic`)

- Decodificação de quadratura (tabela) e acumulação por detent.
- Máquina de gestos do botão: debounce, Click, Hold2s e Hold3s.
- Navegação em grade com volta e em listas com Voltar.
- Modelo do teclado: cursor, inserir, ⌫, maiúsculas, limite de 48 e posição de cada tecla.
- Histórico de 20 mensagens com estados (pendente, entregue, falhou) casados por seq.
- Formatação: distância, idades ("ha 25 s", "12m"), barras de sinal e as linhas do OLED.
- Máquina do overlay de SOS: contagem, prioridade Peer > meu e regra do Seen por 30 s.
- Regra de acordar: a primeira entrada com a tela apagada é consumida, exceto o Hold2s.

## Deixado ao builder

- Tamanhos exatos de fonte e espaçamentos (seguindo os mockups).
- Desenho dos ícones.
- Valor final de `ENC_STEPS_PER_DETENT`, ajustado no hardware.
- Estrutura interna das classes de tela.

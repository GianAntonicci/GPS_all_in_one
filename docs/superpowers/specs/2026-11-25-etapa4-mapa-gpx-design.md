# Design — Etapa 4: mapa offline, trilha GPX e SD

Data: 2026-11-25 · Complementa `2026-11-25-dispositivo-gps-lora-design.md` e `2026-11-25-etapa3-ui-design.md`

## Intenção

O aparelho é um kit de emergência: fica guardado desligado e é ligado quando alguém precisa se localizar, achar o amigo ou voltar pelo caminho por onde veio. Esta etapa entrega o seguinte:

- **Mapa offline:** tiles no cartão SD, com a minha posição e a do amigo. O amigo aparece com uma seta na borda se estiver fora da tela.
- **Trilha da sessão atual:** desenhada no mapa para ajudar a voltar.
- **Arquivo GPX no SD:** gravado de forma que sobreviva a um corte de bateria.
- **Script no PC:** prepara os tiles para o cartão.

## Objetivos

- **Tela Mapa** (substitui o "Em breve"):
  - Tiles `/tiles/{z}/{x}/{y}.jpg` (256×256, slippy map / Web Mercator) num viewport **320×240** (paisagem).
  - Marcadores: eu e o amigo. Se o amigo estiver fora da tela, aparece uma seta na borda.
  - A trilha da sessão aparece como uma linha.
  - Zoom pelo encoder, só nos níveis presentes no SD.
- **GPX:** um arquivo por sessão, `/tracks/AAAAMMDD_HHMMSS.gpx`.
  - Um ponto a cada 10 s ou a cada 20 m, o que vier primeiro.
  - O XML fica válido depois de cada ponto (flush por ponto).
- **Task de SD** de baixa prioridade, dona das escritas do GPX e da trilha em PSRAM.
- **`tools/prepare_tiles.py`:**
  - Converte uma pasta de tiles baixada (por exemplo com o MOBAC, em PNG ou JPG) para **JPEG baseline** 256×256 no layout `z/x/y.jpg`.
  - Mostra tiles por zoom, buracos e tamanho total.
- **Instruções de uso** (README ou doc): como baixar a região, preparar e copiar para o SD.

## Não-objetivos

- Pan manual. O mapa sempre centra automaticamente.
- Mostrar no mapa a trilha de sessões anteriores. O usuário escolheu só a sessão atual.
- A trilha do amigo.
- Um downloader de tiles próprio. O usuário baixa com um programa pronto, respeitando os termos da fonte; o servidor do OSM proíbe download em massa.
- Rotação do mapa pelo rumo. O norte fica sempre para cima.
- Áudio, IMU e scanner, que são da etapa 5.

## Anti-objetivos

- **O SOS ou o ACK atrasarem por causa do mapa ou do SD.** A task de rádio nunca toca o SD nem espera o mutex do SPI.
- **Segurar o mutex do SPI por mais de ~1 quadro.** A decodificação do JPEG **não** pode acontecer com o SPI travado; ver "Leitura de tiles".
- **Um GPX corrompido** depois de um corte de energia.
- **Um tile ruim travar a tela.** Isso vale para tile progressivo, PNG renomeado ou arquivo truncado.

## Decisões do usuário

| Tema | Decisão |
|---|---|
| Origem dos tiles | O usuário baixa com um programa pronto (MOBAC). O `tools/prepare_tiles.py` converte para JPEG baseline `z/x/y.jpg` e confere. Cartão de 32 GB |
| Trilha no mapa | **Só a sessão atual**, como uma linha sobre o mapa. Os pontos ficam em PSRAM e somem ao desligar; o arquivo GPX fica no SD |
| Escrita no SD | **Task de SD dedicada**, baixa prioridade, alimentada por uma fila. O rádio só enfileira pontos |

## Decisões minhas, que o usuário pode corrigir

- **Gestos no Mapa:**
  - Girar muda o zoom: horário aproxima, anti-horário afasta.
  - Clique volta ao menu.
  - Segurar 2 s abre o SOS, como em qualquer tela.
- **GPX sempre ligado** com fix. Não há botão de ligar e desligar: a trilha só serve se estiver gravando quando for preciso.

## Abordagens consideradas (escrita no SD)

- **A. Task de SD dedicada (escolhida).** O rádio decide o ponto e põe numa fila. A task de SD grava o GPX e atualiza a trilha. Se o SPI estiver ocupado com um tile, só ela espera. Custa uma task e uma fila, cerca de 4 KB.
- **B. O rádio grava direto.** Rejeitada: o rádio esperaria o SPI enquanto a UI lê tiles, e o ACK e o SOS atrasariam.
- **C. A UI grava entre quadros.** Rejeitada: com a tela apagada ou a UI ocupada, a trilha deixa de ser gravada.

A renderização do mapa segue a spec principal: tiles decodificados num cache LRU em PSRAM e compostos no quadro (sprite) pela task de UI.

## Arquitetura

### Tasks

| Task | Core | Prio | Mudança |
|---|---|---|---|
| radio | 0 | 3 | Decide quando gravar um ponto da trilha e manda um `TrackPoint` para a fila de SD sem bloquear (descarta se a fila estiver cheia). Não toca o SD |
| sd (nova) | 0 | 1 | Monta o SD no boot. Consome a fila: escreve o GPX e acrescenta à trilha em PSRAM. Publica o estado do SD e da trilha |
| ui | 1 | 2 | Tela Mapa: lê tiles do SD, decodifica, mantém o cache e desenha |

### Fila e dados

- `TrackPoint{ lat, lon (double), alt (float), utc (DateTime), newSegment (bool) }`.
- A fila tem cerca de 16 entradas. Fila cheia descarta o ponto e registra um LOGW.
- `newSegment` é true no primeiro ponto depois de cada (re)aquisição do fix.

### Estado novo

O estado novo fica em `SharedState` ou num módulo próprio, como o builder preferir:

- `sdOk`: SD montado.
- `trackRecording`: arquivo aberto e gravando.
- `trackPoints`: número de pontos no arquivo.
- O nome do arquivo, para a tela GPS e o debug.
- A hora **UTC** do GPS. Hoje só existe `localTime`, e o GPX precisa de UTC.

### Trilha em PSRAM

- Buffer de pontos `float lat, lon`, com um marcador de início de segmento.
  - Capacidade sugerida: 20 000 pontos, ~160 KB. São cerca de 400 km a 20 m por ponto.
  - Ao encher, **dizima** (remove 1 a cada 2 pontos) em vez de parar ou descartar o começo, para manter o caminho inteiro mais grosso.
- Só entra na trilha um ponto a ≥ 20 m do último da trilha, para não acumular pontos parado. O GPX recebe todos.
- A task de SD escreve e a UI lê. É preciso um mutex próprio; a UI copia ou desenha sob esse lock, e o lock nunca é segurado durante I/O.

## Mapa

### Projeção e viewport

- Projeção Web Mercator padrão: lat/lon → pixel global no zoom z (`256·2^z`). O centro do viewport é o pixel global do centro escolhido.
- O viewport 320×240 cobre no máximo 3×2 tiles (ou 2×2 alinhado).
- **Lógica pura testada no host**, em `lib/map/`:
  - a lista dos tiles visíveis e de onde cada um começa no quadro;
  - a posição de um ponto no quadro.

### Centro

1. Minha posição, se `myPosKnown` (fix atual ou última salva).
2. Senão, o amigo, se `peerKnown`.
3. Senão, a tela mostra "Sem posição — aguardando GPS".

### Marcadores

- **Eu:**
  - Com rumo conhecido (`courseKnown`): triângulo apontando para o rumo, vazado se `courseStale`.
  - Sem rumo: círculo.
  - Sem fix atual (centro na posição salva): outra cor e o texto "última posição".
- **Amigo:** um ponto de cor distinta com o ID ao lado.
  - Fora do viewport: uma seta na borda, na direção dele, com a distância (`fmtDistance`).
  - A posição da seta na borda vem de uma função pura testada.
- **Trilha:** linha de 2 px entre pontos consecutivos do mesmo segmento, cortada à tela.

### Zoom

- No boot, a UI lista os diretórios numéricos de `/tiles/` (0–19) e guarda o conjunto de níveis presentes.
- O encoder anda pelos níveis presentes, pulando os que faltam.
- O zoom inicial é o último usado, mantido em RAM na sessão. Sem histórico, usa o maior nível presente ≤ 16.
- Mostrar `z15` e uma barra de escala num canto.

### Leitura de tiles

Esta parte é crítica para o anti-objetivo do SPI.

1. Sob `spiLock()`, ler o arquivo `.jpg` inteiro do SD para um buffer em PSRAM. Um tile tem tipicamente 5–40 KB, o que dá poucos ms. Liberar o SPI.
2. **Sem** o lock, decodificar com TJpg_Decoder a partir da memória para o slot RGB565 do cache, 256×256×2 = 128 KB.
3. Erros viram um slot "sem mapa", e o tile não é relido a cada quadro:
   - arquivo ausente;
   - leitura falha;
   - JPEG progressivo ou inválido (o decoder retorna erro);
   - tamanho acima de um limite (64 KB, por exemplo).
   - A ausência fica registrada no cache como uma entrada negativa.
4. **Decodificar no máximo 1 tile por iteração da UI.** Os que faltam aparecem como fundo cinza "carregando" até chegar a vez deles, para que a entrada (SOS em 2 s) continue responsiva.

### Cache

- LRU de slots RGB565 em PSRAM, com chave (z, x, y). Começa com 24 slots, ~3 MB.
- A lógica do LRU (achar, inserir, despejar o menos usado, entradas negativas) é pura e testada no host. O armazenamento dos pixels é glue.

### Quando redesenhar

- Com a tela Mapa aberta e a tela acesa, redesenhar quando algo muda:
  - a posição do centro muda ≥ 1 px;
  - o zoom muda;
  - um tile termina de carregar;
  - o amigo ou a trilha mudam;
  - pelo menos 1× por segundo.
- Fora da tela Mapa nada é decodificado. O cache permanece.

### Sem SD

A tela Mapa mostra "Sem cartão SD" (ou "Sem mapas no cartão", se `/tiles` estiver vazio). Ainda assim desenha os marcadores e a trilha sobre fundo liso, em escala do último zoom. Assim a seta do amigo e o caminho de volta funcionam mesmo sem tiles.

## GPX

### Quando gravar

`TrackPolicy`, pura e testada no host:

- Grava um ponto se há fix e se passou ≥ 10 s desde o último ponto **ou** se ele está a ≥ 20 m do último.
- O primeiro ponto com fix sempre grava.
- A perda do fix encerra o segmento, e o próximo ponto vem com `newSegment = true`.

### Arquivo

- Criado no **primeiro ponto** da sessão, quando há fix e hora válida.
- O nome usa a hora **local** (UTC-3, a do usuário): `/tracks/AAAAMMDD_HHMMSS.gpx`.
- Os `<time>` usam UTC ISO 8601 com `Z`.
- Se o nome já existir, acrescenta o sufixo `_2`, `_3`, e assim por diante.
- Estrutura: `gpx` 1.1, `creator`, um `<trk>` e um `<trkseg>` por segmento.
- Cada `<trkpt lat lon>` tem `<ele>` e `<time>`. Lat/lon com 6 casas.

### À prova de corte

- O rodapé tem tamanho fixo: `</trkseg></trk></gpx>\n`.
- Para cada ponto:
  1. dar seek para o início do rodapé;
  2. escrever o ponto (e `</trkseg><trkseg>` se for novo segmento) e o rodapé de novo;
  3. dar `flush()`.
- Em qualquer instante o arquivo é XML válido ou, no pior caso, perde só o último ponto.
- A formatação das strings (cabeçalho, ponto, troca de segmento, rodapé, nome do arquivo) é pura e testada no host.

### Falhas

- SD ausente ou erro de escrita: LOGW, `sdOk`/`trackRecording` = false, e tentar remontar a cada 30 s.
  - Ao voltar, abre um **novo** arquivo. Não tenta reabrir o anterior.
- Os pontos continuam indo para a trilha em PSRAM mesmo sem SD.

### Tela GPS

Ganha uma linha: `Trilha: gravando (N pts)` / `Trilha: sem cartão` / `Trilha: aguardando fix`.

## SD e SPI

- O SD fica no mesmo barramento (FSPI, pinos 11/12/13, CS 21) que o TFT.
- Todo acesso ao SD passa por `spiLock()`/`spiUnlock()`; o mutex é recursivo e já existe.
- **Fica para o builder:** fazer `SD.begin()` usar o mesmo `SPIClass` e pinos que o TFT_eSPI sem um reconfigurar o outro. Isso depende da versão e do hardware, e o resultado vai para "a validar no hardware".
- Frequência do SD: começar em 20 MHz.

## `tools/prepare_tiles.py`

- Python 3 + Pillow: `prepare_tiles.py ENTRADA SAIDA [--quality 80]`.
- A entrada é uma árvore `z/x/y.(png|jpg|jpeg)`, o formato "OSMTracker tile storage"/"OSMdroid" do MOBAC.
- A saída é `SAIDA/tiles/z/x/y.jpg`: RGB, 256×256, **baseline** (`progressive=False`), sem metadados.
- Pula os tiles já atualizados, comparando mtime.
- Relatório por zoom: quantidade, faixa x/y, buracos dentro da faixa e MB totais.
- Avisa sobre tiles que não são 256×256 e redimensiona.
- Teste mínimo com pytest em `tools/`: converte um PNG e um JPEG progressivo gerados no teste e confere que a saída é baseline. Usa Pillow e não tem dependência de rede.
- A doc explica como baixar com o MOBAC: escolher uma fonte que permita uso offline, a área e os zooms (sugestão 10–16), e o formato de saída. Também dá uma estimativa de espaço.

## Testes

- **Host (`pio test -e native`):**
  - projeção;
  - tiles visíveis e offsets;
  - ponto para tela;
  - seta na borda;
  - LRU com entradas negativas;
  - navegação entre níveis de zoom presentes;
  - `TrackPolicy`;
  - formatação GPX e nome de arquivo;
  - dizimação da trilha.
- **Firmware:** `pio run -e esp32s3` compila sem warnings novos.
- **PC:** pytest do `prepare_tiles.py`.
- **No hardware (sem hardware ainda):** cartão com tiles, abrir o Mapa, girar o zoom, conferir marcadores e seta, andar e ver a trilha. Também puxar a bateria no meio da gravação e abrir o GPX num visualizador.

## Atualizações na spec principal

- Viewport do mapa: 320×240 (paisagem).
- Trilha da sessão atual desenhada no mapa.
- A task `sd` entra na tabela de tasks.

## Esclarecimentos (revisão do builder; decisões minhas)

- **Memória em PSRAM:** o cache é separado do quadro.
  - O TJpg_Decoder decodifica direto para o slot RGB565 do cache, pelo callback de blocos.
  - A UI copia (blit) do slot para o sprite `frame()`.
  - Orçamento aproximado, folgado nos 8 MB de PSRAM: quadro 150 KB + cache 3 MB + trilha 160 KB + buffer de arquivo 64 KB.
- **Quem decide o ponto:** a `TrackPolicy` roda na task de rádio, junto do `gpsPoll`, que já está lá.
  - `gps.cpp` passa a guardar também a hora UTC (`utcTime`, ao lado de `localTime`). O ponto é montado a partir disso.
  - Os 20 m e os 10 s do GPX contam a partir do último ponto **enfileirado com sucesso**.
  - O filtro de 20 m da trilha em PSRAM conta a partir do último ponto da trilha.
- **SD no SPI:**
  - MISO 13, MOSI 11 e SCK 12 são compartilhados por TFT, SD e CC1101.
  - No boot, **todos os CS (TFT 10, SD 21, CC1101 47) vão para HIGH** antes de iniciar qualquer periférico.
  - O SD usa o mesmo `SPIClass` do TFT_eSPI. O TFT_eSPI usa transações (`SUPPORT_TRANSACTIONS`), então cada um fixa a sua frequência por transação.
  - Haverá o build flag `-DENABLE_SD=1`; com 0, o SD é desligado, para isolar problemas no hardware.
- **Tempo de SPI da task de SD:** escrever um ponto e dar `flush()` sob `spiLock()` pode levar dezenas de ms, raramente ~100 ms.
  - Isso só atrasa a UI, nunca o rádio, e acontece a cada ≥ 10 s. Está aceito.
  - Não há escrita em lote.
- **Mapa sem fix com posição salva:** centra na posição salva, fixa, com o marcador "última posição". Volta a seguir quando o fix voltar.
- **Sem hora válida:** sem hora não se cria o arquivo GPX. Os pontos ainda vão para a trilha em PSRAM, e a tela GPS mostra `Trilha: aguardando hora`.
- **Loop da UI:** a tela Mapa marca `gDirty` quando um tile termina de carregar ou quando ainda há tiles pendentes. Assim decodifica 1 por iteração sem mudar o tempo de leitura da entrada.

## Deixado ao builder

- Cores e tamanhos exatos dos marcadores.
- Comprimento da barra de escala.
- Capacidade final do cache e da trilha.
- Tamanho da fila.
- Nomes dos módulos e onde ficam os novos campos de estado.
- Limite de tamanho do arquivo de tile.
- Texto exato das mensagens.
- Como integrar `SD.begin` com o SPI do TFT_eSPI.

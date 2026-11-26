# Etapa 4 — Mapa offline, trilha GPX e SD — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tela Mapa com tiles do SD, marcadores e trilha da sessão; GPX à prova de corte gravado por uma task de SD; script de preparação de tiles no PC.

**Architecture:** A lógica pura vai para `lib/map/` (projeção, tiles visíveis, seta de borda, níveis de zoom, LRU) e `lib/track/` (política de pontos, buffer de trilha, formatação GPX), com testes no host. A glue em `src/` cobre:
- `sd_task`: monta o SD, grava o GPX e guarda a trilha em PSRAM;
- `map_view`: lê os tiles sob `spiLock`, decodifica fora do lock e desenha;
- ganchos no rádio, no GPS e na UI.

**Tech Stack:** ESP32-S3, Arduino-ESP32 2.0.x (espressif32@6.9.0), FreeRTOS, TFT_eSPI, TJpg_Decoder, SD (FS/FATFS), Unity (`pio test -e native`), Python 3 + Pillow + pytest (script).

**Spec:** `docs/superpowers/specs/2026-11-25-etapa4-mapa-gpx-design.md` (inclui "Esclarecimentos"). Spec principal: `docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md`.

## Global Constraints

- Commits semânticos em português, **sem** `Co-Authored-By` nem "Generated with Claude Code".
- Lógica pura em `lib/` sem Arduino; precisa compilar no env `native` (`-std=gnu++17 -Wall`).
- **A task de rádio nunca toca o SD nem chama `spiLock()`.**
- A decodificação do JPEG nunca acontece com o `spiLock()` segurado.
- Viewport 320×240 (`SCREEN_W`/`SCREEN_H` de `display.h`). Tiles de 256×256 em `/tiles/{z}/{x}/{y}.jpg`.
- GPX: ponto a cada 10 s **ou** a cada 20 m. Rodapé fixo `</trkseg></trk></gpx>\n` reescrito a cada ponto, com `flush()` por ponto. O nome usa a hora local `/tracks/AAAAMMDD_HHMMSS.gpx` (sufixo `_2`, `_3`… em colisão); `<time>` em UTC com `Z`.
- Trilha em PSRAM: 20 000 pontos `float`, filtro de 20 m, dizimação 1 a cada 2 ao encher.
- Cache: 24 slots RGB565 de 128 KB, mais entradas negativas.
- Tile acima de 64 KB = inválido.
- Zoom inicial: o maior nível presente ≤ 16; senão, o menor presente.
- Remontar o SD a cada 30 s após falha. Ao voltar, abrir arquivo **novo**.
- Build flag `ENABLE_SD` (padrão 1).
- SD a 20 MHz no mesmo `SPIClass` do TFT_eSPI (`TFT_eSPI::getSPIinstance()`).
- Todos os CS (10, 21, 47) vão para HIGH antes de iniciar qualquer periférico SPI.

## Review Focus

1. **Coordenadas perto do antimeridiano ou do polo** (lon ±180, lat além de ±85,05°): `visibleTiles` deve fazer wrap em x e pular y fora de `[0, 2^z)`, sem índice negativo nem crash.
2. **Perda e volta do fix com o enfileiramento falhando:** o `newSegment` não pode se perder se o primeiro ponto depois da volta for descartado pela fila cheia.
3. **Dizimação preservando o início de segmento:** depois de dizimar, nenhuma linha pode ligar dois segmentos (um túnel não pode virar uma reta falsa).
4. **Cartão removido no meio da sessão:** a escrita falha → `sdOk = false` → remonta → arquivo novo. Nada de escrever no `File` antigo nem travar a task.
5. **Tile inválido** (progressivo, PNG renomeado, truncado, > 64 KB): vira entrada negativa e não é relido a cada quadro.

Cada linha tem teste no task dono (1 → Task 1, 2 → Task 3, 3 → Task 3, 5 → Task 2). A linha 4 é glue e fica coberta pela revisão do Task 5.

---

### Task 1: Matemática do mapa (`lib/map/map_math`)

**Files:**
- Create: `lib/map/map_math.h`, `lib/map/map_math.cpp`
- Test: `test/test_map_math/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct PixelPos { double x, y; };                       // pixel global no zoom z
  PixelPos latLonToPixel(double lat, double lon, uint8_t z);  // mundo = 256·2^z; lat limitada a ±85.05112878
  double metersPerPixel(double lat, uint8_t z);           // 156543.03392·cos(lat)/2^z
  struct TileRef { uint8_t z; uint32_t x, y; int16_t sx, sy; };  // sx,sy = canto sup. esq. no quadro
  uint8_t visibleTiles(const PixelPos& center, uint8_t z, int16_t w, int16_t h,
                       TileRef* out, uint8_t max);        // retorna a quantidade
  struct ScreenPt { int32_t x, y; };
  ScreenPt toScreen(const PixelPos& p, const PixelPos& center, int16_t w, int16_t h);  // centro em (w/2,h/2), arredonda
  bool onScreen(ScreenPt p, int16_t w, int16_t h, int16_t margin);  // dentro de [margin, w-margin)×[margin, h-margin)
  struct EdgeArrow { int16_t x, y; double angleDeg; };    // 0 = para cima, sentido horário
  EdgeArrow edgeArrow(ScreenPt target, int16_t w, int16_t h, int16_t inset);
  class ZoomLevels {                                      // conjunto de níveis 0..19 presentes no SD
   public:
    void add(uint8_t z);  bool has(uint8_t z) const;  bool empty() const;  uint8_t count() const;
    uint8_t initial(uint8_t pref = 16) const;             // maior presente ≤ pref; senão, o menor presente
    uint8_t step(uint8_t cur, int8_t dir) const;          // próximo presente na direção (dir>0 = mais zoom); trava nas pontas
  };
  ```
- Regras de `visibleTiles`:
  - As colunas vão de `floor((cx - w/2)/256)` a `floor((cx + w/2 - 1)/256)`; as linhas, do mesmo jeito com `h`.
  - x faz wrap mod `2^z`; linhas fora de `[0, 2^z)` são puladas.
  - `sx = col·256 - (cx - w/2)` usa a coluna **sem** wrap; sy do mesmo jeito.
- `edgeArrow`: interseção do raio centro→alvo com o retângulo encolhido por `inset`. O ângulo é `atan2(dx, -dy)` em graus, normalizado para [0, 360).

- [ ] **Step 1: Write the failing tests** em `test/test_map_math/test_main.cpp`:
  - `test_pixel_origin`:
    - `latLonToPixel(0,0,0)` ≈ (128,128) ±1e-6;
    - `latLonToPixel(0,0,1)` ≈ (256,256);
    - `latLonToPixel(0,180,0).x` ≈ 256.
  - `test_pixel_sao_paulo_z15`: `latLonToPixel(-23.55052,-46.63331,15)` ≈ (3107669.12, 4759209.66) ±0.5. O tile é (12139, 18590) (`x/256`, `y/256`).
  - `test_pixel_lat_clamped`: lat 89 e lat 85.05112878 dão o mesmo y (≈ 0 em z0, ±1e-3).
  - `test_mpp_sao_paulo`: `metersPerPixel(-23.55052,15)` ≈ 4.3794 ±1e-3.
  - `test_visible_aligned_center`: centro (512,512), z2, 320×240.
    - Retorna 4 tiles: (1,1), (2,1), (1,2), (2,2).
    - O tile (1,1) tem sx=-96, sy=-136.
  - `test_visible_three_columns`: centro (384,384), z2, 320×240 → 3 tiles x=0,1,2 em y=1. O tile x=0 tem sx=-224.
  - `test_visible_wraps_x`: centro (10,384), z2. A coluna -1 vira x=3 com sx=-106. São 2 colunas no total.
  - `test_visible_skips_rows_outside`: centro (512,10), z2 → só a linha 0 (2 tiles).
  - `test_visible_respects_max`: com `max=2` retorna 2.
  - `test_to_screen_center`: o ponto igual ao centro → (160,120). O ponto +10,-5 → (170,115).
  - `test_on_screen_margin`: (5,5) com margin 8 → false; (160,120) → true.
  - `test_edge_arrow_up`: alvo (160,-1000), inset 12 → (160,12), ângulo 0.
  - `test_edge_arrow_right`: alvo (5000,120), inset 12 → (307,120), ângulo 90 (x = w-1-inset).
  - `test_edge_arrow_down_left`: alvo (-1000,1120).
    - O ângulo está em (180,270).
    - O ponto fica numa das bordas encolhidas: x == 12 ou y == 227.
  - `test_zoom_initial_and_step`:
    - Com {10,12,16,18}: `initial()` = 16; `step(16,+1)` = 18; `step(18,+1)` = 18; `step(16,-1)` = 12; `step(10,-1)` = 10.
    - Com {17,18}: `initial()` = 17.
    - Vazio: `empty()` é true.

- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_map_math`
  Expected: FAIL (erro de compilação: `map_math.h` não existe)

- [ ] **Step 3: Implement** `lib/map/map_math.h/.cpp` com as assinaturas acima. ZoomLevels usa uma máscara `uint32_t`.

- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native -f test_map_math`
  Expected: PASS (todos os testes)

- [ ] **Step 5: Commit**
  ```bash
  git add lib/map/map_math.* test/test_map_math
  git commit -m "feat(mapa): adiciona projeção, tiles visíveis, seta de borda e níveis de zoom"
  ```

---

### Task 2: LRU de tiles (`lib/map/tile_lru`)

**Files:**
- Create: `lib/map/tile_lru.h`, `lib/map/tile_lru.cpp`
- Test: `test/test_tile_lru/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct TileKey { uint8_t z; uint32_t x, y; };
  bool operator==(const TileKey& a, const TileKey& b);
  class TileLru {
   public:
    static constexpr uint8_t kMaxSlots = 32, kMissing = 64;
    explicit TileLru(uint8_t slots);           // slots ≤ kMaxSlots
    int16_t find(const TileKey& k);            // índice do slot pronto ou -1; marca como recente
    int16_t acquire(const TileKey& k);         // slot a preencher: livre ou o menos recente (despejado); grava a chave
    void release(int16_t slot);                // devolve o slot (decodificação falhou); deixa de ser encontrado
    void markMissing(const TileKey& k);        // entrada negativa (anel de kMissing, a mais antiga sai)
    bool isMissing(const TileKey& k) const;
    void clear();                              // esvazia slots e negativas
  };
  ```

- [ ] **Step 1: Write the failing tests:**
  - `test_find_empty_returns_minus_one`.
  - `test_acquire_then_find`: `acquire(A)` devolve s ≥ 0, e `find(A)` == s.
  - `test_fills_free_slots_first`: com 3 slots, A, B e C ficam em slots distintos.
  - `test_evicts_least_recent`:
    1. Com 2 slots: `acquire(A)`, `acquire(B)`, `find(A)`, `acquire(C)`.
    2. Então C pega o slot de B: `find(B)` == -1 e `find(A)` ≥ 0.
  - `test_release_frees_slot`: depois de `release(s)`, `find(A)` == -1, e o próximo `acquire` reutiliza s.
  - `test_missing_entries`: `markMissing(A)` → `isMissing(A)` true e `isMissing(B)` false.
  - `test_missing_ring_drops_oldest`: marcar 65 chaves distintas → a primeira não está mais em missing e a última está.
  - `test_clear`: depois de `clear()`, nada é encontrado e nada é missing.
  - `test_keys_differ_by_zoom`: as chaves (15,1,1) e (16,1,1) não colidem.

- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_tile_lru`
  Expected: FAIL (não compila)

- [ ] **Step 3: Implement** com um contador de uso (`uint32_t tick`) por slot e busca linear. A quantidade de slots é pequena.

- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native -f test_tile_lru`
  Expected: PASS

- [ ] **Step 5: Commit**
  ```bash
  git add lib/map/tile_lru.* test/test_tile_lru
  git commit -m "feat(mapa): adiciona cache LRU de tiles com entradas negativas"
  ```

---

### Task 3: Trilha e GPX (`lib/track/`)

**Files:**
- Create: `lib/track/track_logic.h/.cpp`, `lib/track/gpx_format.h/.cpp`
- Test: `test/test_track/test_main.cpp`, `test/test_gpx/test_main.cpp`

**Interfaces:**
- Consumes: `DateTime` (`lib/geo/gps_logic.h`), `distanceM` (`lib/geo/geo.h`).
- Produces:
  ```cpp
  class TrackPolicy {
   public:
    static constexpr uint32_t kPeriodMs = 10000;
    static constexpr double kMoveM = 20.0;
    // true = enfileirar um ponto agora. newSegment = primeiro ponto desde o boot ou desde a volta do fix.
    bool due(uint32_t now, bool hasFix, double lat, double lon, bool& newSegment);
    void accepted(uint32_t now, double lat, double lon);  // só depois de enfileirar com sucesso
  };
  struct TrackPt { float lat, lon; bool segStart; };
  class TrackBuffer {
   public:
    static constexpr double kMinStepM = 20.0;
    TrackBuffer(TrackPt* storage, uint32_t capacity);
    void add(double lat, double lon, bool newSegment);  // ignora se < 20 m do último e !newSegment
    uint32_t size() const;
    const TrackPt& at(uint32_t i) const;
    void clear();
  };
  extern const char kGpxFooter[];  // "</trkseg></trk></gpx>\n"
  size_t gpxHeader(char* out, size_t cap);
  size_t gpxPoint(char* out, size_t cap, double lat, double lon, float ele,
                  const DateTime& utc, bool breakSegment);
  void gpxFileName(char* out, size_t cap, const DateTime& local, uint8_t suffix);  // suffix<2 = sem sufixo
  ```
- **`TrackPolicy`:**
  - Sem fix: retorna false e arma o novo segmento.
  - Com fix e nada aceito ainda no segmento: true, com `newSegment = true`.
  - Depois disso: true se `now - últimoAceito ≥ 10000` ou `distanceM(último, atual) ≥ 20`.
  - `newSegment` continua true até que `accepted()` seja chamado.
- **`TrackBuffer.add` com o buffer cheio:**
  - Mantém os índices pares e compacta.
  - Se um índice ímpar descartado tinha `segStart`, o ponto mantido seguinte recebe `segStart`.
  - Depois acrescenta o novo ponto.
  - O primeiro ponto de todos sempre tem `segStart = true`.
- **Strings exatas:**
  - `gpxHeader`: `<?xml version="1.0" encoding="UTF-8"?>\n<gpx version="1.1" creator="GPS_all_in_one" xmlns="http://www.topografix.com/GPX/1/1">\n<trk><trkseg>\n`
  - `gpxPoint`: o prefixo opcional `</trkseg><trkseg>\n` (se `breakSegment`), seguido de `<trkpt lat="-23.550520" lon="-46.633310"><ele>760.0</ele><time>2026-11-25T17:01:05Z</time></trkpt>\n`.
  - Os dois retornam o comprimento escrito, ou 0 se `cap` não couber.
  - `gpxFileName`: `/tracks/20261125_140105.gpx`, `/tracks/20261125_140105_2.gpx`.

- [ ] **Step 1: Write the failing tests** em `test/test_track/test_main.cpp`:
  - `test_policy_no_fix_never_due`.
  - `test_policy_first_fix_due_new_segment`.
  - `test_policy_period`: aceito em t=0 → em t=9999 parado não está pendente; em t=10000 está, com `newSegment = false`.
  - `test_policy_distance`: aceito em t=0 → em t=2000 a 25 m está pendente.
  - `test_policy_fix_loss_starts_segment`: aceito → sem fix → fix volta → pendente com `newSegment = true`.
  - `test_policy_new_segment_survives_rejected_enqueue` (Review Focus 2):
    1. Depois da volta do fix, `due` → `newSegment = true` e **não** se chama `accepted`.
    2. No `due` seguinte (t+100), ainda vem `newSegment = true`.
  - `test_policy_distance_from_last_accepted`:
    1. Aceito em P0; `due` com P1 a 15 m sem aceitar.
    2. P2 a 25 m de P0 e 10 m de P1 → pendente.
  - `test_buffer_filters_close_points`: 3 pontos a 5 m um do outro → `size()` 1.
  - `test_buffer_new_segment_kept_even_if_close`: segundo ponto a 5 m com `newSegment` → `size()` 2 e `at(1).segStart`.
  - `test_buffer_decimates_when_full`:
    1. Capacidade 8, 9 pontos a 30 m um do outro.
    2. Depois do 9º: `size()` == 5 (4 pares mantidos + o novo).
    3. `at(0)` é o primeiro ponto e `at(4)` é o 9º.
  - `test_buffer_decimation_keeps_segment_start` (Review Focus 3):
    1. Capacidade 8; o ponto de índice 3 tem `segStart`.
    2. Depois de dizimar, existe exatamente o mesmo número de `segStart` = true que antes.
    3. O ponto mantido no lugar do índice 4 tem `segStart`.

  E em `test/test_gpx/test_main.cpp`:
  - `test_header_exact`: igual à string exata acima.
  - `test_point_exact`: ponto (-23.55052, -46.63331, 760.0f, 2026-11-25 17:01:05, false) igual à string exata.
  - `test_point_with_break`: a string começa com `</trkseg><trkseg>\n`.
  - `test_point_too_small_returns_zero`: `cap` 20 → 0.
  - `test_footer_exact`.
  - `test_filename`: sem sufixo e com o sufixo 2, com zeros à esquerda (01/02/2027 03:04:05 → `/tracks/20270201_030405.gpx`).

- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_track -f test_gpx`
  Expected: FAIL (não compila)

- [ ] **Step 3: Implement** os quatro arquivos com as assinaturas e as regras acima.

- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native`
  Expected: PASS na suíte inteira (167 anteriores + novos)

- [ ] **Step 5: Commit**
  ```bash
  git add lib/track test/test_track test/test_gpx
  git commit -m "feat(trilha): adiciona política de pontos, buffer da trilha e formatação GPX"
  ```

---

### Task 4: Script de preparação de tiles e guia do SD

**Files:**
- Create: `tools/prepare_tiles.py`, `tools/test_prepare_tiles.py`, `tools/requirements.txt` (`Pillow`, `pytest`), `docs/mapas-offline.md`

**Interfaces:**
- Produces (CLI): `python3 tools/prepare_tiles.py ENTRADA SAIDA [--quality 80]`.
  - Lê `ENTRADA/z/x/y.(png|jpg|jpeg)` e escreve `SAIDA/tiles/z/x/y.jpg`.
  - A saída é RGB, 256×256 (com LANCZOS e um aviso, se o tile vier de outro tamanho), `progressive=False`, `optimize=True` e sem EXIF.
  - Pula a saída com mtime ≥ ao da entrada.
  - Relatório por zoom: `z15: 1234 tiles, x 12100-12180, y 18550-18620, buracos 3`. No fim: `total: N tiles, X.X MB`.
  - Funções importáveis: `convert_tile(src: Path, dst: Path, quality: int) -> bool`, `prepare(src_root: Path, dst_root: Path, quality: int) -> dict[int, dict]` (por zoom: `count`, `xmin`, `xmax`, `ymin`, `ymax`, `holes`).

- [ ] **Step 1: Write the failing tests** em `tools/test_prepare_tiles.py`, usando `tmp_path` e gerando imagens com Pillow:
  - `test_png_becomes_baseline_jpeg`:
    - A entrada `15/1/2.png` (RGBA) gera a saída `tiles/15/1/2.jpg`.
    - `Image.open(out).format == "JPEG"`, `"progressive" not in img.info` e o tamanho é (256, 256).
  - `test_progressive_jpeg_reencoded_baseline`: a entrada é salva com `progressive=True`; a saída não pode ter `progressive` em `info`.
  - `test_resizes_non_256`: a entrada 512×512 sai 256×256.
  - `test_report_counts_holes`: tiles z10 em (0,0), (1,0) e (0,1); faltando (1,1) → `holes == 1`, `count == 3`.
  - `test_skips_up_to_date`: na segunda execução, o mtime da saída não muda.

- [ ] **Step 2: Run to verify it fails**
  Run: `python3 -m pytest tools/test_prepare_tiles.py -q`
  Expected: FAIL (ImportError: `prepare_tiles` não existe)

- [ ] **Step 3: Implement** `tools/prepare_tiles.py`. Os buracos são contados dentro da caixa xmin..xmax × ymin..ymax de cada zoom.

- [ ] **Step 4: Run to verify it passes**
  Run: `python3 -m pytest tools/test_prepare_tiles.py -q`
  Expected: `5 passed`

- [ ] **Step 5: Escrever `docs/mapas-offline.md`** (em português):
  - Instalar o MOBAC.
  - Escolher uma fonte que permita uso offline. O servidor `tile.openstreetmap.org` proíbe download em massa.
  - Selecionar a área, com os zooms sugeridos 10–16.
  - Formato do atlas: "OSMTracker tile storage".
  - Rodar `pip install -r tools/requirements.txt` e depois `prepare_tiles.py`.
  - Copiar `SAIDA/tiles` para a raiz do SD (FAT32).
  - Estimativa: cerca de 15 KB por tile. Uma cidade de 20×20 km em z10–16 dá alguns milhares de tiles, dezenas de MB.
  - Os arquivos GPX ficam em `/tracks/`.

- [ ] **Step 6: Commit**
  ```bash
  git add tools docs/mapas-offline.md
  git commit -m "feat(tools): adiciona script de preparação de tiles e guia de mapas offline"
  ```

---

### Task 5: SD, task de SD, hora UTC e gravação da trilha

**Files:**
- Create: `src/sd_task.h`, `src/sd_task.cpp`
- Modify:
  - `src/config.h` (`ENABLE_SD`, `SD_SPI_HZ`, `TRACK_CAPACITY`);
  - `src/spi_bus.cpp` (`spiBusInit` leva os CS 10/21/47 para HIGH);
  - `src/display.h/.cpp` (`bool displayReady()`, `SPIClass& displaySpi()`);
  - `src/state.h` (campos novos);
  - `src/gps.cpp` (`utcTime`);
  - `src/radio_task.cpp` (TrackPolicy + enfileirar);
  - `src/main.cpp` (`sdStart()` depois de `uiStart`);
  - `lib/ui_logic/ui_format.h/.cpp` e `test/test_ui_format/test_main.cpp` (`fmtTrackStatus`);
  - `src/ui_screens.cpp` (linha da trilha em `drawGps`).

**Interfaces:**
- Consumes: Task 3 (`TrackPolicy`, `TrackBuffer`, `gpxHeader`, `gpxPoint`, `kGpxFooter`, `gpxFileName`).
- Produces:
  ```cpp
  // sd_task.h
  struct TrackPoint { double lat, lon; float alt; DateTime utc, local; bool newSegment; };
  void sdStart();                          // task "sd", core 0, prio 1, stack 6144; espera displayReady()
  bool trackEnqueue(const TrackPoint& p);  // não bloqueia; false se a fila (16) estiver cheia
  bool sdMounted();                        // thread-safe
  void trailLock(); void trailUnlock();    // mutex próprio da trilha
  const TrackBuffer& trail();              // ler só entre trailLock/trailUnlock
  // state.h (SharedState)
  DateTime utcTime{};
  bool sdOk = false, trackRecording = false;
  uint32_t trackPoints = 0;
  // ui_format.h
  void fmtTrackStatus(char* out, size_t cap, bool sdOk, bool recording, uint32_t points,
                      bool timeValid, bool fix);
  ```
- **`fmtTrackStatus`**, na ordem de prioridade:
  - `!sdOk` → `Trilha: sem cartao`;
  - `recording` → `Trilha: gravando (N pts)`;
  - `!fix` → `Trilha: aguardando fix`;
  - `!timeValid` → `Trilha: aguardando hora`;
  - senão → `Trilha: iniciando`.
- **Comportamento da task de SD:**
  - **Montagem:**
    - Espera `displayReady()`.
    - Sob `spiLock()`: `SD.begin(PIN_SD_CS, displaySpi(), SD_SPI_HZ, "/sd", 5)` e depois `SD.mkdir("/tracks")`.
    - Atualiza `sdOk`.
    - Com `ENABLE_SD == 0`, nunca monta.
  - **Trilha em PSRAM:** `ps_malloc(TRACK_CAPACITY * sizeof(TrackPt))`, com `TRACK_CAPACITY` 20000.
  - **Loop:** `xQueueReceive` com timeout de 1 s. Para cada ponto:
    1. Primeiro `trail().add(...)` sob `trailLock`. Isso acontece sempre, mesmo sem SD.
    2. Depois, se `sdOk` e o ponto tem hora válida (ano ≥ 2024):
       - Abre o arquivo se não houver um aberto. O nome vem de `gpxFileName(local)`, com o sufixo incrementado até `!SD.exists`. Escreve o header + footer.
       - Faz `seek(size - strlen(kGpxFooter))`, escreve `gpxPoint(..., breakSegment = newSegment && pontosNoArquivo > 0)` mais o footer, e dá `flush()`.
       - Todo esse I/O acontece sob `spiLock()`.
    3. Se qualquer escrita retornar menos bytes que o pedido:
       - fecha o arquivo e faz `SD.end()`;
       - `sdOk = false`, `trackRecording = false`;
       - LOGW;
       - agenda a remontagem em 30 s.
       - Ao remontar, o próximo ponto abre arquivo novo.
    4. Publica `trackRecording` e `trackPoints` no `SharedState`.
- **Rádio:**
  - Logo após `gpsPoll`, lê sob `stateLock` `gpsFix`, `lat`, `lon`, `altM`, `utcTime`, `localTime` e `timeValid`.
  - Se `policy.due(...)` for true: monta o `TrackPoint` (utc/local zerados se `!timeValid`) e chama `trackEnqueue`. Se der certo, `policy.accepted(...)`; senão, LOGW.
  - O rádio não chama `spiLock()`.
- **GPS:** `s.utcTime = utc` no mesmo bloco que hoje grava `localTime`.

- [ ] **Step 1: Write the failing test** em `test/test_ui_format/test_main.cpp`: `test_track_status_lines`, com as 5 saídas exatas acima, incluindo `Trilha: gravando (42 pts)`.
- [ ] **Step 2: Run to verify it fails**
  Run: `pio test -e native -f test_ui_format`
  Expected: FAIL (`fmtTrackStatus` não declarado)
- [ ] **Step 3: Implement `fmtTrackStatus`.**
- [ ] **Step 4: Run to verify it passes**
  Run: `pio test -e native`
  Expected: PASS (suíte inteira)
- [ ] **Step 5: Implement a glue:**
  - `sd_task`;
  - os campos de estado;
  - o `utcTime` no GPS;
  - o gancho no rádio;
  - os CS no `spiBusInit`;
  - `displayReady`/`displaySpi` (`gTft.getSPIinstance()`; a flag é `std::atomic<bool>` gravada no fim de `displayBegin`);
  - `sdStart()` no `setup()`;
  - a linha `fmtTrackStatus` em `drawGps`, depois da bateria.
- [ ] **Step 6: Compile**
  Run: `pio run -e esp32s3 2>&1 | tail -5`
  Expected: `SUCCESS`, sem warnings novos em `src/`
- [ ] **Step 7: Commit**
  ```bash
  git add src lib/ui_logic test/test_ui_format
  git commit -m "feat(trilha): grava GPX no SD por uma task dedicada e mantém a trilha em PSRAM"
  ```

---

### Task 6: Tela Mapa

**Files:**
- Create: `src/map_view.h`, `src/map_view.cpp`
- Modify:
  - `src/ui.cpp` (Screen::Map: desenho, entrada, `gDirty` enquanto houver tile pendente);
  - `docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md` (viewport 320×240, trilha no mapa, task `sd`).

**Interfaces:**
- Consumes:
  - Task 1 (`latLonToPixel`, `visibleTiles`, `toScreen`, `onScreen`, `edgeArrow`, `metersPerPixel`, `ZoomLevels`);
  - Task 2 (`TileLru`, `TileKey`);
  - Task 5 (`sdMounted`, `trailLock`/`trailUnlock`/`trail`);
  - `fmtDistance` (`ui_format.h`), `spiLock`/`spiUnlock`, `frame()`.
- Produces:
  ```cpp
  void mapInit();                                         // aloca 24 slots RGB565 + buffer de arquivo de 64 KB em PSRAM
  void mapZoom(int8_t detents);                           // horário (+) aproxima
  bool mapDraw(TFT_eSprite& f, const SharedState& s);     // desenha o mapa; true = ainda há tile pendente
  ```
- **Níveis de zoom:**
  - Se ainda não foi feito e `sdMounted()`, `mapDraw` escaneia `/tiles` sob `spiLock` (diretórios com nome 0–19 → `ZoomLevels`).
  - Se o SD sumir e voltar, reescaneia na próxima abertura do Mapa.
  - O zoom corrente começa em `initial()`.
- **Centro:** minha posição (`myPosKnown`) → amigo (`peerKnown`) → nenhum, com o texto central "Sem posicao - aguardando GPS".
- **Tiles:** para cada `TileRef` visível:
  - `find` → blit do slot para o sprite com `pushImage` no sprite, cortado pelas bordas.
  - `isMissing` → retângulo cinza #404040 com "sem mapa".
  - Senão → cinza "carregando" e marca pendente.
  - **No máximo 1 carga por chamada de `mapDraw`:**
    1. sob `spiLock`, abre `/tiles/z/x/y.jpg`, rejeita > 64 KB e lê para o buffer;
    2. solta o lock;
    3. `TJpgDec.drawJpg` da memória com um callback que copia blocos para o slot de `acquire()`. O slot fica como "atual" numa variável estática;
    4. com erro: `release` + `markMissing`.
  - O `TJpgDec` é configurado uma vez: `setJpgScale(1)`, `setSwapBytes(true)`, e o callback escreve no slot e não no TFT.
- **Sem SD ou sem níveis:** fundo #202428 com "Sem cartao SD" ou "Sem mapas no cartao" no topo. A escala usa o zoom 15. A trilha e os marcadores são desenhados mesmo assim.
- **Sobreposições, nesta ordem:**
  1. a trilha (linhas de 2 px entre pontos consecutivos sem `segStart`, copiando sob `trailLock` só a projeção; os pontos fora da tela são ignorados pelo clipping do sprite);
  2. o amigo (círculo laranja r=6 + ID) ou, fora da tela (`onScreen` margin 8), `edgeArrow` inset 14 com a distância (`fmtDistance`);
  3. eu (triângulo de 10 px no rumo se `courseKnown`, vazado se `courseStale`; círculo azul r=6 se não; cinza + "ultima posicao" se `!gpsFix`);
  4. o canto inferior esquerdo com `z15` e a barra de escala (comprimento redondo 50/100/200/500 m/1/2/5 km que caiba em ≤ 80 px, por `metersPerPixel`).
- **`ui.cpp`:**
  - O mapa ocupa a tela inteira, sem item Voltar. Em `onScreenInput`, `Screen::Map` é tratado antes do cursor genérico: a rotação (`d`) chama `mapZoom(d)` e marca `gDirty`, e o clique faz `pop()`.
  - No `draw`, `case Screen::Map: gMapPending = mapDraw(f, s);`. No loop, `if (top()==Screen::Map && gMapPending) gDirty = true;`.
  - Com a tela Mapa aberta, um redesenho por segundo já existe (`kRefreshMs`).
  - `mapInit()` é chamado no `uiTask` depois de `displayBegin`.

- [ ] **Step 1: Implement** `map_view.h/.cpp` e a integração em `ui.cpp` conforme acima.
- [ ] **Step 2: Compile**
  Run: `pio run -e esp32s3 2>&1 | tail -5`
  Expected: `SUCCESS`, sem warnings novos em `src/`
- [ ] **Step 3: Run the host suite**
  Run: `pio test -e native`
  Expected: PASS (suíte inteira)
- [ ] **Step 4: Atualizar a spec principal:**
  - Seção Mapa: viewport 320×240 e a trilha da sessão desenhada.
  - Tabela de tasks: entra `sd` (core 0, prio 1, GPX e trilha).
- [ ] **Step 5: Commit**
  ```bash
  git add src docs/superpowers/specs/2026-11-25-dispositivo-gps-lora-design.md
  git commit -m "feat(mapa): adiciona tela de mapa com tiles do SD, marcadores e trilha"
  ```

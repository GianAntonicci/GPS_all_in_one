# Mapas offline no cartão SD

O aparelho lê tiles `/tiles/{z}/{x}/{y}.jpg` (256×256, padrão "slippy map"/OSM) do cartão
microSD e grava as trilhas em `/tracks/AAAAMMDD_HHMMSS.gpx`.

## 1. Baixar a região (no PC)

1. Instale o [MOBAC](https://mobac.sourceforge.io/) (Mobile Atlas Creator; precisa de Java).
2. Escolha uma **fonte de mapa que permita uso offline**. Leia os termos dela.
   - O servidor `tile.openstreetmap.org` **proíbe download em massa**. Não use.
   - Servem, por exemplo, provedores com chave e plano que permita cache offline, ou um servidor
     de tiles próprio.
3. Selecione a área no mapa e marque os zooms. A sugestão é **10 a 16**:
   - 10–12 servem para se localizar na região;
   - 14–16 servem para trilhas e ruas.
4. Formato do atlas: **"OSMTracker tile storage"**. Ele gera `z/x/y.png` ou `.jpg`.
5. Crie o atlas. Ele fica numa pasta, por exemplo `~/mobac/atlases/MinhaRegiao`.

## 2. Preparar para o aparelho

O firmware só lê **JPEG baseline**. PNG e JPEG progressivo aparecem como "sem mapa". O script
converte tudo e confere o resultado:

```bash
pip install -r tools/requirements.txt
python3 tools/prepare_tiles.py ~/mobac/atlases/MinhaRegiao ~/sd-pronto
```

Saída típica:

```
z14: 420 tiles, x 6068-6088, y 9290-9309, buracos 0
z15: 1640 tiles, x 12136-12176, y 18580-18619, buracos 0
total: 2060 tiles, 31.2 MB
```

- Os **buracos** são tiles que faltam dentro do retângulo de um zoom. Normalmente é um download
  incompleto; rode o MOBAC de novo.
- Rodar o script de novo só converte o que mudou.
- Para rodar os testes do script: `python3 -m pytest tools/test_prepare_tiles.py -q`.

## 3. Copiar para o cartão

- Formate o cartão em **FAT32**.
- Copie a pasta `~/sd-pronto/tiles` para a **raiz** do cartão. O caminho fica `/tiles/15/12139/18590.jpg`.
- A pasta `/tracks/` é criada pelo aparelho.

**Espaço:** cerca de 15 KB por tile. Cada zoom a mais multiplica por 4 a quantidade de tiles da
mesma área. Uma cidade de 20×20 km em z10–16 dá alguns milhares de tiles, dezenas de MB. Uma
região grande em z16 chega a alguns GB, o que ainda cabe folgado num cartão de 32 GB.

## No aparelho

- Na tela **Mapa**, girar o encoder muda o zoom (horário aproxima). O zoom só anda pelos níveis
  presentes no cartão.
- O clique volta ao menu. Segurar 2 s abre o SOS, como em qualquer tela.
- O mapa fica centrado em você. Sem fix, centra na última posição salva ou no amigo.
- Quando o amigo está fora da tela, aparece uma seta na borda com a distância.
- A linha da trilha mostra o caminho desta sessão. O arquivo GPX fica em `/tracks/` e abre em
  qualquer visualizador de GPX.

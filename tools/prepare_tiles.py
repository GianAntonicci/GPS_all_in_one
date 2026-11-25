#!/usr/bin/env python3
"""Prepara tiles de mapa para o cartão SD do aparelho.

Lê uma árvore z/x/y.(png|jpg|jpeg) (ex.: exportada pelo MOBAC no formato
"OSMTracker tile storage") e grava SAIDA/tiles/z/x/y.jpg em JPEG *baseline*
256x256 RGB, que é o único formato que o TJpg_Decoder do firmware lê
(JPEG progressivo e PNG aparecem como "sem mapa").

Uso: python3 tools/prepare_tiles.py ENTRADA SAIDA [--quality 80]
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

EXTS = (".png", ".jpg", ".jpeg")
TILE = 256


def convert_tile(src: Path, dst: Path, quality: int) -> bool:
    """Converte um tile. Retorna True se gravou, False se a saída já estava atualizada."""
    if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
        return False
    with Image.open(src) as im:
        if im.mode in ("RGBA", "LA", "P"):
            im = im.convert("RGBA")
            bg = Image.new("RGB", im.size, (255, 255, 255))
            bg.paste(im, mask=im.split()[-1])
            im = bg
        else:
            im = im.convert("RGB")
        if im.size != (TILE, TILE):
            print(f"aviso: {src} tem {im.size[0]}x{im.size[1]}, redimensionando", file=sys.stderr)
            im = im.resize((TILE, TILE), Image.LANCZOS)
        dst.parent.mkdir(parents=True, exist_ok=True)
        tmp = dst.with_suffix(".tmp")
        im.save(tmp, "JPEG", quality=quality, progressive=False, optimize=True)
    tmp.replace(dst)
    return True


def _tiles(src_root: Path):
    for zdir in src_root.iterdir():
        if not (zdir.is_dir() and zdir.name.isdigit()):
            continue
        for xdir in zdir.iterdir():
            if not (xdir.is_dir() and xdir.name.isdigit()):
                continue
            for f in xdir.iterdir():
                if f.suffix.lower() in EXTS and f.stem.isdigit():
                    yield int(zdir.name), int(xdir.name), int(f.stem), f


def prepare(src_root: Path, dst_root: Path, quality: int) -> dict:
    """Converte a árvore inteira. Retorna {z: {count, xmin, xmax, ymin, ymax, holes}}."""
    seen: dict[int, set] = {}
    for z, x, y, f in _tiles(Path(src_root)):
        dst = Path(dst_root) / "tiles" / str(z) / str(x) / f"{y}.jpg"
        try:
            convert_tile(f, dst, quality)
        except OSError as e:
            print(f"erro: {f}: {e}", file=sys.stderr)
            continue
        seen.setdefault(z, set()).add((x, y))
    report = {}
    for z, pts in sorted(seen.items()):
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        xmin, xmax, ymin, ymax = min(xs), max(xs), min(ys), max(ys)
        box = (xmax - xmin + 1) * (ymax - ymin + 1)
        report[z] = dict(count=len(pts), xmin=xmin, xmax=xmax, ymin=ymin, ymax=ymax,
                         holes=box - len(pts))
    return report


def _size_mb(root: Path) -> float:
    return sum(f.stat().st_size for f in root.rglob("*.jpg")) / 1e6


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("entrada", type=Path)
    ap.add_argument("saida", type=Path)
    ap.add_argument("--quality", type=int, default=80)
    a = ap.parse_args(argv)
    if not a.entrada.is_dir():
        print(f"entrada não existe: {a.entrada}", file=sys.stderr)
        return 1
    rep = prepare(a.entrada, a.saida, a.quality)
    if not rep:
        print("nenhum tile encontrado (esperado ENTRADA/z/x/y.png ou .jpg)", file=sys.stderr)
        return 1
    total = 0
    for z, r in rep.items():
        total += r["count"]
        print(f"z{z}: {r['count']} tiles, x {r['xmin']}-{r['xmax']}, y {r['ymin']}-{r['ymax']}, "
              f"buracos {r['holes']}")
    print(f"total: {total} tiles, {_size_mb(a.saida / 'tiles'):.1f} MB")
    print(f"copie a pasta {a.saida / 'tiles'} para a raiz do cartão SD")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Offline view of emitted GE geometry, NOT the emulator's framebuffer.

Uses opt-in GE_preview records from REPOPS_GE_PREVIEW_TRACE=1. This never
replays GP0, changes emulation, or supplies results to the GE adapter.
Requires Pillow only for this diagnostic; the native executable does not.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw


def xy(word: int) -> tuple[int, int]:
    def signed_half(value: int) -> int:
        return ((value & 0xffff) ^ 0x8000) - 0x8000
    return signed_half(word), signed_half(word >> 16)


def read_frames(trace: Path) -> tuple[dict, int]:
    frames: dict[int, list] = defaultdict(list)
    current = None
    incomplete = 0
    with trace.open(encoding="utf-8") as source:
        for line in source:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                incomplete += 1
                continue
            if event.get("kind") != "GE_preview":
                continue
            name, value = event["name"], event["value"]
            if name == "polygon_begin":
                if current is not None:
                    incomplete += 1
                current = {"body": event["address"], "count": value, "vertices": []}
            elif current is None:
                continue
            elif name == "frame":
                current["frame"] = event["address"]
            elif name == "vertex_xy":
                current["vertices"].append(xy(value))
            elif name == "polygon_end":
                required = {"frame", "color_word", "drawing_offset", "scissor_min", "scissor_max"}
                valid = (event["address"] == current["body"] and
                         current["count"] in (3, 4) and
                         len(current["vertices"]) == current["count"] and required <= current.keys())
                if valid:
                    frames[current["frame"]].append(current)
                else:
                    incomplete += 1
                current = None
            else:
                current[name] = value
    return dict(frames), incomplete + (current is not None)


def render(polygons: list, frame: int, target: Path, wire: bool) -> None:
    # Full PS1 coordinate canvas: no guessed camera, display crop or stretching.
    width, height, header = 1024, 512, 80
    canvas = Image.new("RGB", (width, height), (0, 0, 0))
    for poly in polygons:
        dx, dy = xy(poly["drawing_offset"])
        points = [(x + dx, y + dy) for x, y in poly["vertices"]]
        sx, sy = xy(poly["scissor_min"])
        ex, ey = xy(poly["scissor_max"])
        left, top, right, bottom = max(0, sx), max(0, sy), min(width, ex + 1), min(height, ey + 1)
        if left >= right or top >= bottom:
            continue
        region = canvas.crop((left, top, right, bottom))
        painter = ImageDraw.Draw(region)
        color = poly["color_word"] & 0xffffff
        rgb = (color & 255, (color >> 8) & 255, (color >> 16) & 255)
        if wire:
            rgb = (120, 200, 225)  # Diagnostic edges, explicitly not source pixel colors.
        for indices in ((0, 1, 2), (3, 1, 2))[:len(points) - 2]:
            triangle = [(points[i][0] - left, points[i][1] - top) for i in indices]
            if wire:
                painter.line(triangle + triangle[:1], fill=rgb, width=1)
            else:
                painter.polygon(triangle, fill=rgb)
        canvas.paste(region, (left, top))
    output = Image.new("RGB", (width, height + header), (24, 27, 32))
    output.paste(canvas, (0, header))
    draw = ImageDraw.Draw(output)
    mode = "MALLA DIAGNOSTICA" if wire else "COLORES DE LOS REGISTROS EMITIDOS"
    draw.text((16, 10), f"RePops | GE: {mode} | frame_counter {frame}", fill="white")
    draw.text((16, 31), f"{len(polygons)} registros de poligonos reales. Vista offline; NO es un framebuffer del emulador.", fill="white")
    draw.text((16, 52), "Sin texturas, mezcla, dithering ni reglas exactas GE. Lienzo invitado 1024 x 512; sin recorte de pantalla.", fill="white")
    output.save(target)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--frame", type=int, help="Default: latest frame with emitted polygons")
    args = parser.parse_args()
    if args.out.exists():
        parser.error("Output directory exists; select a fresh path")
    frames, incomplete = read_frames(args.trace)
    if not frames:
        parser.error("No complete GE_preview polygons; rerun with REPOPS_GE_PREVIEW_TRACE=1")
    frame = max(frames) if args.frame is None else args.frame
    if frame not in frames:
        parser.error(f"Frame not captured; available: {sorted(frames)}")
    args.out.mkdir(parents=True)
    for wire, name in ((False, "geometry.png"), (True, "wireframe.png")):
        render(frames[frame], frame, args.out / name, wire)
    digest = hashlib.sha256()
    with args.trace.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    report = {
        "trace": str(args.trace), "trace_sha256": digest.hexdigest(),
        "selected_frame_counter": frame, "polygons": len(frames[frame]),
        "captured_frame_counts": {str(key): len(value) for key, value in sorted(frames.items())},
        "incomplete_records": incomplete, "emulator_framebuffer": False,
        "scope": "Offline approximation of emitted GE vertices/colors, not GE execution or game screenshot",
        "omitted": ["textures", "blending", "dithering", "exact rasterization", "display scanout", "other GE primitives"],
    }
    (args.out / "provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print("Images:", args.out / "geometry.png", args.out / "wireframe.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

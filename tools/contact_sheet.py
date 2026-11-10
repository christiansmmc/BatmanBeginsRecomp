"""Throwaway: one contact sheet of a run's screenshots, labelled with their time."""
import pathlib
import re
import sys

from PIL import Image, ImageDraw

run = pathlib.Path(sys.argv[1])
columns = int(sys.argv[2]) if len(sys.argv) > 2 else 4
shots = sorted(run.glob("shot_*.png"), key=lambda p: float(re.search(r"shot_(\d+(?:\.\d+)?)", p.name).group(1)))
width, height = 400, 300
rows = (len(shots) + columns - 1) // columns
sheet = Image.new("RGB", (columns * width, rows * height), (40, 0, 40))
for index, path in enumerate(shots):
    with Image.open(path) as image:
        thumb = image.convert("RGB").resize((width - 4, height - 4))
    draw = ImageDraw.Draw(thumb)
    label = re.search(r"shot_(\d+(?:\.\d+)?)", path.name).group(1)
    draw.rectangle((0, 0, 60, 14), fill=(0, 0, 0))
    draw.text((3, 1), f"{label}s", fill=(255, 255, 0))
    sheet.paste(thumb, (index % columns * width + 2, index // columns * height + 2))
sheet.save(run / "sheet.png")
print(run / "sheet.png", len(shots))

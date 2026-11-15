"""Static StopDial geometry guard; not a rendered UI or DAW test."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
editor = (root / "Source/PluginEditor.cpp").read_text()
theme = (root / "Source/ui/Theme.h").read_text()
def dimension(name):
    m = re.search(r"\b" + name + r"\s*=\s*(\d+(?:\.\d+)?)f?", theme)
    assert m, f"Missing dimension: {name}"
    return float(m.group(1))
width, height = dimension("designW"), dimension("designH")
rects = {name: tuple(map(int, (x, y, w, h))) for name, x, y, w, h in
         re.findall(r"place\((\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)", editor)}
expected = {"dial", "curveButton", "stopButton", "modes", "status"}
assert set(rects) == expected, f"Changed control set: {set(rects) ^ expected}"
for name, (x, y, w, h) in rects.items():
    assert min(x, y) >= 0 and min(w, h) > 0
    assert x + w <= width and y + h <= height, name
def overlaps(a, b):
    x, y, w, h = rects[a]
    X, Y, W, H = rects[b]
    return max(x, X) < min(x + w, X + W) and max(y, Y) < min(y + h, Y + H)
for pair in (("curveButton", "stopButton"), ("modes", "status"),
             ("dial", "status"), ("dial", "modes")):
    assert not overlaps(*pair), pair
assert "setFixedAspectRatio" in editor and "setResizable(true, true)" in editor
assert "boundsConstrainer.setSizeLimits(435, 535, 1087, 1338)" in editor
print("StopDial static geometry PASS; runtime unverified")

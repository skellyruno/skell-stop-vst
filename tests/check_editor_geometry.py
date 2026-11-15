"""Static StopDial editor layout guard; not a rendered UI or host test.

Run: python3 tests/check_editor_geometry.py
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
editor = (root / "Source" / "PluginEditor.cpp").read_text(encoding="utf-8")
theme = (root / "Source" / "ui" / "Theme.h").read_text(encoding="utf-8")

def dimension(name):
    match = re.search(r"\\b" + name + r"\\s*=\\s*(\\d+)", theme)
    assert match, f"Missing theme dimension {name}"
    return int(match.group(1))

width, height = dimension("designW"), dimension("designH")
assert width > 0 and height > 0
rects = {}
for name, x, y, w, h in re.findall(
    r"place\\((\\w+),\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\)", editor
):
    rects[name] = tuple(map(int, (x, y, w, h)))
expected = {"dial", "curveButton", "stopButton", "modes", "status"}
assert expected == rects.keys(), f"Unexpected editor controls: {rects.keys()}"
for name, (x, y, w, h) in rects.items():
    assert min(x, y) >= 0 and min(w, h) > 0, (name, rects[name])
    assert x + w <= width and y + h <= height, (name, rects[name])

def overlaps(a, b):
    x, y, w, h = rects[a]
    X, Y, W, H = rects[b]
    return max(x, X) < min(x + w, X + W) and max(y, Y) < min(y + h, Y + H)

for pair in (("curveButton", "stopButton"), ("modes", "status"),
             ("dial", "status"), ("dial", "modes")):
    assert not overlaps(*pair), f"Overlapping controls: {pair}"

assert "setFixedAspectRatio" in editor
assert "setResizable(true, true)" in editor
assert "boundsConstrainer.setSizeLimits(435, 535, 1087, 1338)" in editor
print("StopDial static editor geometry checks PASSED (runtime UI unverified)")

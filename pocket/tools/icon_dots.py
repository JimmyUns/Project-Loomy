# icon_dots.py - how the app icon's halftone dots were placed (pocket/app/src/main/res/drawable/ic_launcher_*.xml).
# Final numbers: L at VX=47.5 TOP=39 BOT=66 FX=62 W=8.6; dots(step=3.9, rmax=2.0, reach=15, gap=1.5), kept
# within 35.5 of the centre (Android shows the middle 72 of the 108 canvas). Not part of the build.
import math
# the L: vertical stroke x=VX from TOP to BOT, foot from VX to FX at y=BOT; stroke width W (round caps)
VX, TOP, BOT, FX, W = 45.0, 31.0, 72.0, 68.0, 11.0
def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx-ax, by-ay
    t = max(0, min(1, ((px-ax)*dx + (py-ay)*dy) / (dx*dx + dy*dy)))
    return math.hypot(px-ax-t*dx, py-ay-t*dy)
def dots(step=5.6, rmax=2.9, reach=24.0, gap=2.2):
    out = []
    rows = int(108/ (step*0.866)) + 2
    for j in range(rows):
        y = j*step*0.866
        off = (step/2) if j % 2 else 0
        for i in range(-1, int(108/step)+2):
            x = i*step + off
            d = min(seg_dist(x, y, VX, TOP, VX, BOT), seg_dist(x, y, VX, BOT, FX, BOT)) - W/2
            if d < gap: continue                      # on/too close to the letter
            outer = x < VX - W/2 or y > BOT + W/2      # left of the L or under it
            if not outer: continue
            if x > FX + W/2 + 1 or y < TOP - W/2 - 1: continue   # only alongside the letter
            k = 1 - (d - gap) / reach
            if k <= 0: continue
            r = rmax * k**0.9
            if r < 0.55: continue
            if math.hypot(x-54, y-54) + r > 50: continue   # stay inside the icon's safe circle
            out.append((round(x,2), round(y,2), round(r,2), round(0.25 + 0.75*k, 2)))
    return out

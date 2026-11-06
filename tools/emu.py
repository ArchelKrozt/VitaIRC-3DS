#!/usr/bin/env python3
"""Development helper: drives VitaIRC running in the Azahar emulator (macOS).
  emu.py tap X Y        touch the bottom screen at 3DS coordinates (320x240)
  emu.py hold X Y MS    long press
  emu.py key a|b|x|y|l|r|up|down|left|right|start|select [n]
  emu.py type TEXT      types with VitaIRC's on-screen keyboard (letters, space, digits)
  emu.py shot FILE      screenshot of both screens (800x480 PNG: top over bottom)
Needs Screen Recording / Accessibility permission for the terminal."""
import sys, time, subprocess, os
import Quartz

KEYS = {'a': 0, 'b': 1, 'x': 6, 'y': 7, 'l': 12, 'r': 13, 'up': 17, 'down': 5,
        'left': 3, 'right': 4, 'start': 46, 'select': 45, 'zl': 18, 'zr': 19}

def window():
    for w in Quartz.CGWindowListCopyWindowInfo(Quartz.kCGWindowListOptionOnScreenOnly, Quartz.kCGNullWindowID):
        if 'azahar' in (w.get('kCGWindowOwnerName') or '').lower() and w.get('kCGWindowLayer') == 0:
            b = w['kCGWindowBounds']
            return w['kCGWindowNumber'], b['X'], b['Y'], b['Width'], b['Height']
    sys.exit('Azahar window not found')

TITLE, STATUS = 32, 32

def geom():
    wid, x, y, w, h = window()
    sc = (h - TITLE - STATUS) / 480.0
    top_x = x + (w - 400 * sc) / 2
    bot_x = x + (w - 320 * sc) / 2
    top_y = y + TITLE
    bot_y = top_y + 240 * sc
    return wid, sc, top_x, top_y, bot_x, bot_y

def mouse(kind, px, py):
    ev = Quartz.CGEventCreateMouseEvent(None, kind, (px, py), Quartz.kCGMouseButtonLeft)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, ev)

def focus():
    subprocess.run(['osascript', '-e', 'tell application "System Events" to set frontmost of (first process whose name contains "azahar") to true'],
                   capture_output=True)
    # a click on the title bar: the first click on an inactive window is swallowed
    _, x, y, w, h = window()
    mouse(Quartz.kCGEventLeftMouseDown, x + w / 2, y + 12)
    mouse(Quartz.kCGEventLeftMouseUp, x + w / 2, y + 12)
    time.sleep(0.15)

def tap(x, y, ms=60):
    _, sc, _, _, bx, by = geom()
    px, py = bx + x * sc, by + y * sc
    mouse(Quartz.kCGEventMouseMoved, px, py)
    mouse(Quartz.kCGEventLeftMouseDown, px, py)
    time.sleep(ms / 1000.0)
    mouse(Quartz.kCGEventLeftMouseUp, px, py)
    time.sleep(0.08)

def key(name, n=1):
    for _ in range(n):
        code = KEYS[name]
        for down in (True, False):
            Quartz.CGEventPost(Quartz.kCGHIDEventTap, Quartz.CGEventCreateKeyboardEvent(None, code, down))
            time.sleep(0.07)
        time.sleep(0.05)

# on-screen keyboard (see source/ui/kbd.c): 4 rows of 41.5 px from y = 74
ROWS = ['qwertyuiop', 'asdfghjklñ', 'zxcvbnm']
def type_text(t):
    y0, rh = 240 - 166, 166 / 4.0
    for ch in t:
        if ch == ' ':
            tap(160, y0 + 3 * rh + rh / 2); continue
        low = ch.lower()
        if ch.isdigit():
            tap(24, y0 + 3 * rh + rh / 2)                         # ?123
            tap(16 + 32 * ((int(ch) - 1) % 10), y0 + rh / 2)
            tap(24, y0 + 3 * rh + rh / 2)                         # ABC
            continue
        sym2 = '@#$%&*-+()'
        sym3 = '!"\':;/?'
        if ch in sym2 or ch in sym3:
            tap(24, y0 + 3 * rh + rh / 2)                         # ?123
            if ch in sym2: tap(16 + 32 * sym2.index(ch), y0 + rh + rh / 2)
            else: tap(48 + 16 + 32 * sym3.index(ch), y0 + 2 * rh + rh / 2)
            tap(24, y0 + 3 * rh + rh / 2)                         # ABC
            continue
        if ch in ',.':
            tap(48 + 32 + 16 if ch == ',' else 48 + 32 + 32 + 128 + 16, y0 + 3 * rh + rh / 2); continue
        if ch.isupper():
            tap(24, y0 + 2 * rh + rh / 2)                         # shift
        for r, row in enumerate(ROWS):
            if low in row:
                i = row.index(low)
                x = 16 + 32 * i if r < 2 else 48 + 16 + 32 * i
                tap(x, y0 + r * rh + rh / 2)
                break

def shot(path):
    wid, sc, tx, ty, bx, by = geom()
    _, x, y, w, h = window()
    tmp = path + '.full.png'
    subprocess.run(['screencapture', '-x', '-o', '-l%d' % wid, tmp], check=True)
    from PIL import Image
    im = Image.open(tmp).convert('RGB')
    k = im.size[0] / w
    top = im.crop((int((tx - x) * k), int((ty - y) * k), int((tx - x + 400 * sc) * k), int((ty - y + 240 * sc) * k))).resize((400, 240), Image.LANCZOS)
    bot = im.crop((int((bx - x) * k), int((by - y) * k), int((bx - x + 320 * sc) * k), int((by - y + 240 * sc) * k))).resize((320, 240), Image.LANCZOS)
    out = Image.new('RGB', (400, 480), (0, 0, 0))
    out.paste(top, (0, 0)); out.paste(bot, (40, 240))
    out = out.resize((800, 960), Image.LANCZOS)
    out.save(path)
    os.remove(tmp)

if __name__ == '__main__':
    a = sys.argv[1:]
    focus()
    time.sleep(0.2)
    if a[0] == 'tap': tap(float(a[1]), float(a[2]))
    elif a[0] == 'hold': tap(float(a[1]), float(a[2]), int(a[3]))
    elif a[0] == 'key': key(a[1], int(a[2]) if len(a) > 2 else 1)
    elif a[0] == 'type': type_text(' '.join(a[1:]))
    elif a[0] == 'shot': shot(a[1])

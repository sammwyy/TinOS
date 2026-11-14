#!/usr/bin/env python3
"""Render the TinOS icon gallery into resources/icons/.

Every icon is described once with signed-distance shapes in a 32x32 design
space and rasterised, anti-aliased, at each size it ships in.  The result is
written as 32-bit BMP files with alpha:

    resources/icons/<name>.bmp      32x32
    resources/icons/<name>@16.bmp   16x16
    resources/icons/<name>@48.bmp   48x48 (only for icons drawn large)

Only the Python standard library is needed.  The BMPs are committed, so a
normal build never runs this script; rerun it after editing an icon:

    python3 tools/draw_icons.py resources/icons
"""

import math
from pathlib import Path
import struct
import sys

# --- palette ------------------------------------------------------------

INK = (34, 38, 54)
WHITE = (255, 255, 255)
PAPER = (246, 247, 251)
PAPER_SHADE = (208, 213, 226)
LINE = (150, 158, 178)
BLUE = (74, 120, 206)
BLUE_LIGHT = (128, 172, 240)
BLUE_DARK = (40, 72, 150)
VIOLET = (110, 100, 190)
VIOLET_LIGHT = (160, 150, 230)
GREEN = (70, 176, 98)
GREEN_LIGHT = (130, 220, 140)
GREEN_DARK = (36, 116, 64)
RED = (214, 72, 72)
RED_LIGHT = (246, 130, 120)
RED_DARK = (150, 40, 44)
YELLOW = (246, 196, 72)
YELLOW_LIGHT = (255, 226, 130)
YELLOW_DARK = (196, 140, 36)
ORANGE = (236, 140, 56)
GREY = (150, 154, 168)
GREY_LIGHT = (200, 203, 214)
GREY_DARK = (92, 96, 112)
SCREEN = (20, 26, 38)
TEAL = (52, 170, 180)


def clamp(value, low=0.0, high=1.0):
    return low if value < low else high if value > high else value


# --- signed distance shapes (design units, y grows downwards) ------------

def rrect(x0, y0, x1, y1, r=0.0):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r

    def d(x, y):
        qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
        outside = math.hypot(max(qx, 0), max(qy, 0))
        return outside + min(max(qx, qy), 0) - r
    return d


def circle(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def ellipse(cx, cy, rx, ry):
    def d(x, y):
        k = math.hypot((x - cx) / rx, (y - cy) / ry)
        return (k - 1) * min(rx, ry)
    return d


def ring(cx, cy, r, w):
    return lambda x, y: abs(math.hypot(x - cx, y - cy) - r) - w / 2


def seg(ax, ay, bx, by, w):
    def d(x, y):
        px, py, vx, vy = x - ax, y - ay, bx - ax, by - ay
        h = clamp((px * vx + py * vy) / (vx * vx + vy * vy or 1))
        return math.hypot(px - vx * h, py - vy * h) - w / 2
    return d


def poly(points):
    n = len(points)

    def d(x, y):
        best = float("inf")
        sign = 1.0
        j = n - 1
        for i in range(n):
            ax, ay = points[i]
            bx, by = points[j]
            ex, ey = bx - ax, by - ay
            wx, wy = x - ax, y - ay
            h = clamp((wx * ex + wy * ey) / (ex * ex + ey * ey or 1))
            best = min(best, math.hypot(wx - ex * h, wy - ey * h))
            c1 = y >= ay
            c2 = y < by
            c3 = ex * wy > ey * wx
            if (c1 and c2 and c3) or (not c1 and not c2 and not c3):
                sign = -sign
            j = i
        return sign * best
    return d


def path(points, w):
    parts = [seg(*points[i], *points[i + 1], w) for i in range(len(points) - 1)]
    return union(*parts)


def arc(cx, cy, r, a0, a1, w, steps=18):
    """Stroke from angle a0 to a1 (degrees, 0 = right, clockwise on screen)."""
    pts = []
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return path(pts, w)


def union(*shapes):
    return lambda x, y: min(s(x, y) for s in shapes)


def inter(*shapes):
    return lambda x, y: max(s(x, y) for s in shapes)


def sub(a, *b):
    return lambda x, y: max(a(x, y), *(-s(x, y) for s in b))


def gear(cx, cy, r, teeth, depth):
    def d(x, y):
        dx, dy = x - cx, y - cy
        t = math.atan2(dy, dx) * teeth
        wave = clamp(math.cos(t) * 2.2, -1, 1)
        return (math.hypot(dx, dy) - (r + depth * (wave + 1) / 2)) * 0.85
    return d


# --- paints --------------------------------------------------------------

def vgrad(top, bottom, y0=0.0, y1=32.0):
    def paint(x, y):
        t = clamp((y - y0) / (y1 - y0))
        return tuple(top[i] + (bottom[i] - top[i]) * t for i in range(3))
    return paint


def dgrad(a, b):
    def paint(x, y):
        t = clamp((x + y) / 64)
        return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))
    return paint


# --- canvas --------------------------------------------------------------

class Canvas:
    def __init__(self, n):
        self.n = n
        self.small = n <= 16
        self.scale = n / 32.0
        self.px = [[(0.0, 0.0, 0.0, 0.0)] * n for _ in range(n)]

    def _blend(self, ix, iy, color, alpha):
        if alpha <= 0:
            return
        r, g, b, a = self.px[iy][ix]
        na = alpha + a * (1 - alpha)
        if na <= 0:
            return
        mix = [(color[k] * alpha + c * a * (1 - alpha)) / na
               for k, c in enumerate((r, g, b))]
        self.px[iy][ix] = (mix[0], mix[1], mix[2], na)

    def fill(self, shape, paint, outline=INK, ow=1.0, opacity=1.0):
        """Fill `shape` and stroke it `ow` pixels wide on the inside."""
        for iy in range(self.n):
            y = (iy + 0.5) / self.scale
            for ix in range(self.n):
                x = (ix + 0.5) / self.scale
                d = shape(x, y) * self.scale
                cover = clamp(0.5 - d)
                if cover <= 0:
                    continue
                color = paint(x, y) if callable(paint) else paint
                if outline is not None and ow > 0:
                    inner = clamp(0.5 - (d + ow))
                    self._blend(ix, iy, color, inner * opacity)
                    self._blend(ix, iy, outline, (cover - inner) * opacity
                                if inner < cover else 0)
                    if inner >= cover:
                        continue
                else:
                    self._blend(ix, iy, color, cover * opacity)

    def ink(self, shape, color=INK, opacity=1.0):
        self.fill(shape, color, outline=None, opacity=opacity)

    def pixels(self):
        out = []
        for row in self.px:
            for r, g, b, a in row:
                ai = int(round(a * 255))
                if ai == 0:
                    out.append(0)
                    continue
                out.append((ai << 24) | (int(round(r)) << 16) |
                           (int(round(g)) << 8) | int(round(b)))
        return out


# --- reusable parts --------------------------------------------------------

def page(c, x0=7, y0=3, x1=25, y1=29, fold=6, paper=PAPER):
    body = poly([(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)])
    c.fill(body, vgrad(paper, PAPER_SHADE, y0, y1))
    c.fill(poly([(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)]),
           PAPER_SHADE)


def text_lines(c, x0, x1, y0, count, gap=4, color=LINE):
    w = 2.0 if c.small else 1.6
    for i in range(count):
        end = x1 - (4 if i == count - 1 else 0)
        c.ink(seg(x0, y0 + i * gap, end, y0 + i * gap, w), color)


def monitor(c, screen_paint=SCREEN, x0=3, y0=4, x1=29, y1=23):
    c.fill(rrect(12, y1 - 1, 20, y1 + 4, 0), GREY_DARK)
    c.fill(rrect(8, y1 + 3, 24, y1 + 6, 1), GREY)
    c.fill(rrect(x0, y0, x1, y1, 3), vgrad(GREY_LIGHT, GREY))
    c.fill(rrect(x0 + 3, y0 + 3, x1 - 3, y1 - 3, 1), screen_paint)


def arrow(c, direction, color=BLUE, light=BLUE_LIGHT):
    shapes = {
        "left": [(3, 16), (15, 5), (15, 11), (28, 11), (28, 21), (15, 21), (15, 27)],
        "right": [(29, 16), (17, 5), (17, 11), (4, 11), (4, 21), (17, 21), (17, 27)],
        "up": [(16, 3), (27, 15), (21, 15), (21, 28), (11, 28), (11, 15), (5, 15)],
        "down": [(16, 29), (27, 17), (21, 17), (21, 4), (11, 4), (11, 17), (5, 17)],
    }
    c.fill(poly(shapes[direction]), vgrad(light, color))


def curved_arrow(c, mirror, color=BLUE, light=BLUE_LIGHT):
    def m(x):
        return 32 - x if mirror else x
    w = 5 if c.small else 4.5
    # A hook: horizontal stroke ending in an arc down, arrow head at the start.
    pts = []
    for i in range(13):
        a = math.radians(-90 + 180 * i / 12)
        pts.append((m(18 + 8 * math.cos(a)), 18 + 8 * math.sin(a)))
    pts.append((m(10), 26))
    body = path([(m(11), 10)] + pts, w)
    c.fill(body, vgrad(light, color))
    head = poly([(m(3), 10), (m(13), 2), (m(13), 18)])
    c.fill(head, vgrad(light, color))


def speaker(c, color=GREY_DARK):
    c.fill(poly([(3, 12), (9, 12), (16, 5), (16, 27), (9, 20), (3, 20)]),
           vgrad(GREY_LIGHT, GREY))


# --- icons ---------------------------------------------------------------

ICONS = {}
LARGE = {"tinos", "computer", "terminal", "folder", "info", "warning",
         "error", "question", "taskmgr", "settings", "play", "pause",
         "stop"}


def icon(name):
    def register(fn):
        ICONS[name] = fn
        return fn
    return register


@icon("tinos")
def _tinos(c):
    c.fill(rrect(2, 2, 30, 30, 7), dgrad(VIOLET_LIGHT, BLUE_DARK))
    c.ink(rrect(5, 4, 27, 10, 3), WHITE, opacity=0.22)
    w = 5 if c.small else 4
    c.ink(seg(14, 8, 14, 21, w), WHITE)
    c.ink(arc(19, 21, 5, 90, 180, w), WHITE)
    c.ink(seg(9, 13, 21, 13, w), WHITE)
    c.ink(circle(23.5, 23.5, 2.6), YELLOW_LIGHT)


@icon("terminal")
def _terminal(c):
    c.fill(rrect(2, 4, 30, 28, 3), SCREEN)
    c.ink(rrect(3, 5, 29, 9, 1), GREY_DARK)
    c.ink(circle(6, 7, 1.1), RED_LIGHT)
    c.ink(circle(9.5, 7, 1.1), YELLOW)
    w = 3 if c.small else 2.4
    c.ink(path([(7, 13), (12, 17), (7, 21)], w), GREEN_LIGHT)
    c.ink(seg(15, 22, 23, 22, w), WHITE)


@icon("folder")
def _folder(c):
    c.fill(poly([(2, 7), (12, 7), (15, 10), (29, 10), (29, 26), (2, 26)]),
           YELLOW_DARK)
    c.fill(poly([(2, 13), (30, 13), (28, 27), (2, 27)]),
           vgrad(YELLOW_LIGHT, YELLOW, 13, 27))


@icon("folder_open")
def _folder_open(c):
    c.fill(poly([(2, 7), (12, 7), (15, 10), (27, 10), (27, 26), (2, 26)]),
           YELLOW_DARK)
    c.fill(rrect(6, 6, 24, 22, 0), PAPER)
    c.fill(poly([(6, 14), (31, 14), (27, 27), (2, 27)]),
           vgrad(YELLOW_LIGHT, YELLOW, 14, 27))


@icon("menu_folder")
def _menu_folder(c):
    c.fill(poly([(2, 7), (12, 7), (15, 10), (29, 10), (29, 26), (2, 26)]),
           BLUE_DARK)
    c.fill(poly([(2, 13), (30, 13), (28, 27), (2, 27)]),
           vgrad(BLUE_LIGHT, BLUE, 13, 27))
    for x in (9, 16, 23):
        c.ink(rrect(x - 2, 17, x + 2, 21, 0.5), WHITE, opacity=0.9)


@icon("file")
def _file(c):
    page(c)


@icon("text")
def _text(c):
    page(c)
    text_lines(c, 10, 22, 13, 4)


@icon("editor")
def _editor(c):
    page(c, x1=23)
    text_lines(c, 10, 20, 12, 3)
    pencil = poly([(28, 9), (31, 12), (16, 27), (12, 28), (13, 24)])
    c.fill(pencil, vgrad(YELLOW_LIGHT, ORANGE))
    c.ink(poly([(13, 24), (16, 27), (12, 28)]), INK)
    c.fill(poly([(26, 11), (29, 14), (31, 12), (28, 9)]), RED_LIGHT)


@icon("image")
def _image(c):
    c.fill(rrect(2, 5, 30, 27, 2), vgrad(BLUE_LIGHT, (190, 222, 255), 5, 20))
    c.ink(circle(22, 11, 3), YELLOW)
    c.ink(poly([(3, 26), (12, 14), (19, 22), (23, 18), (29, 26)]), GREEN)
    c.ink(poly([(3, 26), (12, 14), (16, 19), (10, 26)]), GREEN_DARK,
          opacity=0.5)
    c.ink(sub(rrect(2, 5, 30, 27, 2), rrect(3, 6, 29, 26, 1.5)), INK)


@icon("image_file")
def _image_file(c):
    page(c)
    c.ink(rrect(10, 13, 22, 24, 0), (190, 222, 255))
    c.ink(poly([(10, 24), (14, 17), (18, 22), (22, 18), (22, 24)]), GREEN)
    c.ink(circle(19, 15.5, 1.6), YELLOW)


@icon("music")
def _music(c):
    w = 3 if c.small else 2.5
    c.fill(poly([(12, 6), (27, 3), (27, 8), (12, 11)]), VIOLET)
    c.fill(seg(13, 7, 13, 23, w + 1), VIOLET_LIGHT)
    c.fill(seg(26, 4, 26, 20, w + 1), VIOLET_LIGHT)
    c.fill(ellipse(9.5, 24, 5, 4), vgrad(VIOLET_LIGHT, VIOLET))
    c.fill(ellipse(22.5, 21, 5, 4), vgrad(VIOLET_LIGHT, VIOLET))


@icon("audio_file")
def _audio_file(c):
    page(c)
    c.ink(seg(17, 12, 17, 22, 2), VIOLET)
    c.ink(seg(17, 12, 21, 14, 2), VIOLET)
    c.ink(ellipse(14.5, 22.5, 3, 2.4), VIOLET)


@icon("app")
def _app(c):
    c.fill(rrect(2, 4, 30, 28, 2), PAPER)
    c.ink(rrect(3, 5, 29, 10, 0.5), vgrad(BLUE_LIGHT, BLUE, 5, 10))
    c.ink(rrect(6, 13, 14, 20, 1), YELLOW)
    c.ink(rrect(17, 13, 26, 15, 0), LINE)
    c.ink(rrect(17, 18, 23, 20, 0), LINE)
    c.ink(rrect(6, 23, 26, 25, 0), LINE)
    c.ink(sub(rrect(2, 4, 30, 28, 2), rrect(3, 5, 29, 27, 1.5)), INK)


@icon("info")
def _info(c):
    c.fill(circle(16, 16, 14), dgrad(BLUE_LIGHT, BLUE_DARK))
    c.ink(circle(16, 9, 2.6), WHITE)
    c.ink(rrect(13.5, 13, 18.5, 25, 1), WHITE)


@icon("question")
def _question(c):
    c.fill(circle(16, 16, 14), dgrad(BLUE_LIGHT, BLUE_DARK))
    w = 4.5 if c.small else 3.6
    c.ink(arc(16, 12.5, 5, 180, 405, w), WHITE)
    c.ink(seg(16, 17.5, 16, 19.5, w), WHITE)
    c.ink(circle(16, 24.5, 2.2), WHITE)


@icon("warning")
def _warning(c):
    c.fill(poly([(16, 2), (31, 28), (1, 28)]), vgrad(YELLOW_LIGHT, YELLOW))
    c.ink(rrect(14, 10, 18, 20, 1.5), INK)
    c.ink(circle(16, 24, 2), INK)


@icon("error")
def _error(c):
    c.fill(circle(16, 16, 14), dgrad(RED_LIGHT, RED_DARK))
    w = 5 if c.small else 4
    c.ink(seg(10.5, 10.5, 21.5, 21.5, w), WHITE)
    c.ink(seg(21.5, 10.5, 10.5, 21.5, w), WHITE)


@icon("close")
def _close(c):
    w = 6 if c.small else 5
    c.fill(union(seg(7, 7, 25, 25, w), seg(25, 7, 7, 25, w)),
           vgrad(RED_LIGHT, RED))


@icon("chip")
def _chip(c):
    for i in range(4):
        p = 8 + i * 5.3
        c.ink(rrect(p - 1, 2, p + 1, 30, 0.5), GREY_LIGHT)
        c.ink(rrect(2, p - 1, 30, p + 1, 0.5), GREY_LIGHT)
    c.fill(rrect(6, 6, 26, 26, 2), vgrad((70, 80, 100), (36, 42, 56)))
    c.ink(rrect(11, 11, 21, 21, 1), GREEN, opacity=0.9)
    c.ink(circle(9.5, 9.5, 1.2), GREY_LIGHT)


@icon("memory")
def _memory(c):
    c.fill(rrect(2, 9, 30, 22, 1), vgrad(GREEN_LIGHT, GREEN_DARK))
    for x in (5, 12.5, 20):
        c.ink(rrect(x, 12, x + 6, 18, 0.5), (30, 34, 44))
    for i in range(7):
        x = 4.5 + i * 3.8
        c.ink(rrect(x, 22, x + 2, 26, 0), YELLOW)


@icon("computer")
def _computer(c):
    monitor(c, vgrad(BLUE_LIGHT, BLUE_DARK))
    c.ink(poly([(7, 18), (12, 12), (16, 16), (19, 13), (25, 18)]), GREEN,
          opacity=0.85)


@icon("display")
def _display(c):
    monitor(c, dgrad(VIOLET_LIGHT, BLUE_DARK))
    c.ink(circle(20, 11, 2.5), YELLOW_LIGHT)


@icon("taskmgr")
def _taskmgr(c):
    monitor(c)
    w = 2.6 if c.small else 2
    c.ink(path([(7, 16), (11, 16), (13, 10), (16, 19), (19, 13), (21, 16),
                (25, 16)], w), GREEN_LIGHT)


@icon("network")
def _network(c):
    c.fill(rrect(10, 2, 22, 11, 1.5), vgrad(GREY_LIGHT, GREY))
    c.ink(rrect(12, 4, 20, 9, 0.5), vgrad(BLUE_LIGHT, BLUE))
    w = 3 if c.small else 2.2
    c.fill(union(seg(16, 11, 16, 18, w + 1), seg(6, 18, 26, 18, w + 1),
                 seg(6, 18, 6, 22, w + 1), seg(26, 18, 26, 22, w + 1)),
           GREY_LIGHT)
    for x in (6, 26):
        c.fill(rrect(x - 5, 21, x + 5, 29, 1.5), vgrad(GREY_LIGHT, GREY))
        c.ink(rrect(x - 3, 23, x + 3, 27, 0.5), vgrad(BLUE_LIGHT, BLUE))


@icon("network_off")
def _network_off(c):
    _network(c)
    c.fill(circle(24, 24, 7.5), RED)
    w = 3 if c.small else 2.4
    c.ink(union(seg(21, 21, 27, 27, w), seg(27, 21, 21, 27, w)), WHITE)


@icon("volume")
def _volume(c):
    speaker(c)
    w = 2.8 if c.small else 2.2
    c.ink(arc(16, 16, 6, -50, 50, w), BLUE)
    c.ink(arc(16, 16, 11, -50, 50, w), BLUE)


@icon("volume_mute")
def _volume_mute(c):
    speaker(c)
    w = 3.4 if c.small else 2.8
    c.ink(union(seg(20, 11, 29, 21, w), seg(29, 11, 20, 21, w)), RED)


@icon("power")
def _power(c):
    c.fill(circle(16, 16, 14), dgrad(RED_LIGHT, RED_DARK))
    w = 4 if c.small else 3.2
    c.ink(arc(16, 17, 7.5, -50, 230, w), WHITE)
    c.ink(seg(16, 6, 16, 15, w), WHITE)


@icon("restart")
def _restart(c):
    c.fill(circle(16, 16, 14), dgrad(GREEN_LIGHT, GREEN_DARK))
    w = 4 if c.small else 3.2
    c.ink(arc(16, 16, 7.5, -60, 220, w), WHITE)
    c.ink(poly([(18, 3.5), (25.5, 7), (19, 12.5)]), WHITE)


@icon("logout")
def _logout(c):
    c.fill(rrect(3, 3, 19, 29, 2), vgrad(GREY_LIGHT, GREY))
    c.ink(rrect(6, 6, 16, 26, 1), (60, 66, 84))
    arrow_shape = poly([(30, 16), (22, 8), (22, 13), (11, 13), (11, 19),
                        (22, 19), (22, 24)])
    c.fill(arrow_shape, vgrad(GREEN_LIGHT, GREEN))


@icon("back")
def _back(c):
    arrow(c, "left")


@icon("forward")
def _forward(c):
    arrow(c, "right")


@icon("up")
def _up(c):
    arrow(c, "up", GREEN, GREEN_LIGHT)


@icon("down")
def _down(c):
    arrow(c, "down", GREEN, GREEN_LIGHT)


@icon("home")
def _home(c):
    c.fill(rrect(6, 14, 26, 29, 0), vgrad(PAPER, PAPER_SHADE))
    c.ink(rrect(13, 19, 19, 29, 0.5), ORANGE)
    c.fill(poly([(16, 2), (31, 16), (27, 16), (16, 6), (5, 16), (1, 16)]),
           vgrad(RED_LIGHT, RED))
    c.ink(rrect(21, 4, 25, 10, 0), RED_DARK)


@icon("drive")
def _drive(c):
    c.fill(rrect(2, 9, 30, 24, 3), vgrad(GREY_LIGHT, GREY))
    c.ink(rrect(5, 17, 22, 19, 0.5), GREY_DARK)
    c.ink(circle(25.5, 18, 1.8), GREEN_LIGHT)
    c.ink(rrect(4, 11, 28, 13, 1), WHITE, opacity=0.5)


@icon("usb")
def _usb(c):
    c.fill(rrect(9, 12, 23, 30, 2), vgrad(BLUE_LIGHT, BLUE_DARK))
    c.fill(rrect(11, 3, 21, 13, 0), GREY_LIGHT)
    c.ink(rrect(13, 6, 15, 9, 0), INK)
    c.ink(rrect(17, 6, 19, 9, 0), INK)


@icon("refresh")
def _refresh(c):
    w = 4.5 if c.small else 3.6
    c.fill(arc(16, 16, 10, 200, 340, w), BLUE, outline=None)
    c.fill(arc(16, 16, 10, 20, 160, w), BLUE, outline=None)
    c.ink(poly([(28.5, 9), (28.5, 18), (20, 14)]), BLUE)
    c.ink(poly([(3.5, 23), (3.5, 14), (12, 18)]), BLUE)


@icon("save")
def _save(c):
    c.fill(poly([(3, 3), (25, 3), (29, 7), (29, 29), (3, 29)]),
           vgrad(BLUE_LIGHT, BLUE_DARK))
    c.fill(rrect(8, 3, 23, 12, 0), GREY_LIGHT)
    c.ink(rrect(18, 5, 21, 10, 0), GREY_DARK)
    c.fill(rrect(7, 17, 25, 29, 0), PAPER)
    if not c.small:
        text_lines(c, 10, 22, 21, 2)


@icon("new")
def _new(c):
    page(c, x1=24)
    star = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        r = 7 if i % 2 == 0 else 3
        star.append((23 + r * math.cos(a), 23 + r * math.sin(a)))
    c.fill(poly(star), vgrad(YELLOW_LIGHT, ORANGE))


@icon("new_folder")
def _new_folder(c):
    _folder(c)
    w = 4 if c.small else 3
    c.fill(circle(24, 23, 7.5), GREEN)
    c.ink(union(seg(24, 19, 24, 27, w), seg(20, 23, 28, 23, w)), WHITE)


@icon("undo")
def _undo(c):
    curved_arrow(c, False)


@icon("redo")
def _redo(c):
    curved_arrow(c, True)


@icon("cut")
def _cut(c):
    w = 3 if c.small else 2.5
    c.ink(seg(10, 3, 20, 20, w), GREY_DARK)
    c.ink(seg(22, 3, 12, 20, w), GREY_DARK)
    c.fill(ring(9, 24, 4.5, 3), RED, outline=None)
    c.fill(ring(23, 24, 4.5, 3), RED, outline=None)


@icon("copy")
def _copy(c):
    c.fill(rrect(3, 3, 19, 23, 1), vgrad(PAPER, PAPER_SHADE))
    c.fill(rrect(12, 10, 29, 30, 1), vgrad(PAPER, PAPER_SHADE))
    text_lines(c, 15, 26, 15, 3)


@icon("paste")
def _paste(c):
    c.fill(rrect(4, 5, 26, 30, 2), vgrad(ORANGE, YELLOW_DARK))
    c.fill(rrect(10, 2, 20, 8, 1.5), GREY_LIGHT)
    c.fill(rrect(13, 12, 29, 30, 1), vgrad(PAPER, PAPER_SHADE))
    text_lines(c, 16, 26, 17, 3)


@icon("delete")
def _delete(c):
    c.fill(poly([(7, 9), (25, 9), (23, 29), (9, 29)]), vgrad(GREY_LIGHT, GREY))
    w = 2 if c.small else 1.6
    for x in (12.5, 16, 19.5):
        c.ink(seg(x, 13, x, 25, w), GREY_DARK)
    c.fill(rrect(4, 5, 28, 9, 1), vgrad(GREY_LIGHT, GREY))
    c.fill(rrect(12, 2, 20, 5, 1), GREY)


@icon("rename")
def _rename(c):
    c.fill(rrect(2, 9, 30, 23, 2), PAPER)
    c.ink(rrect(5, 13, 17, 19, 0), BLUE_LIGHT)
    w = 2.4 if c.small else 2
    c.ink(union(seg(21, 6, 21, 26, w), seg(18, 6, 24, 6, w),
                seg(18, 26, 24, 26, w)), INK)


@icon("search")
def _search(c):
    w = 5 if c.small else 4
    c.ink(seg(19, 19, 28, 28, w + 1), GREY_DARK)
    c.fill(circle(13, 13, 10), (200, 230, 255))
    c.ink(ring(13, 13, 8.5, 3 if c.small else 2.6), GREY_DARK)
    c.ink(circle(10, 10, 2.5), WHITE, opacity=0.9)


@icon("settings")
def _settings(c):
    c.fill(sub(gear(16, 16, 10.5, 8, 4), circle(16, 16, 4.5)),
           dgrad(GREY_LIGHT, GREY_DARK))


@icon("calculator")
def _calculator(c):
    c.fill(rrect(5, 2, 27, 30, 3), vgrad(GREY_LIGHT, GREY))
    c.fill(rrect(8, 5, 24, 11, 1), (170, 210, 160))
    if c.small:
        for row in range(2):
            for col in range(3):
                x, y = 9 + col * 6, 15 + row * 7
                c.ink(rrect(x, y, x + 3, y + 3, 0), INK if col < 2 else ORANGE)
    else:
        for row in range(4):
            for col in range(4):
                x, y = 8.5 + col * 4, 14 + row * 4
                color = ORANGE if col == 3 else (PAPER if row < 3 else GREY_DARK)
                c.ink(rrect(x, y, x + 3, y + 2.6, 0.6), color)


@icon("keyboard")
def _keyboard(c):
    c.fill(rrect(1, 8, 31, 25, 2), vgrad(GREY_LIGHT, GREY))
    if c.small:
        for row in range(2):
            for col in range(6):
                c.ink(rrect(4 + col * 4, 11 + row * 4, 6 + col * 4,
                            13 + row * 4, 0), INK)
        c.ink(rrect(8, 20, 24, 22, 0), INK)
    else:
        for row in range(3):
            for col in range(8):
                x = 4 + col * 3.2 + (row % 2) * 1.2
                c.ink(rrect(x, 11 + row * 3.4, x + 2.2, 13.2 + row * 3.4, 0.4),
                      PAPER)
        c.ink(rrect(9, 21, 23, 23, 0.5), PAPER)


@icon("clock")
def _clock(c):
    c.fill(circle(16, 16, 14), dgrad(PAPER, PAPER_SHADE))
    w = 3 if c.small else 2.4
    c.ink(seg(16, 16, 16, 8, w), INK)
    c.ink(seg(16, 16, 22, 19, w), BLUE)
    if not c.small:
        for i in range(12):
            a = math.radians(i * 30)
            c.ink(circle(16 + 11 * math.cos(a), 16 + 11 * math.sin(a), 0.9),
                  GREY_DARK)


@icon("calendar")
def _calendar(c):
    c.fill(rrect(3, 5, 29, 29, 2), PAPER)
    c.ink(rrect(4, 6, 28, 12, 1), vgrad(RED_LIGHT, RED, 6, 12))
    c.ink(rrect(8, 2, 11, 8, 1), GREY_DARK)
    c.ink(rrect(21, 2, 24, 8, 1), GREY_DARK)
    step = 6 if c.small else 4.5
    for row in range(3):
        for col in range(4):
            x, y = 7 + col * step, 15 + row * 4.5
            if x + 2 < 27 and y + 2 < 28:
                c.ink(rrect(x, y, x + 2.2, y + 2.2, 0), LINE)
    c.ink(sub(rrect(3, 5, 29, 29, 2), rrect(4, 6, 28, 28, 1.5)), INK)


@icon("user")
def _user(c):
    c.fill(circle(16, 10, 6.5), vgrad(YELLOW_LIGHT, (232, 180, 120)))
    c.fill(inter(circle(16, 32, 13), rrect(0, 0, 32, 30, 0)),
           vgrad(BLUE_LIGHT, BLUE))


@icon("play")
def _play(c):
    c.fill(poly([(8, 4), (27, 16), (8, 28)]), vgrad(GREEN_LIGHT, GREEN))


@icon("pause")
def _pause(c):
    c.fill(rrect(7, 5, 13, 27, 1), vgrad(BLUE_LIGHT, BLUE))
    c.fill(rrect(19, 5, 25, 27, 1), vgrad(BLUE_LIGHT, BLUE))


@icon("stop")
def _stop(c):
    c.fill(rrect(6, 6, 26, 26, 2), vgrad(RED_LIGHT, RED))


@icon("next")
def _next(c):
    c.fill(poly([(4, 6), (17, 16), (4, 26)]), vgrad(BLUE_LIGHT, BLUE))
    c.fill(poly([(15, 6), (27, 16), (15, 26)]), vgrad(BLUE_LIGHT, BLUE))
    c.fill(rrect(26, 6, 30, 26, 0), BLUE)


@icon("prev")
def _prev(c):
    c.fill(poly([(28, 6), (15, 16), (28, 26)]), vgrad(BLUE_LIGHT, BLUE))
    c.fill(poly([(17, 6), (5, 16), (17, 26)]), vgrad(BLUE_LIGHT, BLUE))
    c.fill(rrect(2, 6, 6, 26, 0), BLUE)


@icon("desktop")
def _desktop(c):
    c.fill(rrect(2, 4, 30, 26, 2), dgrad(VIOLET_LIGHT, BLUE_DARK))
    c.ink(rrect(3, 22, 29, 25, 0), GREY_LIGHT)
    c.ink(rrect(4, 22.5, 8, 24.5, 0), BLUE)
    for y in (7, 13):
        c.ink(rrect(5, y, 9, y + 4, 0.5), YELLOW)
    c.ink(rrect(13, 8, 26, 18, 1), PAPER, opacity=0.9)


@icon("wallpaper")
def _wallpaper(c):
    _image(c)


@icon("lock")
def _lock(c):
    w = 4 if c.small else 3.2
    c.ink(arc(16, 13, 7, 180, 360, w), GREY_DARK)
    c.ink(seg(9, 13, 9, 16, w), GREY_DARK)
    c.ink(seg(23, 13, 23, 16, w), GREY_DARK)
    c.fill(rrect(5, 15, 27, 30, 2), vgrad(YELLOW_LIGHT, YELLOW_DARK))
    c.ink(circle(16, 21, 2.2), INK)
    c.ink(rrect(15, 21, 17, 26, 0), INK)


@icon("star")
def _star(c):
    star = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        r = 14 if i % 2 == 0 else 6
        star.append((16 + r * math.cos(a), 17 + r * math.sin(a)))
    c.fill(poly(star), vgrad(YELLOW_LIGHT, ORANGE))


@icon("help")
def _help(c):
    _question(c)


# --- output -------------------------------------------------------------

def write_bmp(path, size, pixels):
    """32-bit BI_BITFIELDS BMP, bottom-up, BGRA."""
    header_size = 108
    data = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            p = pixels[y * size + x]
            data += struct.pack("<I", p)
    info = struct.pack("<IiiHHIIiiII", header_size, size, size, 1, 32, 3,
                       len(data), 2835, 2835, 0, 0)
    info += struct.pack("<IIII", 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)
    info += b"sRGB"[::-1] + b"\0" * 36 + b"\0" * 12
    offset = 14 + len(info)
    head = b"BM" + struct.pack("<IHHI", offset + len(data), 0, 0, offset)
    blob = head + info + bytes(data)
    if not path.exists() or path.read_bytes() != blob:
        path.write_bytes(blob)


def render(name, size):
    canvas = Canvas(size)
    ICONS[name](canvas)
    return canvas.pixels()


def main(argv):
    if len(argv) < 2:
        raise SystemExit("usage: draw_icons.py <output-dir> [name ...]")
    out = Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)
    names = argv[2:] or sorted(ICONS)
    for name in names:
        sizes = [16, 32] + ([48] if name in LARGE else [])
        for size in sizes:
            suffix = "" if size == 32 else f"@{size}"
            write_bmp(out / f"{name}{suffix}.bmp", size, render(name, size))
    print(f"draw_icons.py: {len(names)} icons -> {out}")


if __name__ == "__main__":
    main(sys.argv)

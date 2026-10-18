#!/usr/bin/env python3
"""
Wall art editor for the VectorPie GLtron port.

Edits the vector lines drawn on the arena walls (src/wall_art.h) over
GLtron's wall texture (tools/walls.png) as a faded tracing underlay. The
strip is 1024 x 64 texture pixels and wraps once around the arena, a
quarter (256 pixels) per wall; the top of the strip is the top of the wall.
A line may end on a wall's edge but not cross it (the game would skip that
segment: it is shown red here).

Opens src/wall_art.h, saves back to it (the first save of a session keeps
the previous file as wall_art.h.bak). Points are whole texture pixels.
Unsaved work is also written to src/wall_art.autosave.h a second after
each change, and offered back when the editor starts; a line still being
drawn is finished, not lost, when the tool changes or the editor closes.

Tools (keys work when the drawing has focus):
  Pen (P)     click to add points, Shift = horizontal / vertical / 45 deg;
              Enter, right click or double click ends the line, clicking
              its first point closes it; Backspace drops the last point,
              Esc cancels.
  Select (V)  click a line to select it (Shift adds), drag on empty space
              for a box, drag a selected line to move it, drag a point to
              move it; double click on a line inserts a point there;
              Backspace deletes the point under the mouse, X cuts the
              segment under the mouse, arrows nudge (Shift: 8 pixels).
  Ctrl+Z / Ctrl+Y undo / redo, Ctrl+X / C / V cut / copy / paste (in
  place), Ctrl+A select all, Del delete, Ctrl+D copy the selection onto
  the next wall, Ctrl+S save.
  Mouse wheel scrolls, Ctrl+wheel zooms. The strip on top shows the whole
  wall; click it to jump.

Usage:
    python wall_editor.py [--header PATH] [--underlay PATH]

GLtron Copyright (C) Andreas Umbach. This port: GPL v2 or later (COPYING).
"""

import argparse
import math
import re
import shutil
import sys
from copy import deepcopy
from pathlib import Path

import tkinter as tk
from tkinter import messagebox, ttk

try:
    from PIL import Image, ImageTk
except ImportError:
    sys.stderr.write("Pillow required: python -m pip install pillow\n")
    sys.exit(1)

HERE = Path(__file__).resolve().parent
DEFAULT_HEADER = HERE.parent / "src" / "wall_art.h"
DEFAULT_UNDERLAY = HERE / "walls.png"

W, H = 1024, 64              # the strip, texture pixels
QUARTER = W // 4             # one wall
ORG = 24                     # canvas margin around the strip, screen pixels
PICK = 7                     # pick distance, screen pixels

COL_LINE = "#b8a0ff"         # the game draws them (150, 120, 255)
COL_SEL = "#ffe040"
COL_BAD = "#ff3030"
COL_PEN = "#40ff90"
COL_WALL = "#ff8040"


# ---- wall_art.h ------------------------------------------------------------

def load_header(path):
    text = Path(path).read_text()
    def array(name):
        m = re.search(name + r"\[[^\]]*\](?:\[2\])?\s*=\s*\{(.*?)\n\};", text, re.S)
        if not m:
            raise ValueError(f"{path}: no {name} array")
        return [int(n) for n in re.findall(r"-?\d+", m.group(1))]
    start = array("wall_art_start")
    flat = array("wall_art_points")
    pts = [(flat[i], flat[i + 1]) for i in range(0, len(flat) - 1, 2)]
    polys = []
    for i in range(len(start) - 1):
        p = pts[start[i]:start[i + 1]]
        if len(p) >= 2:
            polys.append(p)
    return polys


def clean(polys):
    """Clamped, whole pixels, no repeated points, at least 2 points each."""
    out = []
    for p in polys:
        q = []
        for x, y in p:
            pt = (min(W, max(0, int(round(x)))), min(H - 1, max(0, int(round(y)))))
            if not q or q[-1] != pt:
                q.append(pt)
        if len(q) >= 2:
            out.append(q)
    return out


def save_header(path, polys):
    polys = clean(polys)
    start = [0]
    for p in polys:
        start.append(start[-1] + len(p))
    pts = [pt for p in polys for pt in p] or [(0, 0)]    # C has no empty arrays

    def rows(vals, n):
        return ",\n".join("\t" + ", ".join(vals[i:i + n]) for i in range(0, len(vals), n))

    Path(path).write_text(f"""/* GLtron's wall texture (art/classic/walls.png, 1024 x 64) as vector
 * lines, drawn over the texture with tools/wall_editor.py. Coordinates are
 * texture pixels: x 0..1024 along the strip, which wraps once around the
 * arena (a quarter per wall, x 0 and 1024 the same corner); y 0..63 from
 * the top of the wall. A segment may end on a wall's edge but not cross it.
 * Artwork from GLtron (see CREDITS); GPL v2 or later. */
#ifndef WALL_ART_H
#define WALL_ART_H

#define WALL_ART_WIDTH  {W}
#define WALL_ART_HEIGHT {H}
#define WALL_ART_POLYLINES {len(polys)}
#define WALL_ART_POINTS {len(pts)}

/* polyline i is points[start[i]] .. points[start[i + 1] - 1] */
static const unsigned short wall_art_start[WALL_ART_POLYLINES + 1] = {{
{rows([str(s) for s in start], 16)}
}};

static const unsigned short wall_art_points[WALL_ART_POINTS][2] = {{
{rows(["{%d,%d}" % pt for pt in pts], 8)}
}};

#endif
""", newline="\n")
    return polys


def crosses_wall(a, b):
    """The segment crosses a wall's edge (it may end on one)."""
    lo, hi = min(a[0], b[0]), max(a[0], b[0])
    return hi > lo and lo // QUARTER != (hi - 1) // QUARTER


def dist_to_segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay), 0.0
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy)), t


# ---- the editor ------------------------------------------------------------

class WallEditor:
    def __init__(self, root, header, underlay):
        self.root = root
        self.header = Path(header)
        self.backed_up = False
        self.autosave = self.header.with_name("wall_art.autosave.h")
        self._autosave_pending = False
        self.polys = load_header(self.header) if self.header.exists() else []
        self.src = Image.open(underlay).convert("RGB") if Path(underlay).exists() else None
        if self.src is not None and self.src.size != (W, H):
            self.src = self.src.resize((W, H), Image.BILINEAR)

        self.zoom = 10
        self.alpha = 0.55
        self.tool = tk.StringVar(value="pen")
        self.show_grid = tk.BooleanVar(value=True)
        self.sel = set()
        self.pen = []
        self.mouse = None            # texture coords (float)
        self.drag = None             # ("vertex", pi, vi) / ("move", anchor) / ("band", u, v)
        self.drag_undo = False
        self.undo_stack, self.redo_stack = [], []
        self.clipboard = []
        self.dirty = False
        self._bg_pending = False
        self._bg_imgs = []

        self._build_ui()
        self._bind_keys()
        self.tool.trace_add("write", lambda *a: self._on_tool())
        self.root.after_idle(self._first_draw)

    # ---- UI ----
    def _build_ui(self):
        r = self.root
        r.geometry("1400x760")
        r.protocol("WM_DELETE_WINDOW", self._on_close)

        self.status = tk.StringVar(value="")
        ttk.Label(r, textvariable=self.status, relief=tk.SUNKEN, anchor=tk.W
                  ).pack(side=tk.BOTTOM, fill=tk.X)

        tb = ttk.Frame(r)
        tb.pack(side=tk.TOP, fill=tk.X, padx=2, pady=2)
        def btn(label, cmd, tip=None):
            b = ttk.Button(tb, text=label, command=lambda: (cmd(), self.canvas.focus_set()),
                           takefocus=False, width=max(4, len(label) + 1))
            b.pack(side=tk.LEFT, padx=1)
            if tip:
                self._tooltip(b, tip)
        def sep():
            ttk.Separator(tb, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=4)
        btn("Save", self.save, "Save src/wall_art.h  (Ctrl+S)")
        sep()
        btn("Undo", self.undo, "Ctrl+Z")
        btn("Redo", self.redo, "Ctrl+Y")
        sep()
        btn("Cut", self.cut, "Ctrl+X")
        btn("Copy", self.copy, "Ctrl+C")
        btn("Paste", self.paste, "Ctrl+V (in place)")
        btn("Del", self.delete_sel, "Delete")
        sep()
        btn("Mirror H", lambda: self._mirror(True), "Flip the selection left-right")
        btn("Mirror V", lambda: self._mirror(False), "Flip the selection top-bottom")
        btn("To next wall", self.to_next_wall, "Copy the selection 256 pixels right  (Ctrl+D)")
        sep()
        ttk.Radiobutton(tb, text="Pen (P)", variable=self.tool, value="pen",
                        takefocus=False).pack(side=tk.LEFT, padx=2)
        ttk.Radiobutton(tb, text="Select (V)", variable=self.tool, value="select",
                        takefocus=False).pack(side=tk.LEFT, padx=2)
        sep()
        ttk.Label(tb, text="Zoom").pack(side=tk.LEFT)
        self.zoom_var = tk.DoubleVar(value=self.zoom)
        zs = ttk.Scale(tb, from_=3, to=24, orient=tk.HORIZONTAL, length=120, takefocus=False,
                       variable=self.zoom_var, command=lambda s: self._set_zoom(round(float(s))))
        zs.pack(side=tk.LEFT, padx=4)
        zs.bind("<ButtonRelease-1>", lambda e: self.canvas.focus_set(), add="+")
        ttk.Label(tb, text="Underlay").pack(side=tk.LEFT)
        self.alpha_var = tk.DoubleVar(value=self.alpha)
        al = ttk.Scale(tb, from_=0.0, to=1.0, orient=tk.HORIZONTAL, length=120, takefocus=False,
                       variable=self.alpha_var, command=self._on_alpha)
        al.pack(side=tk.LEFT, padx=4)
        al.bind("<ButtonRelease-1>", lambda e: self.canvas.focus_set(), add="+")
        ttk.Checkbutton(tb, text="Grid", variable=self.show_grid, takefocus=False,
                        command=lambda: (self._schedule_bg(), self.canvas.focus_set())
                        ).pack(side=tk.LEFT, padx=4)

        ttk.Separator(r, orient=tk.HORIZONTAL).pack(side=tk.TOP, fill=tk.X)

        # the whole strip, 1 texture pixel = 1 screen pixel
        self.overview = tk.Canvas(r, width=W + 2 * ORG, height=H + 20, bg="#101010",
                                  highlightthickness=0)
        self.overview.pack(side=tk.TOP, anchor=tk.W, padx=4, pady=4)
        self.overview.bind("<Button-1>", self._on_overview)
        self.overview.bind("<B1-Motion>", self._on_overview)

        body = ttk.Frame(r)
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        side = ttk.Frame(body, width=240)
        side.pack_propagate(False)
        side.pack(side=tk.RIGHT, fill=tk.Y)
        left = ttk.Frame(body)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(left, bg="#000000", highlightthickness=0, cursor="crosshair")
        xsb = ttk.Scrollbar(left, orient=tk.HORIZONTAL, command=self._xview)
        ysb = ttk.Scrollbar(left, orient=tk.VERTICAL, command=self._yview)
        self.canvas.configure(xscrollcommand=lambda a, b: (xsb.set(a, b), self._schedule_bg()),
                              yscrollcommand=lambda a, b: (ysb.set(a, b), self._schedule_bg()))
        xsb.pack(side=tk.BOTTOM, fill=tk.X)
        ysb.pack(side=tk.RIGHT, fill=tk.Y)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        c = self.canvas
        c.bind("<Configure>", lambda e: self._schedule_bg())
        c.bind("<Button-1>", self._on_press)
        c.bind("<B1-Motion>", self._on_drag)
        c.bind("<ButtonRelease-1>", self._on_release)
        c.bind("<Double-Button-1>", self._on_double)
        c.bind("<Button-3>", self._on_right)
        c.bind("<Motion>", self._on_motion)
        c.bind("<MouseWheel>", self._on_wheel)                       # Windows, macOS
        c.bind("<Button-4>", lambda e: self._on_wheel(e, 120))       # X11
        c.bind("<Button-5>", lambda e: self._on_wheel(e, -120))

        st = ttk.LabelFrame(side, text="Wall art")
        st.pack(fill=tk.X, padx=4, pady=4)
        self.stats = tk.StringVar()
        ttk.Label(st, textvariable=self.stats, justify=tk.LEFT).pack(anchor=tk.W, padx=6, pady=4)
        sf = ttk.LabelFrame(side, text="Selection")
        sf.pack(fill=tk.X, padx=4, pady=4)
        self.sel_info = tk.StringVar()
        ttk.Label(sf, textvariable=self.sel_info, justify=tk.LEFT).pack(anchor=tk.W, padx=6, pady=4)
        lf = ttk.LabelFrame(side, text="Lines")
        lf.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.listbox = tk.Listbox(lf, selectmode=tk.EXTENDED, activestyle="none", exportselection=False)
        self.listbox.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        self.listbox.bind("<<ListboxSelect>>", self._on_list_select)

    def _tooltip(self, widget, text):
        tip = {"win": None}
        def show(e):
            if tip["win"]:
                return
            tw = tk.Toplevel(widget)
            tw.wm_overrideredirect(True)
            tw.geometry(f"+{e.x_root + 12}+{e.y_root + 8}")
            tk.Label(tw, text=text, background="#ffffe0", borderwidth=1, relief=tk.SOLID,
                     padx=4, pady=1, font=("TkDefaultFont", 8)).pack()
            tip["win"] = tw
        def hide(_):
            if tip["win"]:
                tip["win"].destroy()
                tip["win"] = None
        widget.bind("<Enter>", show)
        widget.bind("<Leave>", hide)
        widget.bind("<ButtonPress>", hide)

    def _bind_keys(self):
        r = self.root
        def glob(seq, fn):
            r.bind_all(seq, lambda e: None if self._typing() else fn())
        glob("<Control-s>", self.save)
        glob("<Control-z>", self.undo)
        glob("<Control-y>", self.redo)
        glob("<Control-x>", self.cut)
        glob("<Control-c>", self.copy)
        glob("<Control-v>", self.paste)
        glob("<Control-a>", self.select_all)
        glob("<Control-d>", self.to_next_wall)
        glob("<Delete>", self.delete_sel)
        c = self.canvas
        glob("<Return>", self._commit_pen)
        glob("<Escape>", self._escape)
        c.bind("<BackSpace>", lambda e: self._backspace())
        c.bind("<Key-x>", lambda e: None if e.state & 0x4 else self.split_at_mouse())
        c.bind("<Key-p>", lambda e: self.tool.set("pen"))
        c.bind("<Key-v>", lambda e: None if e.state & 0x4 else self.tool.set("select"))
        for key, d in (("Left", (-1, 0)), ("Right", (1, 0)), ("Up", (0, -1)), ("Down", (0, 1))):
            c.bind(f"<{key}>", lambda e, d=d: self.nudge(*d))
            c.bind(f"<Shift-{key}>", lambda e, d=d: self.nudge(d[0] * 8, d[1] * 8))

    def _typing(self):
        return isinstance(self.root.focus_get(), (tk.Entry, ttk.Entry))

    def _first_draw(self):
        self._offer_autosave()
        self._build_overview_bg()
        self._apply_zoom()
        self.redraw()
        self.canvas.focus_set()
        self.set_status(f"{self.header}: {len(self.polys)} lines")

    # ---- coordinates ----
    def to_tex(self, ev):
        x, y = self.canvas.canvasx(ev.x), self.canvas.canvasy(ev.y)
        return (x - ORG) / self.zoom, (y - ORG) / self.zoom

    def to_scr(self, u, v):
        return ORG + u * self.zoom, ORG + v * self.zoom

    @staticmethod
    def snap(u, v):
        return min(W, max(0, int(round(u)))), min(H - 1, max(0, int(round(v))))

    def constrained(self, u, v):
        """The pen's next point: snapped; with Shift, horizontal, vertical or 45 deg."""
        p = self.snap(u, v)
        if not self.pen or not self._shift:
            return p
        ax, ay = self.pen[-1]
        dx, dy = p[0] - ax, p[1] - ay
        if abs(dx) > 2 * abs(dy):
            return (p[0], ay)
        if abs(dy) > 2 * abs(dx):
            return (ax, p[1])
        d = max(abs(dx), abs(dy))
        return self.snap(ax + math.copysign(d, dx), ay + math.copysign(d, dy))

    # ---- view ----
    def _xview(self, *args):
        self.canvas.xview(*args)
        self._schedule_bg()

    def _yview(self, *args):
        self.canvas.yview(*args)
        self._schedule_bg()

    def _apply_zoom(self):
        z = self.zoom
        self.canvas.configure(scrollregion=(0, 0, W * z + 2 * ORG, H * z + 2 * ORG))
        self._schedule_bg()

    def _set_zoom(self, z, keep_u=None, at_x=None):
        z = int(max(3, min(24, z)))
        if z == self.zoom:
            return
        if keep_u is None:   # keep the center of the view
            x0 = self.canvas.canvasx(0)
            at_x = self.canvas.winfo_width() / 2
            keep_u = (x0 + at_x - ORG) / self.zoom
        self.zoom = z
        self.zoom_var.set(z)
        self._apply_zoom()
        total = W * z + 2 * ORG
        self.canvas.xview_moveto(max(0.0, (ORG + keep_u * z - at_x) / total))
        self.redraw()

    def _on_alpha(self, _):
        self.alpha = float(self.alpha_var.get())
        self._build_overview_bg()
        self.redraw_overview()
        self._schedule_bg()

    def _on_wheel(self, ev, delta=None):
        delta = delta if delta is not None else ev.delta
        if ev.state & 0x4:   # Ctrl: zoom around the mouse
            u = (self.canvas.canvasx(ev.x) - ORG) / self.zoom
            self._set_zoom(self.zoom + (1 if delta > 0 else -1), keep_u=u, at_x=ev.x)
        else:
            self.canvas.xview_scroll(-3 if delta > 0 else 3, "units")
            self._schedule_bg()

    def _schedule_bg(self):
        if not self._bg_pending:
            self._bg_pending = True
            self.root.after_idle(self._draw_bg)

    def _faded(self, img):
        return Image.blend(Image.new("RGB", img.size, (0, 0, 0)), img, self.alpha)

    def _draw_bg(self):
        """The underlay, pixel grid and wall edges, for the visible part only."""
        self._bg_pending = False
        c, z = self.canvas, self.zoom
        c.delete("bg")
        self._bg_imgs = []
        x0, x1 = c.canvasx(0), c.canvasx(c.winfo_width())
        u0 = max(0, int((x0 - ORG) / z) - 1)
        u1 = min(W, int((x1 - ORG) / z) + 2)
        if self.src is not None and u1 > u0 and self.alpha > 0:
            crop = self.src.crop((u0, 0, u1, H)).resize(((u1 - u0) * z, H * z), Image.NEAREST)
            photo = ImageTk.PhotoImage(self._faded(crop))
            self._bg_imgs.append(photo)
            # a point sits on its pixel's center
            c.create_image(ORG + u0 * z - z // 2, ORG - z // 2, anchor=tk.NW, image=photo, tags="bg")
        if self.show_grid.get() and z >= 6:
            for u in range(u0, u1 + 1):
                X = ORG + u * z
                c.create_line(X, ORG, X, ORG + (H - 1) * z, fill="#202830", tags="bg")
            for v in range(H):
                Y = ORG + v * z
                c.create_line(ORG + u0 * z, Y, ORG + u1 * z, Y, fill="#202830", tags="bg")
        # the strip's frame and the walls' edges (the arena's corners)
        c.create_rectangle(ORG, ORG, ORG + W * z, ORG + (H - 1) * z, outline="#405060", tags="bg")
        for q in range(5):
            X = ORG + q * QUARTER * z
            c.create_line(X, ORG - 16, X, ORG + (H - 1) * z + 16, fill=COL_WALL, width=2, tags="bg")
            if q < 4:
                c.create_text(X + 6, ORG - 10, anchor=tk.W, fill=COL_WALL, tags="bg",
                              text=f"WALL {q + 1}", font=("TkDefaultFont", 9, "bold"))
        c.tag_lower("bg")
        self.redraw_overview()

    def _build_overview_bg(self):
        self._ov_img = None
        if self.src is not None:
            self._ov_img = ImageTk.PhotoImage(self._faded(self.src))

    def redraw_overview(self):
        o = self.overview
        o.delete("all")
        if self._ov_img is not None:
            o.create_image(ORG, 10, anchor=tk.NW, image=self._ov_img)
        for q in range(5):
            o.create_line(ORG + q * QUARTER, 4, ORG + q * QUARTER, H + 16, fill=COL_WALL)
        for pi, p in enumerate(self.polys):
            col = COL_SEL if pi in self.sel else COL_LINE
            flat = [c for u, v in p for c in (ORG + u, 10 + v)]
            o.create_line(*flat, fill=col)
        c = self.canvas
        if c.winfo_width() > 1:
            a = (c.canvasx(0) - ORG) / self.zoom
            b = (c.canvasx(c.winfo_width()) - ORG) / self.zoom
            o.create_rectangle(ORG + max(0, a), 8, ORG + min(W, b), H + 12, outline="#ffffff")

    def _on_overview(self, ev):
        u = ev.x - ORG
        c = self.canvas
        total = W * self.zoom + 2 * ORG
        c.xview_moveto(max(0.0, (ORG + u * self.zoom - c.winfo_width() / 2) / total))
        self._schedule_bg()

    # ---- drawing the art ----
    def redraw(self):
        c, z = self.canvas, self.zoom
        c.delete("art")
        bad = 0
        w = 3 if z >= 8 else 2
        for pi, p in enumerate(self.polys):
            sel = pi in self.sel
            for a, b in zip(p, p[1:]):
                cross = crosses_wall(a, b)
                bad += cross
                col = COL_BAD if cross else (COL_SEL if sel else COL_LINE)
                c.create_line(*self.to_scr(*a), *self.to_scr(*b), fill=col,
                              width=w + (1 if sel else 0), capstyle=tk.ROUND, tags="art")
            if sel or self.tool.get() == "select" and z >= 8:
                r = 3 if sel else 2
                for u, v in p:
                    X, Y = self.to_scr(u, v)
                    c.create_rectangle(X - r, Y - r, X + r, Y + r, tags="art",
                                       fill="#ffffff" if sel else "", outline="#808080")
        self._draw_pen()
        self.redraw_overview()
        self._update_panels(bad)

    def _draw_pen(self):
        c = self.canvas
        c.delete("pen")
        if not self.pen:
            return
        pts = list(self.pen)
        if self.mouse is not None:
            pts.append(self.constrained(*self.mouse))
        if len(pts) >= 2:
            flat = [k for pt in pts for k in self.to_scr(*pt)]
            c.create_line(*flat, fill=COL_PEN, width=2, dash=(6, 3), tags="pen")
        for i, pt in enumerate(self.pen):
            X, Y = self.to_scr(*pt)
            r = 5 if i == 0 else 3
            c.create_oval(X - r, Y - r, X + r, Y + r, outline=COL_PEN,
                          fill="" if i == 0 else COL_PEN, width=2, tags="pen")

    def _update_panels(self, bad):
        segs = sum(len(p) - 1 for p in self.polys)
        length = sum(math.hypot(b[0] - a[0], b[1] - a[1])
                     for p in self.polys for a, b in zip(p, p[1:]))
        per_wall = [0, 0, 0, 0]
        for p in self.polys:
            for a, b in zip(p, p[1:]):
                if not crosses_wall(a, b):
                    per_wall[min(3, min(a[0], b[0]) // QUARTER)] += 1
        txt = (f"lines {len(self.polys)}\nsegments {segs}\n"
               f"points {sum(len(p) for p in self.polys)}\n"
               f"length {length:.0f} px\n"
               f"per wall {' / '.join(map(str, per_wall))}")
        if bad:
            txt += f"\ncrossing a corner: {bad} (red, not drawn)"
        self.stats.set(txt)
        if self.sel:
            ps = [self.polys[i] for i in sorted(self.sel)]
            us = [u for p in ps for u, _ in p]
            vs = [v for p in ps for _, v in p]
            self.sel_info.set(f"{len(ps)} line(s), {sum(len(p) for p in ps)} points\n"
                              f"x {min(us)}..{max(us)}  y {min(vs)}..{max(vs)}")
        else:
            self.sel_info.set("(none)")
        top = self.listbox.yview()[0]
        self.listbox.delete(0, tk.END)
        for pi, p in enumerate(self.polys):
            self.listbox.insert(tk.END, f"#{pi}  wall {min(4, p[0][0] // QUARTER + 1)}  "
                                        f"({p[0][0]},{p[0][1]})  {len(p)} pts")
        for i in self.sel:
            self.listbox.selection_set(i)
        self.listbox.yview_moveto(top)
        self.root.title(f"GLtron wall editor - {self.header.name}{' *' if self.dirty else ''}")

    def _on_list_select(self, _):
        self.sel = set(self.listbox.curselection())
        if self.sel:
            u = self.polys[min(self.sel)][0][0]
            c = self.canvas
            X = ORG + u * self.zoom
            if not (c.canvasx(0) < X < c.canvasx(c.winfo_width())):
                c.xview_moveto(max(0.0, (X - c.winfo_width() / 2) / (W * self.zoom + 2 * ORG)))
                self._schedule_bg()
        self.redraw()

    # ---- picking ----
    def pick_vertex(self, u, v):
        best, bd = None, PICK / self.zoom
        order = sorted(range(len(self.polys)), key=lambda i: i not in self.sel)
        for pi in order:
            for vi, (pu, pv) in enumerate(self.polys[pi]):
                d = math.hypot(u - pu, v - pv)
                if d < bd:
                    bd, best = d, (pi, vi)
            if best is not None and best[0] in self.sel:
                break
        return best

    def pick_segment(self, u, v):
        best, bd = None, PICK / self.zoom
        for pi, p in enumerate(self.polys):
            for si, (a, b) in enumerate(zip(p, p[1:])):
                d, t = dist_to_segment(u, v, a[0], a[1], b[0], b[1])
                if d < bd:
                    bd, best = d, (pi, si, t)
        return best

    # ---- mouse ----
    def _on_motion(self, ev):
        self.mouse = self.to_tex(ev)
        self._shift = bool(ev.state & 0x1)
        u, v = self.snap(*self.mouse)
        wall = min(4, u // QUARTER + 1)
        self.set_status(f"x {u}  y {v}   wall {wall} (x {u - (wall - 1) * QUARTER} on it)   "
                        f"tool {self.tool.get()}")
        if self.pen:
            self._draw_pen()

    def _on_press(self, ev):
        self.canvas.focus_set()
        self.mouse = self.to_tex(ev)
        self._shift = bool(ev.state & 0x1)
        u, v = self.mouse
        if self.tool.get() == "pen":
            pt = self.constrained(u, v)
            if len(self.pen) >= 2 and pt == self.pen[0]:
                self.pen.append(pt)
                self._commit_pen()
                return
            if not self.pen or self.pen[-1] != pt:
                self.pen.append(pt)
            self._draw_pen()
            return
        self.drag_undo = False
        hit = self.pick_vertex(u, v)
        if hit is not None:
            if hit[0] not in self.sel and not self._shift:
                self.sel = {hit[0]}
            self.drag = ("vertex", hit[0], hit[1])
            self.redraw()
            return
        seg = self.pick_segment(u, v)
        if seg is not None:
            pi = seg[0]
            if self._shift:
                self.sel ^= {pi}
            elif pi not in self.sel:
                self.sel = {pi}
            self.drag = ("move", self.snap(u, v)) if pi in self.sel else None
            self.redraw()
            return
        if not self._shift:
            self.sel = set()
        self.drag = ("band", u, v)
        self.redraw()

    def _on_drag(self, ev):
        self.mouse = self.to_tex(ev)
        if self.tool.get() != "select" or self.drag is None:
            return
        u, v = self.mouse
        kind = self.drag[0]
        if kind == "band":
            self.canvas.delete("band")
            self.canvas.create_rectangle(*self.to_scr(self.drag[1], self.drag[2]), *self.to_scr(u, v),
                                         outline="#ffffff", dash=(3, 3), tags="band")
            return
        if not self.drag_undo:
            self.push_undo()
            self.drag_undo = True
        if kind == "vertex":
            _, pi, vi = self.drag
            self.polys[pi][vi] = self.snap(u, v)
        else:
            nu, nv = self.snap(u, v)
            du, dv = nu - self.drag[1][0], nv - self.drag[1][1]
            du, dv = self._clamp_move(du, dv)
            if du == 0 and dv == 0:
                return
            for pi in self.sel:
                self.polys[pi] = [(pu + du, pv + dv) for pu, pv in self.polys[pi]]
            self.drag = ("move", (self.drag[1][0] + du, self.drag[1][1] + dv))
        self._changed()
        self.redraw()

    def _on_release(self, ev):
        if self.drag and self.drag[0] == "band":
            self.canvas.delete("band")
            u, v = self.to_tex(ev)
            ua, ub = sorted((self.drag[1], u))
            va, vb = sorted((self.drag[2], v))
            if ub - ua > 0.5 or vb - va > 0.5:
                for pi, p in enumerate(self.polys):
                    if any(ua <= pu <= ub and va <= pv <= vb for pu, pv in p):
                        self.sel.add(pi)
            self.redraw()
        self.drag = None

    def _on_double(self, ev):
        u, v = self.to_tex(ev)
        if self.tool.get() == "pen":
            # the press before this added the point already
            self._commit_pen()
            return
        seg = self.pick_segment(u, v)
        if seg is None:
            return
        pi, si, _ = seg
        pt = self.snap(u, v)
        p = self.polys[pi]
        if pt in (p[si], p[si + 1]):
            return
        self.push_undo()
        p.insert(si + 1, pt)
        self.sel = {pi}
        self._changed()
        self.redraw()

    def _on_right(self, ev):
        if self.tool.get() == "pen":
            self._commit_pen()
        else:
            self.sel = set()
            self.redraw()

    def _clamp_move(self, du, dv):
        pts = [pt for pi in self.sel for pt in self.polys[pi]]
        if not pts:
            return 0, 0
        us, vs = [p[0] for p in pts], [p[1] for p in pts]
        du = max(-min(us), min(W - max(us), du))
        dv = max(-min(vs), min(H - 1 - max(vs), dv))
        return du, dv

    # ---- pen ----
    def _commit_pen(self):
        if len(self.pen) >= 2:
            self.push_undo()
            self.polys.append(list(self.pen))
            self.sel = {len(self.polys) - 1}
            self._changed()
            self.set_status(f"line #{len(self.polys) - 1}: {len(self.pen)} points")
        self.pen = []
        self.redraw()

    def _escape(self):
        if self.pen:
            self.pen = []
            self._draw_pen()
        elif self.sel:
            self.sel = set()
            self.redraw()

    def _backspace(self):
        if self.tool.get() == "pen":
            if self.pen:
                self.pen.pop()
                self._draw_pen()
            return
        if self.mouse is None:
            return
        hit = self.pick_vertex(*self.mouse)
        if hit is None:
            return
        pi, vi = hit
        self.push_undo()
        del self.polys[pi][vi]
        if len(self.polys[pi]) < 2:
            del self.polys[pi]
            self.sel = set()
        self._changed()
        self.redraw()

    def _on_tool(self):
        self._commit_pen()          # a line being drawn is kept, not dropped

    # ---- edits ----
    def push_undo(self):
        self.undo_stack.append(deepcopy(self.polys))
        if len(self.undo_stack) > 200:
            self.undo_stack.pop(0)
        self.redo_stack.clear()

    def undo(self):
        if self.undo_stack:
            self.redo_stack.append(deepcopy(self.polys))
            self.polys = self.undo_stack.pop()
            self.sel = set()
            self._changed()
            self.redraw()

    def redo(self):
        if self.redo_stack:
            self.undo_stack.append(deepcopy(self.polys))
            self.polys = self.redo_stack.pop()
            self.sel = set()
            self._changed()
            self.redraw()

    def select_all(self):
        self.tool.set("select")
        self.sel = set(range(len(self.polys)))
        self.redraw()

    def delete_sel(self):
        if not self.sel:
            return
        self.push_undo()
        self.polys = [p for i, p in enumerate(self.polys) if i not in self.sel]
        self.sel = set()
        self._changed()
        self.redraw()

    def copy(self):
        self.clipboard = [list(self.polys[i]) for i in sorted(self.sel)]
        self.set_status(f"copied {len(self.clipboard)} line(s)")

    def cut(self):
        self.copy()
        self.delete_sel()

    def paste(self):
        if not self.clipboard:
            return
        self.push_undo()
        n = len(self.polys)
        self.polys += deepcopy(self.clipboard)
        self.sel = set(range(n, len(self.polys)))
        self.tool.set("select")
        self._changed()
        self.redraw()
        self.set_status(f"pasted {len(self.clipboard)} line(s) in place: drag or arrows to move")

    def to_next_wall(self):
        if not self.sel:
            return
        self.push_undo()
        n = len(self.polys)
        for i in sorted(self.sel):
            p = self.polys[i]
            shift = QUARTER if max(u for u, _ in p) + QUARTER <= W else QUARTER - W
            self.polys.append([(u + shift, v) for u, v in p])
        self.sel = set(range(n, len(self.polys)))
        self._changed()
        self.redraw()

    def nudge(self, du, dv):
        if self.tool.get() != "select" or not self.sel:
            return
        du, dv = self._clamp_move(du, dv)
        if du == 0 and dv == 0:
            return
        self.push_undo()
        for i in self.sel:
            self.polys[i] = [(u + du, v + dv) for u, v in self.polys[i]]
        self._changed()
        self.redraw()

    def _mirror(self, horizontal):
        if not self.sel:
            return
        pts = [pt for i in self.sel for pt in self.polys[i]]
        if horizontal:
            m = min(p[0] for p in pts) + max(p[0] for p in pts)
            fn = lambda u, v: (m - u, v)
        else:
            m = min(p[1] for p in pts) + max(p[1] for p in pts)
            fn = lambda u, v: (u, m - v)
        self.push_undo()
        for i in self.sel:
            self.polys[i] = [fn(u, v) for u, v in self.polys[i]]
        self._changed()
        self.redraw()

    def split_at_mouse(self):
        """Remove the segment under the mouse; its line splits in two."""
        if self.mouse is None:
            return
        seg = self.pick_segment(*self.mouse)
        if seg is None:
            self.set_status("cut: no segment under the mouse")
            return
        pi, si, _ = seg
        self.push_undo()
        p = self.polys[pi]
        halves = [h for h in (p[:si + 1], p[si + 1:]) if len(h) >= 2]
        self.polys[pi:pi + 1] = halves
        self.sel = set()
        self._changed()
        self.redraw()

    # ---- file ----
    def _changed(self):
        self.dirty = True
        if not self._autosave_pending:
            self._autosave_pending = True
            self.root.after(1000, self._write_autosave)

    def _write_autosave(self):
        self._autosave_pending = False
        if self.dirty:
            try:
                save_header(self.autosave, self.polys)
            except OSError as e:
                self.set_status(f"autosave failed: {e}")

    def _drop_autosave(self):
        try:
            self.autosave.unlink()
        except OSError:
            pass

    def _offer_autosave(self):
        if not self.autosave.exists():
            return
        try:
            polys = load_header(self.autosave)
        except (OSError, ValueError):
            return
        if messagebox.askyesno("Wall editor",
                               f"Unsaved work from the last session was kept "
                               f"({len(polys)} lines, {self.autosave.name}).\n\n"
                               f"Restore it?"):
            self.polys = polys
            self.dirty = True
        else:
            self._drop_autosave()

    def save(self):
        self._commit_pen()
        if self.header.exists() and not self.backed_up:
            shutil.copyfile(self.header, self.header.with_name(self.header.name + ".bak"))
            self.backed_up = True
        self.polys = save_header(self.header, self.polys)
        self.sel = {i for i in self.sel if i < len(self.polys)}
        self.dirty = False
        self._drop_autosave()
        self.redraw()
        self.set_status(f"saved {self.header}: {len(self.polys)} lines")

    def _on_close(self):
        self._commit_pen()
        if self.dirty:
            r = messagebox.askyesnocancel("Wall editor", "Save the changes to wall_art.h?")
            if r is None:
                return
            if r:
                self.save()
            else:
                self._drop_autosave()
        self.root.destroy()

    def set_status(self, s):
        self.status.set(s)

    _shift = False


def main():
    ap = argparse.ArgumentParser(description="Edit the GLtron port's wall art")
    ap.add_argument("--header", default=str(DEFAULT_HEADER), help="wall_art.h to edit")
    ap.add_argument("--underlay", default=str(DEFAULT_UNDERLAY), help="texture shown under the lines")
    args = ap.parse_args()
    root = tk.Tk()
    WallEditor(root, args.header, args.underlay)
    root.mainloop()


if __name__ == "__main__":
    main()

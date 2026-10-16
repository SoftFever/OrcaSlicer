#!/usr/bin/env python3
"""Run the vendored SolidCraft oracle corpus (tests/cad_oracle/*) against a live Orca-CAD over its MCP socket.

    ORCA_CAD_MCP=/tmp/orca-test.sock python3 scripts/CAD/oracle_runner.py [--filter 03] [--report out.json]

Each case is a kernel-neutral recipe.json plus expected.json (analytic or FreeCAD-measured). The runner replays
the recipe through the app's MCP tools, then compares volume / area / centre of mass / bbox / body count.

Statuses: PASS, FAIL (built but numbers differ, or the app refused), XFAIL (known, see XFAIL), BLOCKED (the MCP surface cannot express an op
yet: the reason is the work item that unblocks it). The blocked list is the P1/P4 burn-down of the SolidCraft
integration plan; exit code is 1 only on FAIL, so growing coverage never turns the gate red by itself.

Tolerances follow tests/cad_oracle/README.md: relative 1e-3 on volume/area, 1e-3 of the bbox diagonal on COM/bbox.
Needs a FRESH document and the Design tab selected (same preconditions as smoke_golden_path.py).
"""
import argparse, glob, json, math, os, socket, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.join(HERE, "..", "..", "tests", "cad_oracle")


# Cases that run but disagree with the oracle for a known kernel reason. Reported XFAIL (not a red gate);
# an XFAIL case that starts passing is reported XPASS so the entry gets deleted.
XFAIL = {
    "05-taper-extrude": "extrude taper: top outline 0.11 mm/side short of the exact frustum (offset-based taper)",
    "58-phone-stand": "two 3 mm fillets on edges 6 mm apart: 'no suitable edges for chamfer or fillet' (cause not analysed)",
    "43-cut-splits-body": "a cut that leaves two solids stays ONE body (compound); oracle expects two bodies",
}


class Blocked(Exception):
    """The MCP surface cannot express this yet. Message = what is missing."""


class Mcp:
    def __init__(self, path):
        self.path, self.n = path, 0

    def call(self, method, params=None):
        self.n += 1
        req = {"jsonrpc": "2.0", "id": self.n, "method": method}
        if params is not None:
            req["params"] = params
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(120)
        s.connect(self.path)
        s.sendall((json.dumps(req) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(1 << 16)
            if not chunk:
                break
            buf += chunk
        s.close()
        r = json.loads(buf.decode())
        if "error" in r:
            raise RuntimeError("%s: %s" % (method, r["error"].get("message", r["error"])))
        out = r["result"]
        if isinstance(out, dict) and out.get("ok") is False:
            raise RuntimeError("%s refused: %s" % (method, out.get("error") or out))
        return out


# ---- recipe -> MCP -----------------------------------------------------------------------------

# The recipe's sketch frames (x_dir, y_dir, normal) against the app's SketchPlane: XY is identical; the app's XZ has
# y = +Z (recipe -Z) and its YZ has x = Y, y = Z (recipe x = -Z, y = Y). to_app maps recipe (u, v) -> app (a, b).
PLANES = {
    "XY": ("XY", lambda u, v: (u, v)),
    "XZ": ("XZ", lambda u, v: (u, -v)),
    "YZ": ("YZ", lambda u, v: (v, -u)),
}
# world axis -> (plane, app revolve axis index 0 = plane x, 1 = plane y) for a revolve about a world axis through the origin
AXES = {("XY", "x"): 0, ("XY", "y"): 1, ("XZ", "x"): 0, ("XZ", "z"): 1, ("YZ", "y"): 0, ("YZ", "z"): 1}


def plane_of(sk):
    p = sk["plane"]
    ref = p.get("ref")
    if ref not in PLANES:
        raise Blocked("sketch plane %r (offset/face planes need the datum-plane tools)" % ref)
    if any(abs(c) > 1e-9 for c in p.get("origin", [0, 0, 0])):
        raise Blocked("sketch plane with non-zero origin")
    return PLANES[ref]


def sketch_entities(sk, to_app):
    """App-format entities for the recipe sketch; Blocked for kinds the app's sketch_add cannot take."""
    out = []
    for e in sk["entities"]:
        t, c = e["type"], bool(e.get("construction", False))
        if t == "line":
            out.append({"type": "line", "p0": list(to_app(*e["start"]["at"])), "p1": list(to_app(*e["end"]["at"])), "construction": c})
        elif t == "circle":
            out.append({"type": "circle", "center": list(to_app(*e["center"]["at"])), "radius": e["radius"], "construction": c})
        elif t == "arc_three_point":
            out.append(arc_from_three(to_app(*e["start"]["at"]), to_app(*e["mid"]), to_app(*e["end"]["at"]), c))
        elif t == "arc_center_start_sweep":
            ctr, st = to_app(*e["center"]["at"]), to_app(*e["start"]["at"])
            r = math.dist(ctr, st)
            a0 = math.atan2(st[1] - ctr[1], st[0] - ctr[0])
            sweep = math.radians(e["sweep_deg"])
            if mirrored(to_app):
                sweep = -sweep
            lo, hi = (a0, a0 + sweep) if sweep >= 0 else (a0 + sweep, a0)
            out.append({"type": "arc", "center": list(ctr), "radius": r, "start_angle": lo, "end_angle": hi, "construction": c})
        elif t == "point":
            out.append({"type": "point", "p": list(to_app(*e["at"])), "construction": c})
        else:
            raise Blocked("sketch entity %r" % t)
    return out


def mirrored(to_app):
    a, b = to_app(1, 0), to_app(0, 1)
    return a[0] * b[1] - a[1] * b[0] < 0


def arc_from_three(p, m, q, construction):
    ax, ay = p; bx, by = m; cx, cy = q
    d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    if abs(d) < 1e-12:
        raise Blocked("degenerate three-point arc")
    ux = ((ax**2 + ay**2) * (by - cy) + (bx**2 + by**2) * (cy - ay) + (cx**2 + cy**2) * (ay - by)) / d
    uy = ((ax**2 + ay**2) * (cx - bx) + (bx**2 + by**2) * (ax - cx) + (cx**2 + cy**2) * (bx - ax)) / d
    ang = lambda x, y: math.atan2(y - uy, x - ux)
    s, mm, e = ang(ax, ay), ang(bx, by), ang(cx, cy)
    ccw = lambda a0, a1: (a1 - a0) % (2 * math.pi)       # CCW distance from a0 to a1
    if ccw(s, mm) > ccw(s, e):                           # the middle point is not on the CCW way s->e: the arc runs CW
        s, e = e, s
    return {"type": "arc", "center": [ux, uy], "radius": math.hypot(ax - ux, ay - uy), "start_angle": s, "end_angle": e, "construction": construction}


BOOL = {"new": "new", "add": "union", "join": "union", "remove": "subtract", "cut": "subtract", "intersect": "intersect"}


class Replay:
    def __init__(self, mcp):
        self.m, self.sketches, self.body = mcp, {}, 0

    def run(self, ft):
        h = getattr(self, "op_" + ft["op"], None)
        if h is None:
            raise Blocked("op %s" % ft["op"])
        h(ft)

    def op_sketch(self, ft):
        if ft.get("dimensions"):   # positions in the recipe are pre-solve; the expected values come from the solved sketch
            raise Blocked("dimension-driven sketch (needs sketch_constrain / sketch_dimension over MCP)")
        plane, to_app = plane_of(ft)
        ents = sketch_entities(ft, to_app)
        self.m.call("sketch_begin", {"plane": plane})
        try:
            self.m.call("sketch_add", {"entities": ents})
        except Exception:
            self.m.call("sketch_cancel")
            raise
        idx = self.m.call("sketch_commit")["sketch_index"]
        solid = {e["id"] for e in ft["entities"] if e["type"] != "point" and not e.get("construction")}
        self.sketches[ft["name"]] = (idx, plane, solid)

    def _sketch(self, prof):
        idx, plane, solid = self.sketches[prof["sketch"]]
        used = {c for lp in prof.get("loops", []) for c in lp["curves"]}
        if used != solid:   # the app extrudes every region of the sketch
            raise Blocked("profile is a subset of the sketch's curves")
        return idx, plane

    def op_extrude(self, ft):
        if ft.get("thin"):
            raise Blocked("thin extrude")
        if len(ft["profiles"]) != 1:
            raise Blocked("extrude of several profiles")
        idx, _ = self._sketch(ft["profiles"][0])
        ex = ft["extent"]
        et = ex.get("type", "distance")
        p = {"sketch": idx, "boolean": BOOL[ft["operation"]]}
        if et == "distance":
            if ex.get("taper_deg"):
                p["taper"] = ex["taper_deg"]
            p["distance"] = ex["distance"]
            p["flip"] = ex.get("direction") == "negative"
        elif et == "through_all":
            p.update(end="through_all", distance=1.0, flip=ex.get("direction") == "negative")
        elif et == "symmetric":
            p.update(end="symmetric", distance=2 * ex["distance"])   # the app's symmetric distance is the total; the recipe's is per side
        else:
            raise Blocked("extent %s" % et)
        self.m.call("extrude", p)

    def op_revolve(self, ft):
        idx, plane = self._sketch(ft["profile"])
        d = ft["axis"].get("dir", [0, 0, 1])
        if any(abs(c) > 1e-9 for c in ft["axis"].get("origin", [0, 0, 0])):
            raise Blocked("revolve axis off the origin")
        name = "xyz"[max(range(3), key=lambda i: abs(d[i]))]
        if (plane, name) not in AXES:
            raise Blocked("revolve about world %s from a %s sketch" % (name, plane))
        self.m.call("revolve", {"sketch": idx, "axis": AXES[(plane, name)], "angle": ft.get("angle", 360), "boolean": BOOL[ft["operation"]]})

    def _edges(self, ft):
        """Edge ids of the last body nearest to each recipe edge midpoint."""
        self.body = len(self.m.call("describe_scene")["bodies"]) - 1   # the app edits the last body by default
        topo = self.m.call("query_topology", {"body": self.body})
        ids = []
        for sel in ft["edges"]:
            if sel.get("type") != "line":   # query_topology gives only the end points; an arc's midpoint is not theirs
                raise Blocked("edge selection with a non-line edge (needs sampled edge points in query_topology)")
            pt = sel["point"]
            best = min(topo["edges"], key=lambda e: math.dist([(a + b) / 2 for a, b in zip(e["p0"], e["p1"])], pt))
            ids.append(best["id"])
        return ids, topo.get("generation")

    def op_fillet(self, ft):
        if ft.get("type") != "constant_radius":
            raise Blocked("fillet type %s" % ft.get("type", "(none: variable/chord set)"))
        ids, gen = self._edges(ft)
        self.m.call("fillet", {"edge": ids, "radius": ft["radius"], "body": self.body, "generation": gen})

    def op_chamfer(self, ft):
        if ft.get("type") != "equal_distance":
            raise Blocked("chamfer type %s" % ft.get("type"))
        ids, gen = self._edges(ft)
        self.m.call("chamfer", {"edge": ids, "distance": ft["distance"], "body": self.body, "generation": gen})


# ---- compare -----------------------------------------------------------------------------------

def measure(mcp):
    scene = mcp.call("describe_scene")
    origin = scene.get("modeling_origin", [0, 0, 0])
    vol = area = 0.0
    com = [0.0, 0.0, 0.0]
    lo, hi = [1e18] * 3, [-1e18] * 3
    for b in scene["bodies"]:
        mp = mcp.call("mass_properties", {"body": b["index"]})
        v = mp.get("volume", 0.0)
        vol += v
        area += mp.get("surface_area", 0.0)
        for i in range(3):
            com[i] += v * mp.get("center_of_mass", [0, 0, 0])[i]
        bb = b.get("bbox")
        if bb:
            lo = [min(a, c) for a, c in zip(lo, bb["min"])]
            hi = [max(a, c) for a, c in zip(hi, bb["max"])]
    com = [c / vol for c in com] if vol else com
    return {"bodies": len(scene["bodies"]), "volume": vol, "area": area, "com": com, "lo": lo, "hi": hi}, origin


def compare(exp, got):
    errs = []
    rel = lambda a, b: abs(a - b) / max(abs(b), 1e-12)
    tol = exp.get("tolerance", {})
    rt = tol.get("relative", 1e-3)
    if exp.get("body_count") is not None and got["bodies"] != exp["body_count"]:
        errs.append("bodies %d != %d" % (got["bodies"], exp["body_count"]))
    if exp.get("volume_mm3") is not None and rel(got["volume"], exp["volume_mm3"]) > rt:
        errs.append("volume %.4f vs %.4f" % (got["volume"], exp["volume_mm3"]))
    if exp.get("area_mm2") is not None and rel(got["area"], exp["area_mm2"]) > rt:
        errs.append("area %.4f vs %.4f" % (got["area"], exp["area_mm2"]))
    bb = exp.get("bbox")
    diag = math.dist(bb["min"], bb["max"]) if bb else 1.0
    at = tol.get("bbox_diag", 1e-3) * diag
    if exp.get("center_of_mass") and math.dist(got["com"], exp["center_of_mass"]) > at:
        errs.append("com %s vs %s" % ([round(c, 3) for c in got["com"]], exp["center_of_mass"]))
    if bb and (math.dist(got["lo"], bb["min"]) > at * 2 or math.dist(got["hi"], bb["max"]) > at * 2):
        errs.append("bbox %s..%s vs %s..%s" % ([round(c, 2) for c in got["lo"]], [round(c, 2) for c in got["hi"]], bb["min"], bb["max"]))
    return errs


def run_case(mcp, d):
    recipe = json.load(open(os.path.join(d, "recipe.json")))
    exp = json.load(open(os.path.join(d, "expected.json")))
    try:
        mcp.call("new_document")
        rp = Replay(mcp)
        for ft in recipe["features"]:
            rp.run(ft)
        got, _ = measure(mcp)   # sketch_begin sketches sit at the absolute origin; only extrude{profile} adds modeling_origin (MCP inconsistency, see bd)
        errs = compare(exp, got)
        name = os.path.basename(d)
        if errs:
            return ("XFAIL", XFAIL[name]) if name in XFAIL else ("FAIL", "; ".join(errs))
        return ("XPASS", "remove from XFAIL") if name in XFAIL else ("PASS", "")
    except Blocked as b:
        return "BLOCKED", str(b)
    except Exception as e:  # noqa: BLE001 - report, never raise
        name = os.path.basename(d)
        return ("XFAIL", XFAIL[name]) if name in XFAIL else ("FAIL", str(e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="")
    ap.add_argument("--sock", default=os.environ.get("ORCA_CAD_MCP", "/tmp/orca-cad-mcp.sock"))
    ap.add_argument("--report")
    a = ap.parse_args()
    mcp = Mcp(a.sock)
    cases = sorted(d for d in glob.glob(os.path.join(CORPUS, "*")) if os.path.isfile(os.path.join(d, "recipe.json")) and a.filter in os.path.basename(d))
    res = []
    for d in cases:
        st, why = run_case(mcp, d)
        res.append({"case": os.path.basename(d), "status": st, "why": why})
        print("%-34s %-8s %s" % (os.path.basename(d), st, why))
    n = {k: sum(1 for r in res if r["status"] == k) for k in ("PASS", "FAIL", "XFAIL", "XPASS", "BLOCKED")}
    print("  ".join("%s %d" % (k, v) for k, v in n.items()) + "  of %d" % len(res))
    if a.report:
        json.dump(res, open(a.report, "w"), indent=1)
    return 1 if n["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())

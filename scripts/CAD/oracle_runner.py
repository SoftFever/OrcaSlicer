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

PLANES = {"XY": "XY"}   # ponytail: XZ/YZ frame conventions not yet verified against the app; add after a rig check


def plane_name(sk):
    p = sk["plane"]
    ref = p.get("ref")
    if ref not in PLANES:
        raise Blocked("sketch plane %r (only XY at the origin; offset/face planes need extrude{sketch})" % ref)
    if any(abs(c) > 1e-9 for c in p.get("origin", [0, 0, 0])):
        raise Blocked("sketch plane with non-zero origin")
    return PLANES[ref]


def loop_polygon(sk, loop):
    """Ordered [[x,y],...] for a loop made only of straight lines; else Blocked."""
    ents = {e["id"]: e for e in sk["entities"]}
    pts = []
    for cid in loop["curves"]:
        e = ents[cid]
        if e["type"] != "line":
            raise Blocked("sketch entity %r in a profile (MCP profile is a polygon; needs extrude{sketch})" % e["type"])
        pts.append((e["start"]["at"], e["end"]["at"]))
    # chain the segments in the given order, flipping one when it is stored end-first; poly = the corner list
    poly, cur = [list(pts[0][0])], list(pts[0][1])
    for a, b in pts[1:]:
        if math.dist(a, cur) > 1e-6 and math.dist(b, cur) > 1e-6:
            raise Blocked("profile loop is not chained in order")
        poly.append(cur)
        cur = list(b if math.dist(a, cur) <= 1e-6 else a)
    if math.dist(cur, poly[0]) > 1e-6:
        raise Blocked("profile loop is not closed")
    return poly


BOOL = {"new": "new", "add": "union", "join": "union", "remove": "subtract", "cut": "subtract", "intersect": "intersect"}


class Replay:
    def __init__(self, mcp):
        self.m, self.sketches = mcp, {}

    def run(self, ft):
        h = getattr(self, "op_" + ft["op"], None)
        if h is None:
            raise Blocked("op %s" % ft["op"])
        h(ft)

    def op_sketch(self, ft):
        if ft.get("dimensions"):   # positions in the recipe are pre-solve; the expected values come from the solved sketch
            raise Blocked("dimension-driven sketch (needs sketch_constrain / sketch_dimension over MCP)")
        self.sketches[ft["name"]] = ft

    def _profile(self, prof):
        if len(prof.get("loops", [])) != 1 or not prof["loops"][0].get("outer", True):
            raise Blocked("profile with several loops / inner loops")
        sk = self.sketches[prof["sketch"]]
        return plane_name(sk), loop_polygon(sk, prof["loops"][0])

    def op_extrude(self, ft):
        if ft.get("thin"):
            raise Blocked("thin extrude")
        if len(ft["profiles"]) != 1:
            raise Blocked("extrude of several profiles")
        plane, poly = self._profile(ft["profiles"][0])
        ex = ft["extent"]
        et = ex.get("type", "distance")
        p = {"plane": plane, "profile": poly, "boolean": BOOL[ft["operation"]]}
        if et == "distance":
            if ex.get("taper_deg"):
                p["taper"] = ex["taper_deg"]
            p["distance"] = ex["distance"]
            p["flip"] = ex.get("direction") == "negative"
        elif et == "through_all":
            p.update(end="through_all", distance=1.0, flip=ex.get("direction") == "negative")
        else:
            raise Blocked("extent %s" % et)
        self.m.call("extrude", p)

    def op_revolve(self, ft):
        plane, poly = self._profile(ft["profile"])
        ax = ft["axis"].get("dir", [0, 0, 1])
        raise Blocked("revolve axis %s (MCP axis is plane X/Y; frame mapping unverified)" % ax)

    def _edges(self, ft):
        """Edge ids of the last body nearest to each recipe edge midpoint."""
        self.body = len(self.m.call("describe_scene")["bodies"]) - 1   # the app edits the last body by default
        topo = self.m.call("query_topology", {"body": self.body})
        ids = []
        org = self.m.call("describe_scene").get("modeling_origin", [0, 0, 0])   # app world = design coords + origin
        for sel in ft["edges"]:
            pt = [c + o for c, o in zip(sel["point"], org)]
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
        got, origin = measure(mcp)
        if any(abs(o) > 1e-9 for o in origin):   # app reports world = modeling_origin + design coords
            got["com"] = [c - o for c, o in zip(got["com"], origin)]
            got["lo"] = [c - o for c, o in zip(got["lo"], origin)]
            got["hi"] = [c - o for c, o in zip(got["hi"], origin)]
        errs = compare(exp, got)
        name = os.path.basename(d)
        if errs:
            return ("XFAIL", XFAIL[name]) if name in XFAIL else ("FAIL", "; ".join(errs))
        return ("XPASS", "remove from XFAIL") if name in XFAIL else ("PASS", "")
    except Blocked as b:
        return "BLOCKED", str(b)
    except Exception as e:  # noqa: BLE001 - report, never raise
        return "FAIL", str(e)


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

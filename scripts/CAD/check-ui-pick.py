#!/usr/bin/env python3
"""Picking: the SELECT filter and Select Other candidates / click cycling. Drives the real click path (ui.click)
against a RUNNING app with the Design tab visible.

    python3 scripts/CAD/check-ui-pick.py [socket]      # default /tmp/orca-cad-mcp.sock
Exit 0 = every assertion held.
"""
import json, socket, sys, time

SOCK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/orca-cad-mcp.sock"
_n = 0


def call(method, **params):
    global _n
    _n += 1
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(60)
    s.connect(SOCK)
    s.sendall((json.dumps({"jsonrpc": "2.0", "id": _n, "method": method, "params": params}) + "\n").encode())
    buf = b""
    while b"\n" not in buf:
        d = s.recv(65536)
        if not d:
            break
        buf += d
    s.close()
    r = json.loads(buf.decode())
    if "error" in r:
        raise RuntimeError("%s: %s" % (method, r["error"]))
    return r["result"]


fails = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)


def sel():
    time.sleep(0.3)
    return call("ui.inspect")["solid_selection"]


try:
    call("sketch_cancel")
except RuntimeError:
    pass
call("new_document")
call("extrude", width=40, height=40, distance=10)
call("screenshot", path="/tmp/ui-pick-0.png", view="iso", fit=True)   # brings the Design tab forward
time.sleep(0.8)
c = call("ui.inspect")["canvas"]
mid = [c["width"] / 2, c["height"] / 2]    # the fitted box fills the middle of the view

call("ui.candidates", filter={"face": True, "edge": True, "vertex": True}, cycle=False)
cand = call("ui.candidates", at=mid)["candidates"]
check(len(cand) >= 2, "a ray through the box crosses several faces: %s" % cand)
check(cand and cand[0]["kind"] == "face", "the first candidate is the face a plain click takes: %s" % cand[:1])
check(len({(x["body"], x["face"]) for x in cand}) == len(cand), "one candidate per face")

# SELECT filter: with faces off, the middle of a face is nothing
f = call("ui.candidates", filter={"face": False, "edge": True, "vertex": True}, at=mid)
check(f["candidates"] == [] and not f["filter"]["face"], "faces filtered out: nothing under the middle of a face")
call("ui.click", at=mid)
check(sel()["faces"] == [], "and a click there selects nothing")
call("ui.candidates", filter={"face": True, "edge": True, "vertex": True})

# click cycling: three clicks on the same spot walk the candidate list
call("ui.candidates", cycle=True)
seen = []
for _ in range(min(3, len(cand))):
    call("ui.click", at=mid)
    s = sel()["faces"]
    seen.append(s[0] if s else None)
    time.sleep(0.4)   # past the double-click interval, so each press is its own click
check(all(x is not None for x in seen), "every click picked a face: %s" % seen)
check(len({tuple(x) for x in seen if x}) == len(seen), "each click took a different face: %s" % seen)
check(seen and cand and seen[0] == [cand[0]["body"], cand[0]["face"]], "the first click takes the top candidate")
other = [mid[0] + 40, mid[1] + 40]
top2 = call("ui.candidates", at=other)["candidates"]
call("ui.click", at=other)                        # a different spot restarts the cycle at the top
s = sel()["faces"]
check(top2 and s and s[0] == [top2[0]["body"], top2[0]["face"]], "moving restarts the cycle at the top candidate: %s vs %s" % (s, top2[:1]))

# Select Other: a press held 0.5 s opens the list instead of clicking; hover previews a row, a row takes it
call("ui.candidates", cycle=False)
call("ui.key", key="Escape")
call("ui.click", at=[c["width"] - 30, 120])          # bare canvas: clears the selection
call("ui.click", at=mid, hold_ms=800)
time.sleep(0.5)
st = call("ui.inspect")
om = st["other_menu"]
check(om["open"] and len(om["labels"]) == len(cand), "a held press opens the list with every candidate: %s" % om["labels"])
check(st["solid_selection"]["faces"] == [], "and the held press is not also a click")
call("ui.candidates", other_hover=1)
time.sleep(0.5)
check(call("ui.inspect")["other_menu"]["hover"] == 1, "hovering a row is reported")
check(call("ui.inspect")["solid_selection"]["faces"] == [], "a preview does not select")
call("ui.candidates", other_choose=1)
time.sleep(0.5)
st = call("ui.inspect")
check(not st["other_menu"]["open"], "choosing a row closes the list")
check(st["solid_selection"]["faces"] == [[cand[1]["body"], cand[1]["face"]]], "and takes that row: %s" % st["solid_selection"]["faces"])
call("ui.click", at=mid)                              # a quick click still just clicks
check(not call("ui.inspect")["other_menu"]["open"], "a quick click does not open the list")
print("ui pick:", "PASS" if not fails else "%d FAIL" % len(fails))
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
"""Sketch drawing gestures through the real event path: a two-point shape is drawn click-click OR by one
press-drag-release. Needs a RUNNING app with the Design tab visible.

    python3 scripts/CAD/check-ui-sketch-draw.py [socket]      # default /tmp/orca-cad-mcp.sock
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


def ents():
    time.sleep(0.3)
    return call("sketch_describe")["entities"]


try:
    call("sketch_cancel")
except RuntimeError:
    pass
call("new_document")
call("extrude", width=40, height=40, distance=10)
call("screenshot", path="/tmp/ui-draw-0.png", view="iso", fit=True)
time.sleep(0.5)
org = call("describe_scene")["modeling_origin"]
top = call("ui.worldToScreen", point=[org[0], org[1], 10.0])
call("ui.click", at=[top["x"], top["y"]])
call("run_verb", verb="sketch")
time.sleep(0.5)
c = call("ui.inspect")["canvas"]
cx, cy = c["width"] / 2, c["height"] / 2

call("ui.key", key="C")                                          # Circle (centre, radius)
call("ui.drag", **{"from": [cx, cy], "to": [cx + 90, cy], "steps": 6})   # press, drag, release = two points
e = ents()
check(len(e) == 1, "one press-drag-release draws a whole circle: %d entit(ies)" % len(e))
call("ui.key", key="Escape")
call("ui.key", key="C")
call("ui.click", at=[cx - 150, cy - 100])                         # click, click still works
time.sleep(0.6)
call("ui.click", at=[cx - 150, cy - 40])
e = ents()
check(len(e) == 2, "click-click still draws a circle: %d entities" % len(e))
call("ui.key", key="Escape")
call("sketch_cancel")
print("ui sketch draw:", "PASS" if not fails else "%d FAIL" % len(fails))
sys.exit(1 if fails else 0)

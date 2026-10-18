#!/usr/bin/env python3
"""Timeline (on-canvas ImGui tree): dragging a row onto another reorders the history, and stops where the
dependencies refuse. Drives real mouse input (ui.drag) against a RUNNING app with the Design tab visible.

    python3 scripts/CAD/check-ui-timeline.py [socket]      # default /tmp/orca-cad-mcp.sock
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


def names():
    return [f["name"] for f in call("describe_scene")["features"]]


def rows():
    for _ in range(10):
        t = call("ui.inspect").get("tree")
        if t and t["rows_y"] and t["rows_y"][0] > 0:
            return t
        time.sleep(0.4)
    return {"rows_y": [], "x": 0}


try:
    call("sketch_cancel")
except RuntimeError:
    pass
call("new_document")
call("ui_option", name="imgui_tree", value=True)
for nm in "ABC":
    i = call("plane", base=0, offset=10)["plane_index"]
    call("rename_feature", feature=i, name=nm)
call("screenshot", path="/tmp/ui-tl-0.png", view="iso", fit=True)
time.sleep(1.0)
check(names() == ["A", "B", "C"], "three independent planes: %s" % names())

t = rows()
check(len(t["rows_y"]) == 3, "the tree reports its rows: %s" % t)
x, ys = t["x"], t["rows_y"]
call("ui.drag", **{"from": [x, ys[0]], "to": [x, ys[2]], "steps": 6, "step_ms": 200})
time.sleep(1.0)
check(names() == ["B", "C", "A"], "dragging A onto C moves it to the end: %s" % names())

# a sketch cannot go below the feature that uses it
call("new_document")
call("extrude", width=20, height=20, distance=5)
call("screenshot", path="/tmp/ui-tl-1.png", view="iso", fit=True)
time.sleep(1.0)
before = names()
t = rows()
x, ys = t["x"], t["rows_y"]
check(len(ys) >= 2, "sketch + extrude rows: %s" % before)
call("ui.drag", **{"from": [x, ys[0]], "to": [x, ys[1]], "steps": 6, "step_ms": 200})
time.sleep(1.0)
check(names() == before, "the sketch cannot move below its extrude: %s" % names())

call("ui_option", name="imgui_tree", value=False)
print("ui timeline:", "PASS" if not fails else "%d FAIL" % len(fails))
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
"""Local, dependency-free API for the Exam OS learning dashboard."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WEB = Path(__file__).resolve().parent
PORT = int(__import__("os").environ.get("OSLAB_PORT", "8766"))


def integer(data, key, low, high, default=None):
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{key} must be between {low} and {high}")
    return value


def choice(data, key, allowed, default=None):
    value = data.get(key, default)
    if value not in allowed:
        raise ValueError(f"{key} must be one of: {', '.join(allowed)}")
    return value


def run(args, timeout=90):
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError((result.stderr or result.stdout or "Command failed").strip()[-1800:])
    rows = {}
    for line in result.stdout.splitlines():
        if ": " in line:
            key, value = line.split(": ", 1)
            rows[key] = value
    return {"output": result.stdout, "values": rows}


def build(target):
    result = subprocess.run(["make", target], cwd=ROOT, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError((result.stderr or result.stdout or "Build failed").strip()[-1800:])


def dispatch(data):
    action = data.get("action")
    if action == "build":
        target = choice(data, "target", ["all", "synclab", "memlab", "fslab", "pagesim", "disksim", "deadsim"])
        build(target)
        return {"built": target}
    if action == "counter":
        mode = choice(data, "mode", ["none", "mutex", "sem", "peterson"])
        threads = integer(data, "threads", 1, 16, 2)
        if mode == "peterson" and threads != 2:
            raise ValueError("Peterson's solution requires exactly two threads")
        iters = integer(data, "iters", 1, 50000000, 200000)
        build("synclab")
        return run(["./synclab", "counter", "--mode", mode, "--threads", str(threads), "--iters", str(iters)])
    if action == "rw":
        policy = choice(data, "policy", ["none", "reader", "writer", "fair"], "fair")
        readers, writers = integer(data, "readers", 1, 16, 4), integer(data, "writers", 1, 16, 2)
        ops, hold = integer(data, "ops", 1, 100000, 100), integer(data, "hold", 0, 100000, 100)
        build("synclab")
        return run(["./synclab", "rw", "--policy", policy, "--readers", str(readers), "--writers", str(writers), "--ops", str(ops), "--hold-us", str(hold)])
    if action == "philosophers":
        mode = choice(data, "mode", ["naive", "ordered", "waiter"], "ordered")
        count = integer(data, "count", 2, 32, 5)
        args = ["./synclab", "phil", "--mode", mode, "--n", str(count)]
        if mode == "naive":
            args += ["--timeout-ms", str(integer(data, "timeout", 1, 60000, 150))]
        else:
            args += ["--meals", str(integer(data, "meals", 1, 100000, 30)), "--eat-us", str(integer(data, "eat_us", 0, 100000, 100))]
        build("synclab")
        return run(args)
    if action == "memory":
        mode = choice(data, "mode", ["first", "best", "worst", "buddy", "slab"], "first")
        build("memlab")
        if mode in ("first", "best", "worst"):
            holes = choice(data, "holes", ["100 500 200 300 600"], "100 500 200 300 600")
            reqs = choice(data, "requests", ["212 417 112 426"], "212 417 112 426")
            return run(["./memlab", "contiguous", "--policy", mode, "--holes", holes, "--requests", reqs])
        if mode == "buddy":
            build("memlab")
            return run(["./memlab", "buddy", "--total", "1024", "--min-block", "32", "--requests", "100 200 60 500"])
        build("memlab")
        return run(["./memlab", "slab", "--page-size", "4096", "--classes", "16 32 64 128 256", "--objects", "12 24 40 100 200 12"])
    if action == "filesystem":
        method = choice(data, "method", ["contiguous", "linked", "indexed"], "indexed")
        build("fslab")
        files = "alpha:3,beta:4"
        args = ["./fslab", "--blocks", "10", "--method", method, "--files", files]
        if data.get("free_alpha"):
            args += ["--free", "alpha"]
        return run(args)
    if action == "paging":
        algorithm = choice(data, "algorithm", ["FIFO", "LRU", "CLOCK", "OPT"], "FIFO")
        frames = integer(data, "frames", 1, 256, 3)
        references = "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"
        build("pagesim")
        return run(["./pagesim", "--algo", algorithm, "--frames", str(frames), "--string", references])
    if action == "disk":
        algorithm = choice(data, "algorithm", ["FIFO", "LIFO", "SSTF", "SCAN", "CSCAN"], "SSTF")
        build("disksim")
        return run(["./disksim", "--algo", algorithm, "--head", "53", "--cylinders", "200", "--string", "98 183 37 122 14 124 65 67"])
    if action == "deadlock":
        operation = choice(data, "operation", ["safe", "request", "detect", "recover"], "safe")
        build("deadsim")
        scenario = "scenarios/banker_textbook.txt" if operation in ("safe", "request") else "scenarios/detect_textbook_deadlock.txt"
        args = ["./deadsim", operation, "--file", scenario]
        if operation == "request":
            args += ["--pid", "1", "--req", "1 0 2"]
        if operation == "recover":
            args += ["--policy", "min-alloc"]
        return run(args)
    if action == "simulator":
        algo = choice(data, "algorithm", ["FCFS", "SJF", "SRTF", "RR", "PRIORITY", "PRIO", "MLFQ"], "RR")
        students = integer(data, "students", 1, 200, 12)
        duration = integer(data, "duration", 1, 2000, 150)
        frames = integer(data, "frames", 1, 256, 24)
        seed = integer(data, "seed", 1, 2147483647, 7)
        build("all")
        output = run(["./exam_os", "--headless", "--strict", "--tick-ms", "10", "--workload", "INTERACTIVE", "--students", str(students), "--duration", str(duration), "--frames", str(frames), "--algo", algo, "--seed", str(seed)])
        summary = ROOT / "output" / "summary.txt"
        if summary.exists():
            text = summary.read_text(encoding="utf-8", errors="replace")
            for label in ("CPU Utilization", "Page Faults", "Page Hits", "Disk Requests", "Total Submissions", "Completed Exams", "Timeouts"):
                match = re.search(rf"{re.escape(label)}\s*:\s*([^║\n]+)", text)
                if match:
                    output["values"][label.lower().replace(" ", "_")] = match.group(1).strip()
        return output
    raise ValueError("Unknown action")


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path not in ("/", "/index.html"):
            self.send_error(404)
            return
        body = (WEB / "index.html").read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/api/run":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length < 1 or length > 8192:
                raise ValueError("Request size is invalid")
            data = json.loads(self.rfile.read(length))
            result = dispatch(data)
            self.respond(200, {"ok": True, **result})
        except (ValueError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
            self.respond(400, {"ok": False, "error": str(exc)})

    def respond(self, status, payload):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        print("[oslab] " + fmt % args)


if __name__ == "__main__":
    print(f"Exam OS Lab Console: http://127.0.0.1:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()

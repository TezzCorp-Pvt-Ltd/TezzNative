"""Independent f32 Conv1D oracles, numerical gradients and error contracts.

Requires a compiled CPU library; --gpu-library requires an actual CUDA device.
No external numeric packages, architecture, optimizer or model are implemented.
"""
import argparse
import ctypes as c
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"


def f32(value):
    return c.c_float(value).value


def close(actual, expected, tolerance=3e-5):
    assert len(actual) == len(expected)
    for index, (a, e) in enumerate(zip(actual, expected)):
        assert math.isfinite(a) and abs(a-e) <= tolerance*(1+abs(e)), (index, a, e)


def oracle(shape, x, w, bias, dy=None):
    """Double precision scalar reference, without importing native code."""
    B, T, I, O, K, S, D = shape
    U, left = (T-1)//S+1, D*(K-1)//2
    y, dx, dw, db = [0.0]*(B*U*O), [0.0]*len(x), [0.0]*len(w), [0.0]*O
    for b in range(B):
        for u in range(U):
            for o in range(O):
                yi = (b*U+u)*O+o
                y[yi] = bias[o] if bias is not None else 0.0
                if dy is not None:
                    db[o] += dy[yi]
                for k in range(K):
                    t = u*S-left+k*D
                    if not 0 <= t < T:
                        continue
                    for i in range(I):
                        xi, wi = (b*T+t)*I+i, (o*K+k)*I+i
                        y[yi] += x[xi]*w[wi]
                        if dy is not None:
                            dx[xi] += dy[yi]*w[wi]
                            dw[wi] += dy[yi]*x[xi]
    return y, dx, dw, db


class Backend:
    def __init__(self, library, gpu, native_abi, out):
        self.gpu, self.keep, self.functions = gpu, [], {}
        self.prefix = "tz_gpu_" if gpu else "tz_cpu_"
        signatures = {self.prefix+"conv1d_f32": 11, self.prefix+"conv1d_bwd_f32": 13}
        if gpu:
            signatures.update(tz_cuda_device_count=0, tz_cuda_set_device=1,
                              tz_cuda_malloc=1, tz_cuda_free=1, tz_cuda_memcpy=4, tz_cuda_sync=0)
        bridge = library
        if native_abi and os.name != "nt":
            source = out/("gpu_adapter.c" if gpu else "cpu_adapter.c")
            lines = ["#include <stdint.h>\n"]
            for name, count in signatures.items():
                params = ",".join(f"int64_t a{i}" for i in range(count)) or "void"
                values = ",".join(f"a{i}" for i in range(count))
                lines += [f"extern __attribute__((ms_abi)) int64_t {name}({params});\n",
                          f"int64_t oracle_{name}({params}){{return {name}({values});}}\n"]
            source.write_text("".join(lines))
            bridge = source.with_suffix(".so")
            subprocess.run(["gcc", "-shared", "-fPIC", str(source), str(library),
                            "-Wl,-rpath,"+str(library.parent), "-o", str(bridge)], check=True)
        self.library = c.CDLL(str(bridge))
        for name, count in signatures.items():
            fn = getattr(self.library, ("oracle_" if native_abi and os.name != "nt" else "")+name)
            fn.argtypes, fn.restype = [c.c_int64]*count, c.c_int64
            self.functions[name] = fn
        if gpu:
            assert self.call("tz_cuda_device_count") > 0, "Actual CUDA device required"
            assert self.call("tz_cuda_set_device", 0) == 0

    def call(self, name, *args):
        return self.functions[name if name.startswith("tz_") else self.prefix+name](*args)

    def alloc(self, values):
        host = (c.c_float*len(values))(*values)
        if not self.gpu:
            self.keep.append(host)
            return c.addressof(host)
        ptr = self.call("tz_cuda_malloc", c.sizeof(host))
        assert ptr != 0
        self.keep.append(ptr)
        assert self.call("tz_cuda_memcpy", ptr, c.addressof(host), c.sizeof(host), 1) == 0
        return ptr

    def read(self, ptr, count):
        if not self.gpu:
            return list((c.c_float*count).from_address(ptr))
        host = (c.c_float*count)()
        assert self.call("tz_cuda_memcpy", c.addressof(host), ptr, c.sizeof(host), 2) == 0
        return list(host)

    def clear(self):
        if self.gpu:
            for ptr in self.keep:
                assert self.call("tz_cuda_free", ptr) == 0
            assert self.call("tz_cuda_sync") == 0
        self.keep.clear()


def values(n, phase=0):
    return [f32(math.sin(i*0.71+phase)*0.4) for i in range(n)]


def numerical_case(backend, shape, finite_difference=False):
    B, T, I, O, K, S, _ = shape
    U = (T-1)//S+1
    x, w, bias, dy = values(B*T*I), values(O*K*I, 1), values(O, 2), values(B*U*O, 3)
    reference = oracle(shape, x, w, bias, dy)
    xp, wp, bp, gp = [backend.alloc(v) for v in (x, w, bias, dy)]
    yp = backend.alloc([float("nan")]*len(dy))  # forward overwrites, never reads output
    assert backend.call("conv1d_f32", xp, wp, bp, yp, *shape) == 0
    close(backend.read(yp, len(dy)), reference[0])
    assert backend.call("conv1d_f32", xp, wp, 0, yp, *shape) == 0
    close(backend.read(yp, len(dy)), oracle(shape, x, w, None)[0])
    gradients = []
    for mask in (7, 1, 2, 4):
        outputs = [backend.alloc([0.125]*len(v)) if mask & (1 << j) else 0
                   for j, v in enumerate(reference[1:])]
        for repeat in (1, 2):
            assert backend.call("conv1d_bwd_f32", gp, xp, wp, *outputs, *shape) == 0
            for ptr, expected in zip(outputs, reference[1:]):
                if ptr:
                    close(backend.read(ptr, len(expected)), [0.125+repeat*v for v in expected])
        if mask == 7:
            gradients = [[(v-0.125)/2 for v in backend.read(p, len(r))]
                         for p, r in zip(outputs, reference[1:])]
    for p, expected in ((xp, x), (wp, w), (bp, bias), (gp, dy)):
        close(backend.read(p, len(expected)), expected, 0)
    if finite_difference:
        # Perturb actual f32 values and differentiate the native forward objective.
        for kind, original in enumerate((x, w, bias)):
            for index in range(len(original)):
                objectives, points = [], []
                for direction in (-1, 1):
                    changed = list(original)
                    changed[index] = f32(original[index]+direction*0.002)
                    points.append(changed[index])
                    pointers = [xp, wp, bp]
                    pointers[kind] = backend.alloc(changed)
                    assert backend.call("conv1d_f32", *pointers, yp, *shape) == 0
                    objectives.append(sum(a*g for a, g in zip(backend.read(yp, len(dy)), dy)))
                numeric = (objectives[1]-objectives[0])/(points[1]-points[0])
                close([gradients[kind][index]], [numeric], 2e-4)


def invalid_case(b):
    shape = (1, 3, 2, 2, 3, 1, 1)
    x, w, bias, out, dy, dx, dw, db = [b.alloc([0.5]*n) for n in (6, 12, 2, 6, 6, 6, 12, 2)]
    original = [(p, n, b.read(p, n)) for p, n in ((x, 6), (w, 12), (bias, 2), (out, 6), (dx, 6), (dw, 12), (db, 2))]
    def unchanged():
        for ptr, n, before in original:
            close(b.read(ptr, n), before, 0)
    for index in range(7):
        for value in (0, -1, 1 << 40):
            dims = list(shape)
            dims[index] = value
            assert b.call("conv1d_f32", x, w, bias, out, *dims) == -1
            assert b.call("conv1d_bwd_f32", dy, x, w, dx, dw, db, *dims) == -1
    for dims in ((1, 3, 2, 2, 1 << 30, 1, 4), (1 << 30, 3, 2, 2, 3, 1, 1)):
        assert b.call("conv1d_f32", x, w, bias, out, *dims) == -1
    for index in (0, 1, 3):
        for value in (0, (x, w, bias, out)[index]+1):
            pointers = [x, w, bias, out]
            pointers[index] = value
            assert b.call("conv1d_f32", *pointers, *shape) == -1
    for pointers in ((x, w, bias+1, out), (x, w, bias, x), (x, w, bias, x+4), (x, w, bias, w)):
        assert b.call("conv1d_f32", *pointers, *shape) == -1
    for outputs in ((0, 0, 0), (x, dw, db), (dx, w, db), (dx, dw, dy),
                    (dx, dx, db), (dx, dw, dx+4), (dx+1, dw, db)):
        assert b.call("conv1d_bwd_f32", dy, x, w, *outputs, *shape) == -1
    for index in range(3):
        pointers = [dy, x, w]
        pointers[index] = 0
        assert b.call("conv1d_bwd_f32", *pointers, dx, dw, db, *shape) == -1
    if b.gpu:
        short = b.alloc([1.0])
        assert b.call("conv1d_f32", short, w, bias, out, *shape) == -1
        assert b.call("conv1d_f32", x+4, w, bias, out, *shape) == -1
        assert b.call("conv1d_bwd_f32", dy, x, w, dx, short, db, *shape) == -1
    unchanged()
    for invalid in (float("nan"), float("inf"), -float("inf")):
        for index, n in enumerate((6, 12, 2)):
            bad = b.alloc([invalid]+[0.5]*(n-1))
            pointers = [x, w, bias]
            pointers[index] = bad
            assert b.call("conv1d_f32", *pointers, out, *shape) == -1
        for index, n in enumerate((6, 6, 12, 6, 12, 2)):
            bad = b.alloc([invalid]+[0.5]*(n-1))
            pointers = [dy, x, w, dx, dw, db]
            pointers[index] = bad
            assert b.call("conv1d_bwd_f32", *pointers, *shape) == -1
        unchanged()


def overflow_case(b):
    big, out, grad = b.alloc([1e38]), b.alloc([0]), b.alloc([0])
    shape = (1, 1, 1, 1, 1, 1, 1)
    assert b.call("conv1d_f32", big, big, 0, out, *shape) == -1
    assert b.call("conv1d_bwd_f32", big, big, big, grad, 0, 0, *shape) == -1


def consistency(cpu, gpu):
    shape = (2, 17, 3, 5, 4, 2, 3)
    B, T, I, O, K, S, _ = shape
    inputs = (values(B*T*I), values(O*K*I, 1), values(O, 2), values(B*((T-1)//S+1)*O, 3))
    observed = []
    for b in (cpu, gpu):
        x, w, bias, dy = [b.alloc(v) for v in inputs]
        out = b.alloc([0]*len(inputs[3]))
        grads = [b.alloc([0]*len(v)) for v in inputs[:3]]
        assert b.call("conv1d_f32", x, w, bias, out, *shape) == 0
        assert b.call("conv1d_bwd_f32", dy, x, w, *grads, *shape) == 0
        observed.append([b.read(out, len(inputs[3]))]+[b.read(p, len(v)) for p, v in zip(grads, inputs[:3])])
    for a, e in zip(*observed):
        close(a, e)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu-library", type=Path, required=True)
    parser.add_argument("--gpu-library", type=Path)
    parser.add_argument("--native-abi", action="store_true")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"native_abi": args.native_abi, "real_cuda_required": bool(args.gpu_library), "results": [], "libraries": {}}
    backends = []
    def case(name, work):
        try:
            work()
            report["results"].append({"name": name, "status": "pass"})
        except Exception as error:
            report["results"].append({"name": name, "status": "fail", "error": repr(error)})
        finally:
            for backend in backends:
                backend.clear()
    try:
        for kind, library in (("cpu", args.cpu_library), ("gpu", args.gpu_library)):
            if library is None:
                continue
            library = library.resolve()
            report["libraries"][kind] = {"path": str(library), "sha256": hashlib.sha256(library.read_bytes()).hexdigest()}
            backend = Backend(library, kind == "gpu", args.native_abi, out)
            backends.append(backend)
            shapes = [(2, 5, 2, 3, 3, 1, 1), (1, 5, 2, 2, 4, 2, 2),
                      (1, 1, 2, 2, 7, 1, 3), (2, 3, 1, 2, 3, 7, 1),
                      (1, 9, 3, 5, 1, 2, 17), (2, 27, 3, 7, 5, 1, 2)]
            for index, shape in enumerate(shapes):
                case(f"{kind}/forward_backward_{shape}", lambda s=shape, i=index: numerical_case(backend, s, i < 2))
            case(kind+"/invalid_arguments_no_writes", lambda: invalid_case(backend))
            case(kind+"/arithmetic_overflow", lambda: overflow_case(backend))
        if len(backends) == 2:
            case("cpu_cuda_consistency", lambda: consistency(*backends))
    except Exception as error:
        report["results"].append({"name": "setup_or_cleanup", "status": "fail", "error": repr(error)})
    report["summary"] = {status: sum(r["status"] == status for r in report["results"]) for status in ("pass", "fail")}
    (out/"results.json").write_text(json.dumps(report, indent=2)+"\n")
    print("TEMPORAL_CONV1D", report["summary"])
    for result in report["results"]:
        if result["status"] == "fail":
            print(result)
    return bool(report["summary"]["fail"])


if __name__ == "__main__":
    raise SystemExit(main())

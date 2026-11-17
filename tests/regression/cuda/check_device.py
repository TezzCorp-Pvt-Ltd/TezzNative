"""Build the complete CUDA library and execute independent real-device oracles.

No host extraction, CUDA qualifier shim, CPU fallback, or automatic skip is
used. Missing toolkit/device, compilation errors and numerical failures fail.
"""
import argparse
import ctypes as c
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / "TezzCorp_WebSites/tn_tezzcorp_com/download/sdk"
SOURCE = SDK / "lib/tzgpu_kernels.cu"


def signatures():
    text = SOURCE.read_text(encoding="utf-8")
    return {name: ([] if args.strip() in ("", "void") else
                   [c.c_double if part.strip().startswith("double") else c.c_int64
                    for part in args.split(",")])
            for name, args in re.findall(r"TZ_EXPORT int64_t (tz_\w+)\((.*?)\)\s*\{", text, re.S)}


def gelu(x):
    return 0.5*x*(1+math.tanh(math.sqrt(2/math.pi)*(x+0.044715*x*x*x)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cuda-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT/"build/verification/cuda-device")
    parser.add_argument("--library", type=Path)
    parser.add_argument("--native-abi", action="store_true")
    parser.add_argument("--arch", default="sm_75", help="Explicit SM build target (default: tested GTX1650 sm_75)")
    args = parser.parse_args()
    out, cuda = args.out.resolve(), args.cuda_root.resolve()
    out.mkdir(parents=True, exist_ok=True)
    library = args.library.resolve() if args.library else out/("tzgpu.dll" if os.name == "nt" else "libtzgpu.so")
    records, called = [], set()
    report = {"full_cuda_build": not bool(args.library), "real_device_required": True,
              "provided_library": bool(args.library), "architecture": args.arch,
              "native_x64_abi": args.native_abi, "source_sha256": {}, "results": records}
    for path in [SOURCE, *sorted((SOURCE.parent/"tzgpu").glob("*.cuh"))]:
        report["source_sha256"][str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    env = dict(os.environ)
    env["PATH"] = str(cuda/"bin") + os.pathsep + env.get("PATH", "")
    libraries = cuda/("lib64" if (cuda/"lib64").exists() else "lib")
    env["LD_LIBRARY_PATH"] = str(libraries) + ":/usr/lib/wsl/lib:" + env.get("LD_LIBRARY_PATH", "")
    if not re.fullmatch(r"sm_[0-9]+[a-z]?", args.arch):
        parser.error("--arch must specify a CUDA SM architecture")
    if not args.library:
        nvcc = cuda/"bin"/("nvcc.exe" if os.name == "nt" else "nvcc")
        command = [str(nvcc), "-std=c++14", "-O2", "-arch="+args.arch, "-shared", "--cudart=shared",
                   str(SOURCE), "-o", str(library)]
        if os.name != "nt":
            command += ["-Xcompiler", "-fPIC", "-L"+str(libraries), "-Xlinker", "-rpath,"+str(libraries)]
        if args.native_abi:
            command += ["-DTN_NATIVE_X64_ABI=1"]
        built = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
        report["build"] = {"command": command, "exit": built.returncode, "stdout": built.stdout, "stderr": built.stderr}
        if built.returncode:
            report["summary"] = {"pass": 0, "fail": 1}
            (out/"results.json").write_text(json.dumps(report, indent=2)+"\n")
            print(built.stdout+built.stderr)
            return 1
    report["library_sha256"] = hashlib.sha256(library.read_bytes()).hexdigest()
    types = signatures()
    dll_directories = []
    if os.name == "nt":
        runtime_paths = sorted((cuda/"bin").glob("**/cudart64*.dll"))
        if not runtime_paths:
            raise RuntimeError("CUDA shared runtime DLL is missing")
        for directory in {library.parent, runtime_paths[-1].parent}:
            dll_directories.append(os.add_dll_directory(str(directory)))
    if args.native_abi and os.name != "nt":
        # ctypes uses the host ABI. This adapter calls every actual exported
        # function using the Win64 ABI required by generated Tezz x64 programs.
        adapter = out/"abi_adapter.c"
        body = ["#include <stdint.h>\n"]
        for name, parameters in types.items():
            decl = ",".join(("double" if kind == c.c_double else "int64_t")+f" a{i}" for i,kind in enumerate(parameters)) or "void"
            vals = ",".join(f"a{i}" for i in range(len(parameters)))
            body.append(f"extern __attribute__((ms_abi)) int64_t {name}({decl});\n")
            body.append(f"int64_t oracle_{name}({decl}){{return {name}({vals});}}\n")
        adapter.write_text("".join(body))
        bridge = out/"abi_adapter.so"
        compiled = subprocess.run(["gcc","-shared","-fPIC",str(adapter),str(library),"-Wl,-rpath,"+str(library.parent),"-o",str(bridge)],
                                  capture_output=True,text=True,env=env,timeout=60)
        if compiled.returncode:
            raise RuntimeError(compiled.stdout+compiled.stderr)
        loaded = c.CDLL(str(bridge))
    else:
        loaded = c.CDLL(str(library))
    runtime = c.CDLL(str(libraries/"libcudart.so" if os.name != "nt" else runtime_paths[-1]))
    functions = {}
    for name, parameters in types.items():
        fn = getattr(loaded, ("oracle_" if args.native_abi and os.name != "nt" else "")+name)
        fn.argtypes, fn.restype = parameters, c.c_int64
        functions[name] = fn

    def call(name, *values):
        if not name.startswith("tz_"):
            name = "tz_gpu_"+name
        called.add(name)
        return functions[name](*values)

    def checked(name, *values):
        status = call(name, *values)
        assert status == 0, (name, status)
        return status

    def sync():
        checked("tz_cuda_sync")
        if runtime:
            assert runtime.cudaDeviceSynchronize() == 0, "CUDA asynchronous execution failed"

    allocated = []
    def upload(values, kind=c.c_float):
        host = (kind*len(values))(*values)
        pointer = call("tz_cuda_malloc", c.sizeof(host))
        assert pointer, "CUDA allocation failed"
        allocated.append(pointer)
        checked("tz_cuda_memcpy", pointer, c.addressof(host), c.sizeof(host), 1)
        return pointer

    def download(pointer, count, kind=c.c_float):
        sync()
        host = (kind*count)()
        checked("tz_cuda_memcpy", c.addressof(host), pointer, c.sizeof(host), 2)
        return list(host)

    def close(actual, expected, tol=2e-5):
        assert len(actual) == len(expected)
        for i,(got,wanted) in enumerate(zip(actual,expected)):
            assert math.isclose(got,wanted,rel_tol=tol,abs_tol=tol), (i,got,wanted)

    def case(name, fn):
        record = {"case": name, "status": "fail"}
        try:
            fn()
            record["status"] = "pass"
        except (AssertionError, RuntimeError, ValueError) as error:
            record["error"] = str(error)
        finally:
            for pointer in allocated:
                checked("tz_cuda_free", pointer)
            allocated.clear()
        records.append(record)
        print(name,record["status"],record.get("error",""),flush=True)

    def memory():
        assert call("tz_cuda_backend_kind") == 1
        assert call("tz_cuda_device_count") > 0
        checked("tz_cuda_set_device",0)
        assert call("tz_cuda_mem_total") >= call("tz_cuda_mem_free") > 0
        assert call("tz_gpu_info") > 0
        a=upload([1,2,3]);b=upload([0,0,0])
        checked("tz_cuda_memcpy",b,a,12,3)
        close(download(b,3),[1,2,3])
    case("device_memory",memory)

    for suffix,kind in [("_f32",c.c_float),("",c.c_double)]:
        for operation in ["add","mul"] + (["sub"] if suffix else []):
            def binary(operation=operation,suffix=suffix,kind=kind):
                xs=[(i%19-9)/5 for i in range(513)];ys=[(i%13-6)/7 for i in range(513)]
                a,b,d=upload(xs,kind),upload(ys,kind),upload([0]*513,kind)
                checked(operation+suffix,a,b,d,513)
                expected=[x+y if operation=='add' else x-y if operation=='sub' else x*y for x,y in zip(xs,ys)]
                close(download(d,513,kind),expected,2e-5 if suffix else 1e-12)
            case(operation+suffix,binary)
        for operation in ["relu","sigmoid","gelu","scale"] + (["tanh","leaky_relu"] if suffix else []):
            def unary(operation=operation,suffix=suffix,kind=kind):
                xs=[-5,-2,-0.5,0,0.5,2,5];a,d=upload(xs,kind),upload([0]*7,kind)
                extra=[0.2] if operation in ('scale','leaky_relu') else []
                checked(operation+suffix,a,d,7,*extra)
                expected=[max(0,x) if operation=='relu' else 1/(1+math.exp(-x)) if operation=='sigmoid' else gelu(x) if operation=='gelu' else x*0.2 if operation=='scale' else math.tanh(x) if operation=='tanh' else max(x,x*0.2) for x in xs]
                close(download(d,7,kind),expected,2e-5 if suffix else 1e-12)
            case(operation+suffix,unary)
        def softmax(suffix=suffix,kind=kind):
            xs=[10000,10001,9998,-10000,-10001,-9999];a=upload(xs,kind)
            checked('softmax'+suffix,a,2,3)
            expected=[]
            for row in [xs[:3],xs[3:]]:
                ex=[math.exp(x-max(row)) for x in row];expected += [x/sum(ex) for x in ex]
            close(download(a,6,kind),expected,2e-5 if suffix else 1e-12)
        case('softmax'+suffix,softmax)

    def edits():
        a=upload([-2,1,3,5]);b=upload([0]*4)
        checked('copy_f32',a,b,4);close(download(b,4),[-2,1,3,5])
        checked('axpy_f32',b,a,0.5,4);close(download(b,4),[-3,1.5,4.5,7.5])
        checked('clamp_f32',b,4,-1,2);close(download(b,4),[-1,1.5,2,2])
        checked('fill_f32',b,4,7.5);close(download(b,4),[7.5]*4)
        bias=upload([1,-2]);checked('add_bias_f32',b,bias,2,2);close(download(b,4),[8.5,5.5,8.5,5.5])
    case('editing',edits)

    for operation in ['relu','sigmoid','tanh','gelu']:
        def backward(operation=operation):
            xs=[-3,-1,-0.2,0.2,1,3];grad=[0.1,-0.3,1,2,-0.2,0.7]
            values=[1/(1+math.exp(-x)) for x in xs] if operation=='sigmoid' else [math.tanh(x) for x in xs] if operation=='tanh' else xs
            a,g,d=upload(values),upload(grad),upload([0]*6)
            checked(operation+'_bwd_f32',g,a,d,6)
            deriv=[(1 if x>0 else 0) if operation=='relu' else s*(1-s) if operation=='sigmoid' else 1-s*s if operation=='tanh' else (gelu(x+1e-5)-gelu(x-1e-5))/2e-5 for x,s in zip(xs,values)]
            close(download(d,6),[g*v for g,v in zip(grad,deriv)])
        case(operation+'_backward',backward)

    def norm():
        xs=[1,2,5,-1,-2,7];g=[0.5,2,-1];b=[1,-0.1,0.2];a=upload(xs);gamma=upload(g);beta=upload(b)
        checked('layernorm_f32',a,gamma,beta,2,3,1e-5)
        expected=[]
        for row in [xs[:3],xs[3:]]:
            mu=sum(row)/3;var=sum((x-mu)**2 for x in row)/3
            expected += [(x-mu)/math.sqrt(var+1e-5)*gg+bb for x,gg,bb in zip(row,g,b)]
        close(download(a,6),expected)
        a64=upload(xs,c.c_double);checked('layer_norm',a64,2,3,1e-5)
        close(download(a64,6,c.c_double),[(expected[i]-b[i%3])/g[i%3] for i in range(6)])
    case('normalization',norm)

    def norm_backward():
        xs=[1,2,5,-1,-2,7];gs=[0.4,-0.2,0.7,1,-2,0.1];gamma=[0.5,2,-1]
        a,g,w,d=upload(xs),upload(gs),upload(gamma),upload([0]*6)
        checked('layernorm_bwd_f32',d,g,a,w,2,3,1e-5)
        def value(vals):
            s=0
            for start in [0,3]:
                row=vals[start:start+3];mu=sum(row)/3;var=sum((x-mu)**2 for x in row)/3
                s+=sum((x-mu)/math.sqrt(var+1e-5)*gamma[j]*gs[start+j] for j,x in enumerate(row))
            return s
        expected=[]
        for i in range(6):
            hi=xs.copy();lo=xs.copy();hi[i]+=1e-4;lo[i]-=1e-4
            expected.append((value(hi)-value(lo))/2e-4)
        close(download(d,6),expected)
    case('layernorm_gradient',norm_backward)

    def batchnorm():
        xs=[1,2,3,4,5,6,7,8];a=upload(xs)
        mean,var,g,b=upload([2,6]),upload([1,4]),upload([2,3]),upload([-1,0.5])
        checked('batchnorm_f32',a,mean,var,g,b,2,2,2,1e-5)
        close(download(a,8),[((v-[2,6][i//2%2])/math.sqrt([1,4][i//2%2]+1e-5)*[2,3][i//2%2]+[-1,0.5][i//2%2]) for i,v in enumerate(xs)])
    case('batchnorm',batchnorm)

    def random():
        count=8192;a=upload([1]*count);mask=upload([0]*count)
        checked('dropout_f32',a,mask,count,0.25,123)
        values=download(a,count);close(values,download(mask,count))
        assert all(v==0 or math.isclose(v,4/3,rel_tol=1e-6) for v in values)
        assert 0.21 < values.count(0)/count < 0.29
        checked('fill_f32',a,count,1);checked('dropout_f32',a,mask,count,0.25,123)
        close(download(a,count),values)
        checked('xavier_init_f32',a,count,8,123)
        values=download(a,count);mean=sum(values)/count;variance=sum((x-mean)**2 for x in values)/count
        assert abs(mean)<0.025 and abs(variance-0.25)<0.02,(mean,variance)
    case('random_kernels',random)

    def embedding():
        idx=upload([2,0,2],c.c_int32);table=upload([1,2,3,4,5,6]);outp=upload([0]*6)
        checked('embedding_f32',idx,table,outp,3,2);close(download(outp,6),[5,6,1,2,5,6])
        grads=upload([1,2,3,4,5,6]);target=upload([0]*6)
        checked('embedding_bwd_f32',idx,grads,target,3,2);close(download(target,6),[3,4,0,0,6,8])
    case('embedding',embedding)

    def transformer():
        xs=[i/4 for i in range(24)];x=upload(xs);checked('rope_f32',x,3,2,4)
        expected=xs.copy()
        for pos in range(3):
            for head in range(2):
                for j in range(2):
                    theta=pos/10000**(2*j/4);i=(pos*2+head)*4+j
                    expected[i]=xs[i]*math.cos(theta)-xs[i+2]*math.sin(theta)
                    expected[i+2]=xs[i]*math.sin(theta)+xs[i+2]*math.cos(theta)
        close(download(x,24),expected)
        scores=upload([0]*18);checked('causal_mask_f32',scores,2,3);values=download(scores,18)
        for i,value in enumerate(values):
            assert value < -1e8 if i%3>(i//3)%3 else value==0
    case('transformer',transformer)

    def losses():
        xs=[10000,9999,9998,1,2,4];labels=[2,1];x=upload(xs);lab=upload(labels,c.c_int32);loss=upload([0]*2);grad=upload([0]*6)
        checked('cross_entropy_f32',x,lab,loss,2,3);checked('cross_entropy_bwd_f32',x,lab,grad,2,3)
        expected=[];deriv=[]
        for row,label in zip([xs[:3],xs[3:]],labels):
            ex=[math.exp(v-max(row)) for v in row];s=sum(ex)
            expected.append(max(row)-row[label]+math.log(s));deriv += [(v/s-(j==label))/2 for j,v in enumerate(ex)]
        close(download(loss,2),expected);close(download(grad,6),deriv)
        a=upload([1,3,-2]);b=upload([2,1,-1]);l=upload([0]*3);g=upload([0]*3)
        checked('mse_loss_f32',a,b,l,3);checked('mse_bwd_f32',a,b,g,3)
        close(download(l,3),[1,4,1]);close(download(g,3),[-2/3,4/3,-2/3])
    case('losses',losses)

    def optimizers():
        p=upload([1,-2]);g=upload([0.3,-0.4]);m=upload([0,0]);v=upload([0,0])
        checked('adam_f32',p,g,m,v,0.01,0.9,0.999,1e-8,0.9,0.999,2)
        close(download(p,2),[0.99,-1.99]);close(download(m,2),[0.03,-0.04]);close(download(v,2),[0.00009,0.00016])
        p2=upload([1,-2]);velocity=upload([0.2,-0.1])
        checked('sgd_f32',p2,g,velocity,0.1,0.9,0.2,2)
        close(download(velocity,2),[0.68,-0.89]);close(download(p2,2),[0.932,-1.911])
    case('optimizers',optimizers)

    def reductions():
        xs=[(i%11-5)/4 for i in range(517)];x=upload(xs);outp=upload([0])
        checked('reduce_sum_sq_f32',x,outp,len(xs));close(download(outp,1),[sum(v*v for v in xs)],1e-4)
        checked('grad_clip_f32',x,len(xs),2,4);close(download(x,len(xs)),[v/2 for v in xs])
        a=upload([1,2,3,4,5,6]);d=upload([10,20,30]);checked('reduce_sum_cols_f32',a,d,2,3);close(download(d,3),[15,27,39])
    case('reductions',reductions)

    def conversions():
        xs=[0,-0.0,1/3,-1e-20,1e20];x=upload(xs,c.c_double);f=upload([0]*5);d=upload([0]*5,c.c_double)
        checked('f64_to_f32',x,f,5);checked('f32_to_f64',f,d,5)
        expected=[c.c_float(v).value for v in xs];close(download(d,5,c.c_double),expected,0)
        assert math.copysign(1,download(d,5,c.c_double)[1]) == -1
    case('conversions',conversions)

    for name in ['gemm_f32','gemm_nt_f32','gemm_tn_f32','gemm_f64','matmul_tiled']:
        def gemm(name=name):
            m,k,n=17,19,21;av=[(i%13-6)/7 for i in range(m*k)];bv=[(i%11-5)/6 for i in range(k*n)]
            expected=[sum(av[r*k+t]*bv[t*n+col] for t in range(k)) for r in range(m) for col in range(n)]
            aa=[av[r*k+t] for t in range(k) for r in range(m)] if name=='gemm_tn_f32' else av
            bb=[bv[t*n+col] for col in range(n) for t in range(k)] if name=='gemm_nt_f32' else bv
            kind=c.c_double if name in ('gemm_f64','matmul_tiled') else c.c_float
            a,b,d=upload(aa,kind),upload(bb,kind),upload([1]*(m*n),kind)
            extra=[] if name=='matmul_tiled' else [0.75,0.25]
            checked(name,a,b,d,m,k,n,*extra)
            close(download(d,m*n,kind),expected if not extra else [0.75*v+0.25 for v in expected],
                  1e-12 if kind==c.c_double else 2e-5)
        case(name,gemm)

    def invalid_dimensions():
        a=upload([1,2]);d=upload([17,19])
        assert call('relu_f32',a,d,0)<0, 'empty launch falsely reports success'
        assert call('relu_f32',a,d,2147483648)<0, '64-bit count truncated'
        assert call('softmax_f32',a,1,0)<0
        assert call('gemm_f32',a,a,d,0,1,1,1,0)<0
        close(download(d,2),[17,19])
    case('invalid_dimensions',invalid_dimensions)
    def invalid_memory():
        a=upload([1,2]);d=upload([17,19])
        for pointer,count in [(a,3),(a+4,2),(a+1,1),(0,1),(1234,1)]:
            assert call('relu_f32',pointer,d,count)<0, (pointer,count,'invalid allocation accepted')
        assert call('tz_cuda_malloc',0)==0
        assert call('tz_cuda_malloc',-1)==0
        assert call('tz_cuda_memcpy',d,a,12,3)<0
        assert call('tz_cuda_memcpy',d,a,-1,3)<0
        assert call('tz_cuda_memcpy',d,a,4,7)<0
        assert call('tz_cuda_set_device',2147483648)<0
        checked('relu_f32',a,d,2)
        close(download(d,2),[1,2])
    case('invalid_memory',invalid_memory)

    def invalid_parameters():
        a=upload([1,2,3,4]);d=upload([17]*4);idx=upload([-1,0],c.c_int32)
        assert call('dropout_f32',a,d,4,1,0)<0
        assert call('dropout_f32',a,d,4,-0.1,0)<0
        assert call('dropout_f32',a,a,4,0.5,0)<0
        assert call('layernorm_f32',a,0,0,1,4,0)<0
        assert call('rope_f32',a,1,1,3)<0
        assert call('clamp_f32',a,4,2,1)<0
        assert call('xavier_init_f32',a,4,0,0)<0
        assert call('cross_entropy_f32',a,idx,d,2,2)<0
        assert call('cross_entropy_bwd_f32',a,idx,d,2,2)<0
        assert call('embedding_f32',idx,a,d,2,2)<0
        assert call('embedding_bwd_f32',idx,a,d,2,2)<0
        idx2=upload([2,0],c.c_int32)
        assert call('embedding_f32',idx2,a,d,2,2)<0
        assert call('cross_entropy_f32',a,idx2,d,2,2)<0
        assert call('gemm_f32',a,a,a,2,2,2,1,0)<0
        assert call('f32_to_f64',a,a,2)<0
        close(download(d,4),[17]*4)
        close(download(a,4),[1,2,3,4])
    case('invalid_parameters',invalid_parameters)

    def seeded_oracle():
        def uniform(seed,index):
            mask=(1<<64)-1
            z=(seed+0x9e3779b97f4a7c15*(index+1))&mask
            z=((z^(z>>30))*0xbf58476d1ce4e5b9)&mask
            z=((z^(z>>27))*0x94d049bb133111eb)&mask
            return (((z^(z>>31))>>40)+1)/(1<<24)
        a=upload([1]*513);mask=upload([0]*513)
        for seed in [0,123,-1]:
            checked('fill_f32',a,513,1)
            checked('dropout_f32',a,mask,513,0.25,seed)
            close(download(a,513),[4/3 if uniform(seed,i)>0.25 else 0 for i in range(513)])
            checked('xavier_init_f32',a,513,8,seed)
            close(download(a,513),[math.sqrt(-2*math.log(uniform(seed,2*i)))*math.cos(2*math.pi*uniform(seed,2*i+1))*0.5 for i in range(513)])
    case('seeded_cpu_oracle',seeded_oracle)

    def gemm_beta_zero():
        for name,kind in [('gemm_f32',c.c_float),('gemm_f64',c.c_double)]:
            a=upload([2],kind);b=upload([3],kind);d=upload([float('nan')],kind)
            checked(name,a,b,d,1,1,1,1,0)
            close(download(d,1,kind),[6],0)
    case('gemm_zero_beta_nan_output',gemm_beta_zero)

    def optional_normalization():
        for g,b in [(None,None),([2,3],None),(None,[1,2])]:
            a=upload([1,3]);gamma=upload(g) if g else 0;beta=upload(b) if b else 0
            checked('layernorm_f32',a,gamma,beta,1,2,1e-5)
            close(download(a,2),[v/math.sqrt(1+1e-5)*(g[i] if g else 1)+(b[i] if b else 0) for i,v in enumerate([-1,1])])
    case('optional_normalization',optional_normalization)
    def training_convergence():
        # Fit y=2*x+1 using only GPU forward/backward/reduction/SGD kernels.
        x=upload([-2,-1,0,1,2]);target=upload([-3,-1,1,3,5])
        w=upload([0]);b=upload([0]);vw=upload([0]);vb=upload([0])
        pred=upload([0]*5);grad=upload([0]*5);dw=upload([0]);db=upload([0]);loss=upload([0]*5)
        for step in range(200):
            checked('gemm_f32',x,w,pred,5,1,1,1,0)
            checked('add_bias_f32',pred,b,5,1)
            checked('mse_bwd_f32',pred,target,grad,5)
            checked('gemm_tn_f32',x,grad,dw,1,5,1,1,0)
            checked('fill_f32',db,1,0)
            checked('reduce_sum_cols_f32',grad,db,5,1)
            checked('sgd_f32',w,dw,vw,0.03,0.8,0,1)
            checked('sgd_f32',b,db,vb,0.03,0.8,0,1)
        checked('gemm_f32',x,w,pred,5,1,1,1,0)
        checked('add_bias_f32',pred,b,5,1)
        checked('mse_loss_f32',pred,target,loss,5)
        close(download(w,1),[2],1e-5);close(download(b,1),[1],1e-5)
        assert sum(download(loss,5))/5<1e-9
    case('gpu_training_convergence',training_convergence)
    def temporal_convolution():
        # The full finite-difference/boundary matrix lives in temporal/check_conv1d.py.
        x=upload([1,2,3]);w=upload([2,3,5]);bias=upload([1]);out=upload([0]*3)
        dy=upload([1,2,3]);dx=upload([0]*3);dw=upload([0]*3);db=upload([0])
        checked('conv1d_f32',x,w,bias,out,1,3,1,1,3,1,1)
        close(download(out,3),[14,24,14])
        checked('conv1d_bwd_f32',dy,x,w,dx,dw,db,1,3,1,1,3,1,1)
        close(download(dx,3),[7,17,19]);close(download(dw,3),[8,14,8]);close(download(db,1),[6])
        assert call('conv1d_f32',x,w,bias,x,1,3,1,1,3,1,1)<0
        assert call('conv1d_bwd_f32',dy,x,w,dx,dw,db,1,3,1,1,3,0,1)<0
    case('temporal_convolution',temporal_convolution)
    report['covered_exports']=sorted(called)
    report['uncovered_exports']=sorted(set(types)-called)
    report['summary']={status:sum(r['status']==status for r in records) for status in ('pass','fail')}
    if report['uncovered_exports']:
        report['summary']['fail']+=1
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('CUDA_DEVICE_SUMMARY',report['summary'],'uncovered',report['uncovered_exports'])
    return bool(report['summary']['fail'])


if __name__ == '__main__':
    raise SystemExit(main())

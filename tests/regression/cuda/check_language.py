"""Run the real CUDA library through both Tezz bytecode and native execution."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tezzc', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True,
                        help='Complete CUDA library built with TN_NATIVE_X64_ABI=1')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fixture', choices=['language_device','nn_training_device','nn_errors_device','conv1d_gpu','stt_cuda'], default='language_device')
    args = parser.parse_args()
    compiler, library, out = args.tezzc.resolve(), args.library.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    isolated = out/compiler.name
    shutil.copy2(compiler, isolated)
    for name in (['tnrt.dll', 'tzcpu.dll'] if os.name == 'nt' else
                 ['libtnrt-native.so', 'libtzcpu-native.so']):
        shutil.copy2(compiler.parent/name, out/name)
    shutil.copy2(library, out/('tzgpu.dll' if os.name == 'nt' else 'libtzgpu-native.so'))
    folder = 'tests/regression/temporal/support/' if args.fixture in ('conv1d_gpu', 'stt_cuda') else 'tests/regression/cuda/support/'
    fixture = ROOT/(folder+args.fixture+'.tn')
    native = out/('device.exe' if os.name == 'nt' else 'device')
    commands = [[str(isolated),'check',str(fixture)],
                [str(isolated),'run',str(fixture),'--bc'],
                [str(isolated),'buildexe',str(fixture),str(native),'--verify'],
                [str(native)]]
    results=[]
    for command in commands:
        try:
            result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=90)
        except subprocess.TimeoutExpired:
            results.append({'command':command,'timeout':True})
            (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
            print('FAIL: timeout',command,flush=True)
            return 1
        results.append({'command':command,'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(result.returncode, ' '.join(command), result.stdout, result.stderr, flush=True)
        if result.returncode or 'FAIL:' in result.stdout or 'FAIL:' in result.stderr:
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

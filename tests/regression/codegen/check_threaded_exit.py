"""Linux x64 native programs must terminate threads on return and sys_exit.

The preload helper deliberately leaves a pthread alive. No GPU is required.
A per-process exit syscall hangs these programs; exit_group terminates them.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT=Path(__file__).resolve().parents[3]
SUPPORT=Path(__file__).resolve().parent/'support'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tezzc',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if platform.system()!='Linux' or platform.machine()!='x86_64':
        parser.error('this strict regression requires Linux x64')
    compiler,out=args.tezzc.resolve(),args.out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    helper=out/'threaded_preload.so'
    subprocess.run(['cc','-shared','-fPIC','-pthread',str(SUPPORT/'threaded_preload.c'),'-o',str(helper)],check=True)
    records=[]
    for fixture,wanted in [('threaded_return',37),('threaded_exit',42)]:
        native=out/fixture
        built=subprocess.run([str(compiler),'buildexe',str(SUPPORT/(fixture+'.tn')),str(native),'--verify'],
                             cwd=ROOT,capture_output=True,text=True,timeout=60)
        record={'case':fixture,'pass':False,'build_exit':built.returncode,'build_output':built.stdout+built.stderr}
        if built.returncode==0:
            env=dict(os.environ,LD_PRELOAD=str(helper))
            try:
                run=subprocess.run([str(native)],env=env,capture_output=True,text=True,timeout=5)
                record.update(exit=run.returncode,stdout=run.stdout,stderr=run.stderr)
                record['pass']=run.returncode==wanted and run.stdout=='WORKER_STARTED\n'
            except subprocess.TimeoutExpired:
                record['timeout']=True
        records.append(record)
        (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
        print(record,flush=True)
    return int(not all(record['pass'] for record in records))


if __name__=='__main__':
    raise SystemExit(main())

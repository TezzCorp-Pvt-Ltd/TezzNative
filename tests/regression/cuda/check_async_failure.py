"""Verify the production CUDA library reports asynchronous device failures.

Runs an intentional device trap in this process only; never run this command
inside the normal numerical or compute-sanitizer suite.
"""
import argparse
import ctypes as c
import json
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cuda-root',type=Path,required=True)
    parser.add_argument('--library',type=Path,required=True,help='Host ABI library from check_device.py')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--arch',default='sm_75')
    args=parser.parse_args()
    cuda,out=args.cuda_root.resolve(),args.out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    probe=out/'async_failure.so'
    libraries=cuda/('lib64' if (cuda/'lib64').exists() else 'lib')
    subprocess.run([str(cuda/'bin/nvcc'),'-shared','-Xcompiler','-fPIC','--cudart=shared',
                    '-arch='+args.arch,str(Path(__file__).parent/'support/async_failure.cu'),
                    '-L'+str(libraries),'-Xlinker','-rpath,'+str(libraries),'-o',str(probe)],check=True)
    backend=c.CDLL(str(args.library.resolve()))
    fault=c.CDLL(str(probe))
    for name in ['tz_cuda_malloc','tz_cuda_sync','tz_cuda_free','tz_cuda_mem_free','tz_cuda_mem_total']:
        function=getattr(backend,name)
        function.restype=c.c_int64
        function.argtypes=[c.c_int64] if name in ['tz_cuda_malloc','tz_cuda_free'] else []
    pointer=backend.tz_cuda_malloc(4)
    assert pointer, 'real CUDA allocation failed'
    assert fault.trigger_failure()==0, 'failure kernel was not submitted'
    status=backend.tz_cuda_sync()
    record={'sync':status,'free':backend.tz_cuda_free(pointer),
            'memory_free':backend.tz_cuda_mem_free(),'memory_total':backend.tz_cuda_mem_total()}
    record['pass']=all(value<0 for value in record.values())
    (out/'results.json').write_text(json.dumps(record,indent=2)+'\n')
    print(record)
    return int(not record['pass'])


if __name__=='__main__':
    raise SystemExit(main())

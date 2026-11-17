"""Build the complete native CUDA library with an explicit toolkit and SM target.

Windows requires a supported MSVC developer environment. Linux/WSL requires
Linux nvcc and its supported GCC host compiler. No vendor BLAS/RNG is needed.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cuda-root',type=Path,required=True)
    parser.add_argument('--arch',required=True,help='Explicit CUDA architecture, for example sm_75')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if not re.fullmatch(r'sm_[0-9]+[a-z]?',args.arch):
        parser.error('--arch must be a CUDA SM architecture')
    cuda,out=args.cuda_root.resolve(),args.out.resolve()
    nvcc=cuda/'bin'/('nvcc.exe' if os.name=='nt' else 'nvcc')
    if not nvcc.is_file(): parser.error('nvcc is missing from the selected toolkit')
    out.parent.mkdir(parents=True,exist_ok=True)
    command=[str(nvcc),'-std=c++14','-O2','-arch='+args.arch,'-shared','--cudart=shared',
             '-DTN_NATIVE_X64_ABI=1',str(Path(__file__).with_name('tzgpu_kernels.cu')),'-o',str(out)]
    if os.name!='nt':
        libraries=cuda/('lib64' if (cuda/'lib64').exists() else 'lib')
        command+=['-Xcompiler','-fPIC','-L'+str(libraries),'-Xlinker','-rpath,'+str(libraries)]
    return subprocess.run(command).returncode


if __name__=='__main__':
    raise SystemExit(main())

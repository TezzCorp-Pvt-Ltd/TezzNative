"""Build Tezz firmware and execute ATmega328P instructions with real simavr I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from hardware.build import build


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',required=True,type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'build/verification/uno-hardware')
    args=parser.parse_args()
    compiler,out=args.compiler.resolve(),args.out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,TEZZ_SDK_ROOT=str(ROOT/'TezzCorp_WebSites/tn_tezzcorp_com/download/sdk'))
    records=[]

    def execute(command,timeout=120):
        result=subprocess.run([str(value) for value in command],cwd=ROOT,env=env,
                              capture_output=True,text=True,timeout=timeout)
        return {'command':[str(value) for value in command],'exit':result.returncode,
                'stdout':result.stdout,'stderr':result.stderr}

    try:
        cc=shutil.which('gcc')
        if not cc: raise RuntimeError('host GCC and libsimavr-dev are required for the peripheral observer')
        simulator=out/'uno-sim'
        observer=execute([cc,'-std=c11','-Wall','-Wextra','-Werror','-I/usr/include/simavr',
                          ROOT/'tests/hardware/uno_sim.c','-lsimavr','-o',simulator])
        if observer['exit']!=0: raise RuntimeError(json.dumps(observer))
        for fixture,scenario in [('uno_gpio','gpio'),('uno_adc_eeprom','adc'),('uno_uart','uart'),('uno_timing_irq','timing'),('core','core'),('uno_memory','core'),('uno_utf8','core')]:
            record={'case':fixture,'status':'fail'}
            try:
                product=build('arduino-uno',ROOT/f'tests/hardware/{fixture}.tn',compiler,out/fixture)
                result=execute([simulator,product['elf'],scenario],timeout=20)
                record.update(build=product,result=result)
                if result['exit']==0 and 'UNO_SIM_PASS' in result['stdout']: record['status']='pass'
            except (RuntimeError,OSError,subprocess.TimeoutExpired) as error:
                record['error']=str(error)
            records.append(record)
            print(fixture,record['status'],flush=True)
        negative=out/'float-must-not-exist.c'
        negative.unlink(missing_ok=True)
        result=execute([compiler,'buildir',ROOT/'tests/hardware/uno_float_rejected.tn',negative,
                        '--target','arduino-uno','--freestanding'])
        rejected=result['exit']!=0 and not negative.exists() and 'float' in result['stderr'].lower()
        records.append({'case':'uno_float_rejected','status':'pass' if rejected else 'fail','result':result})
        product=build('arduino-uno',ROOT/'tests/hardware/uno_failure.tn',compiler,out/'uno_failure')
        result=execute([simulator,product['elf'],'core'],timeout=20)
        rejected=result['exit']!=0 and 'UNO status=37' in result['stderr'] and 'TEZZ_HW_EXIT=37' in result['stderr']
        records.append({'case':'uno_failure_status','status':'pass' if rejected else 'fail',
                        'expected_firmware_status':37,'build':product,'result':result})
    except (RuntimeError,OSError,subprocess.TimeoutExpired) as error:
        records.append({'case':'setup','status':'fail','error':str(error)})
    report={'compiler':str(compiler),'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),
            'target':'ATmega328P / Arduino UNO R3 16MHz','physical_hardware':False,
            'execution':'simavr AVR instruction/peripheral simulation','results':records,
            'summary':{'pass':sum(record['status']=='pass' for record in records),
                       'fail':sum(record['status']!='pass' for record in records)}}
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('UNO_HARDWARE_SUMMARY',report['summary'])
    for record in records:
        if record['status']!='pass': print(json.dumps(record,indent=2))
    return bool(report['summary']['fail'])


if __name__=='__main__':
    raise SystemExit(main())

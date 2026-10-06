#!/usr/bin/env python3
"""Run reconstructed POPS C on the host; original image bytes are data only."""
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path

from analyze_pops import Image, parse_module_info, parse_imports
from build_psp_hybrid import SOURCE_SHA256

ROOT=Path(__file__).resolve().parents[1]


def last_trace_event(path: Path) -> dict:
    """Read the terminal record without retaining the entire diagnostic log."""
    last = b''
    if path.exists():
        with path.open('rb') as stream:
            for line in stream:
                if line.strip():
                    last = line
    return json.loads(last) if last else {'status': 'no_result'}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--pbp',type=Path)
    ap.add_argument('--diagnostic-skip-ui',action='store_true',
                    help='Explicitly bypass PSP startup UI to investigate core initialization')
    ap.add_argument('--elf',type=Path,default=ROOT/'build/pops_660.prx.dec')
    ap.add_argument('--timeout',type=float,default=30,
                    help='Host execution limit in seconds; not guest-time emulation')
    args=ap.parse_args()
    if not 0 < args.timeout <= 600: ap.error('Timeout must be in (0, 600] seconds')
    if args.out.exists(): ap.error('Refusing existing output directory')
    original=Image(args.elf)
    if hashlib.sha256(original.data).hexdigest()!=SOURCE_SHA256:ap.error('Wrong original POPS hash')
    manifest=json.loads((args.image/'manifest.json').read_text())
    binary=args.image/'pops_image.bin'
    if (manifest.get('source_sha256')!=SOURCE_SHA256 or manifest.get('image_base')!=0 or
        manifest.get('language')!='Allegrex:LE:32:default' or
        manifest.get('image_bytes')!=0x4AE730 or binary.stat().st_size!=0x4AE730 or
        hashlib.sha256(binary.read_bytes()).hexdigest()!=manifest.get('image_sha256')):
        ap.error('Wrong native data image provenance')
    if args.pbp is not None and not args.pbp.is_file():ap.error('PBP path does not exist')
    args.out.mkdir(parents=True)
    imports=parse_imports(original,parse_module_info(original))
    imports_file=args.out/'imports.tsv'
    imports_file.write_text(''.join(f"{fn['stub']:08X}\t{lib['name']}\t{fn['nid']:08X}\n"
                                  for lib in imports for fn in lib['functions']))
    subprocess.run(['make','native'],cwd=ROOT,check=True)
    trace=args.out/'trace.jsonl'
    command=[str(ROOT/'build/repops-native'),str(binary.resolve()),str(imports_file.resolve()),str(trace.resolve())]
    if args.pbp is not None:command.append(str(args.pbp.resolve()))
    if args.diagnostic_skip_ui:command.append('--diagnostic-skip-ui')
    started=time.perf_counter()
    run=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=args.timeout)
    elapsed=time.perf_counter()-started
    (args.out/'stdout.log').write_text(run.stdout+run.stderr)
    last=last_trace_event(trace)
    report={'exit_code':run.returncode,'result':last,'original_sha256':SOURCE_SHA256,
            'host_timeout_seconds':args.timeout,
            'host_execution_seconds':elapsed,
            'diagnostic_ui_bypassed':args.diagnostic_skip_ui,
            'image_sha256':manifest['image_sha256'],'native_binary_sha256':hashlib.sha256((ROOT/'build/repops-native').read_bytes()).hexdigest(),
            'scope':'Reconstructed POPS C plus Unicorn MIPS32 execution of the generated cache; original PRX pages are nonexecutable',
            'limitations':'Not a complete emulator; POPSMAN startup, callbacks, hardware, remaining functions and game execution are incomplete'}
    (args.out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
    print(run.stdout,end='')
    print('Trace:',trace)
    return 0 if run.returncode==78 and last.get('kind')=='result' else 1


if __name__=='__main__':raise SystemExit(main())

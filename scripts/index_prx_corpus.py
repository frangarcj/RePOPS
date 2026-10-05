#!/usr/bin/env python3
"""Inventory local firmware PRX and conservatively join library/NID dependencies.

A dependency superset is not the PSP load order. Multiple providers are kept
explicitly. Dynamic loads and variable imports are not silently called resolved.
"""
import argparse
import hashlib
import json
from collections import defaultdict, deque
from pathlib import Path
from analyze_pops import Image, parse_module_info, parse_exports, parse_imports

ROOTS=('kd/pops_01g.prx','kd/popsman.prx','vsh/module/libpspvmc.prx')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('firmware',type=Path,help='Extracted F0 directory')
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    if args.out.exists():ap.error('Refusing an existing output directory')
    modules={};errors=[];providers=defaultdict(list)
    for path in sorted(args.firmware.rglob('*.prx')):
        relative=path.relative_to(args.firmware).as_posix()
        data=path.read_bytes()
        module={'path':relative,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
        try:
            image=Image(path);info=parse_module_info(image)
            imports=parse_imports(image,info);exports=parse_exports(image,info)
            module.update(name=info['name'],entry=image.entry,info=info,imports=imports,exports=exports)
            for lib in exports:
                if lib['name']=='syslib':continue
                for entry in lib['entries']:
                    if entry['kind']=='function':
                        providers[lib['name'],entry['nid']].append({'path':relative,'address':entry['address']})
        except Exception as exc:
            module['parse_error']=str(exc)
            errors.append({'path':relative,'error':str(exc)})
        modules[relative]=module
    absent=[p for p in ROOTS if p not in modules]
    if absent:ap.error('Missing core modules: '+', '.join(absent))
    queue=deque(ROOTS);seen=set();edges=[];unresolved=[];ambiguous=[];variables=[]
    while queue:
        path=queue.popleft()
        if path in seen:continue
        seen.add(path)
        for lib in modules[path].get('imports',[]):
            if lib['var_count']:
                variables.append({'consumer':path,'library':lib['name'],'count':lib['var_count']})
            for fn in lib['functions']:
                candidates=providers.get((lib['name'],fn['nid']),[])
                edge={'consumer':path,'library':lib['name'],'nid':fn['nid'],'providers':candidates}
                edges.append(edge)
                if not candidates:unresolved.append(edge)
                if len(candidates)>1:ambiguous.append(edge)
                for provider in candidates:queue.append(provider['path'])
    report={'root_modules':list(ROOTS),'scope':'conservative static function-import superset, not a runtime load order',
            'firmware_directory':str(args.firmware),'modules_total':len(modules),'parse_errors':errors,
            'related_module_count':len(seen),'related_modules':sorted(seen),'function_dependency_edges':edges,
            'unresolved_function_imports':unresolved,'ambiguous_function_imports':ambiguous,
            'unresolved_variable_import_groups':variables,'dynamic_loads_accounted_for':False,'modules':modules}
    args.out.mkdir(parents=True)
    (args.out/'corpus.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.out/'related_modules.tsv').write_text('path\tsha256\tbytes\tmodule\n'+''.join(
        f"{p}\t{modules[p]['sha256']}\t{modules[p]['bytes']}\t{modules[p].get('name','UNPARSED')}\n" for p in sorted(seen)))
    print(f"PRX files: {len(modules)}; parsed: {len(modules)-len(errors)}; dependency superset: {len(seen)}")
    print(f"Unresolved function imports: {len(unresolved)}; ambiguous: {len(ambiguous)}; variable groups: {len(variables)}")
    for p in ROOTS:print(p,modules[p]['sha256'])
    print('Report:',args.out/'corpus.json')


if __name__=='__main__':main()

#!/usr/bin/env python3
"""Strict native AOT/Valgrind gate for the Spec 61 container audit.

Historical failures are metadata, never expected-success exceptions.
Artifacts are retained, and compilation/assertion failures are not leak evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def execute(command, directory, env, stage, timeout):
    with (directory/(stage+'.log')).open('w') as output:
        try:
            return subprocess.run(command, cwd=directory, env=env, stdout=output,
                                  stderr=subprocess.STDOUT, timeout=timeout,
                                  preexec_fn=no_core).returncode
        except subprocess.TimeoutExpired:
            output.write('\nTIMEOUT\n')
            return 124


def lost_bytes(log, kind):
    found = re.search(kind+r' lost:\s+([\d,]+) bytes', log)
    if found:
        return int(found.group(1).replace(',', ''))
    if 'All heap blocks were freed -- no leaks are possible' in log:
        return 0
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, help='AIML root; inferred when installed in the project')
    parser.add_argument('--iterations', type=int, nargs='+', default=[200, 400])
    parser.add_argument('--only', nargs='+', help='exact case names from cases.json')
    parser.add_argument('--output', type=Path, help='new artifact directory (must not exist)')
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    if any(n <= 0 for n in args.iterations) or args.timeout <= 0:
        parser.error('iterations and timeout must be positive')
    root = args.root.resolve() if args.root else Path(__file__).resolve().parents[3]
    cases_dir = Path(__file__).resolve().parent
    manifest = json.loads((cases_dir/'cases.json').read_text())
    selected = args.only or list(manifest)
    if any(name not in manifest for name in selected):
        parser.error('unknown --only case')
    compiler = root/'out/aiml/aiml'
    library = root/'out/libaiml/libaiml.so'
    for path in (compiler, library):
        if not path.is_file():
            parser.error(f'missing: {path}')
    for tool in ('g++', 'valgrind'):
        if not shutil.which(tool):
            parser.error(f'missing required tool: {tool}')
    if args.output:
        artifacts = args.output.resolve()
        artifacts.mkdir(parents=True, exist_ok=False)
    else:
        artifacts = Path(tempfile.mkdtemp(prefix='aiml-container-leaks-'))
    env = dict(os.environ, LC_ALL='C', AIML_MEM_V3='1', AIML_LIFETIME_DROPS='1',
               AIML_MODEL_REFCOUNT='0')
    before = {str(p): digest(p) for p in (compiler, library)}
    provenance = {'root': str(root), 'binaries_before': before,
                  'flags': {k: env[k] for k in ('AIML_MEM_V3','AIML_LIFETIME_DROPS','AIML_MODEL_REFCOUNT')},
                  'cases': {name: digest(cases_dir/(name+'.aiml')) for name in selected}}
    (artifacts/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(f'Artifacts: {artifacts}', flush=True)
    results = []
    for name in selected:
        for count in args.iterations:
            directory = artifacts/f'{name}-{count}'
            directory.mkdir()
            source = (cases_dir/(name+'.aiml')).read_text()
            marker = 'condition: {lt: [counter, 200]}'
            if source.count(marker) != 1:
                raise RuntimeError(f'{name}: iteration marker must occur exactly once')
            (directory/'main.aiml').write_text(source.replace(marker, f'condition: {{lt: [counter, {count}]}}'))
            result = dict(case=name, iterations=count, **manifest[name])
            build = execute([str(compiler),str(directory/'main.aiml'),'-o',str(directory/'main.o'),
                             '--log-level','ERROR'],directory,env,'build',args.timeout)
            result['build_exit'] = build
            if build:
                result['status'] = 'BUILD_FAIL'
            else:
                link = execute(['g++',str(directory/'main.o'),'-o',str(directory/'bin'),
                    '-L'+str(library.parent),'-Wl,-rpath,'+str(library.parent),'-laiml',
                    '-lyaml-cpp','-lpthread','-lssl','-lcrypto','-ldl'],directory,env,'link',args.timeout)
                result['link_exit'] = link
                if link:
                    result['status'] = 'LINK_FAIL'
                else:
                    native = execute([str(directory/'bin')],directory,env,'native',args.timeout)
                    vg = execute(['valgrind','--leak-check=full','--show-leak-kinds=definite,indirect',
                        '--errors-for-leak-kinds=definite,indirect','--error-exitcode=99','--num-callers=20',
                        '--log-file='+str(directory/'valgrind.log'),str(directory/'bin')],
                        directory,env,'valgrind-program',args.timeout)
                    log = (directory/'valgrind.log').read_text() if (directory/'valgrind.log').exists() else ''
                    direct, indirect = lost_bytes(log,'definitely'), lost_bytes(log,'indirectly')
                    summary = re.search(r'ERROR SUMMARY:\s+([\d,]+) errors',log)
                    errors = int(summary.group(1).replace(',','')) if summary else None
                    result.update(native_exit=native,valgrind_exit=vg,definite=direct,indirect=indirect,errors=errors)
                    unsafe = re.search(r'Invalid (read|write|free)|uninitialised|uninitialized|Mismatched free|Process terminating with default action',log)
                    if native == 124 or vg == 124:
                        status = 'TIMEOUT'
                    elif unsafe or native < 0 or vg < 0:
                        status = 'SAFETY_FAIL'
                    elif errors is None or direct is None or indirect is None:
                        status = 'INCOMPLETE_LOG'
                    elif native != 0 or vg not in (0,99):
                        status = 'ASSERTION_OR_RUN_FAIL'
                    elif direct or indirect:
                        status = 'LEAK'
                    elif errors or vg:
                        status = 'VALGRIND_FAIL'
                    else:
                        status = 'PASS'
                    result['status'] = status
            results.append(result)
            (artifacts/'results.json').write_text(json.dumps(results,indent=2)+'\n')
            print(f'{result["status"]:22} {name:34} N={count} '
                  f'def={result.get("definite","?")} ind={result.get("indirect","?")} '
                  f'native={result.get("native_exit","?")}',flush=True)
    after = {str(p): digest(p) for p in (compiler, library)}
    provenance['binaries_after'] = after
    provenance['stable_binaries'] = before == after
    (artifacts/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    failed = sum(r['status'] != 'PASS' for r in results)
    print(f'{len(results)-failed} passed, {failed} failed; binaries stable: {before == after}')
    return int(failed != 0 or before != after)


if __name__ == '__main__':
    raise SystemExit(main())

#!/usr/bin/env python3
"""Trace regression: native (default), plus WASM with --wasm. Run after exodus build --all."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def run(test, lifetime, symbols, trace, wasm=False):
    env = dict(os.environ, AIML_CACHE='0', AIML_LIFETIME_DROPS=str(lifetime),
               AIML_TRACE_SYMBOL=symbols, AIML_TRACE=trace)
    cmd = (['bash', 'tools/run_wasm_test.sh', test] if wasm else
           ['./out/aiml/aiml', '-r', test])
    result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace', timeout=180)
    assert result.returncode == 0, result.stdout[-6000:]
    assert not re.search(r'Invalid UTF|verification FAILED|wasm-ld: error|RuntimeError', result.stdout), result.stdout[-6000:]
    return result.stdout

def diagnostics(wasm):
    for selection in ('', 'length_first'):
        log = run('tests/test_trace_string_len_first.aiml', 1, '', selection, wasm)
        assert 'PASS trace_string_len_first' in log
    print(f'PASS {"wasm" if wasm else "native"} string.len before concat')
    for lifetime in (0, 1):
        log = run('tests/test_trace_diagnostics.aiml', lifetime, 'all',
                  'trace_args,trace_numbers', wasm)
        assert 'PASS trace_diagnostics' in log
        assert log.count('[TRACE] => trace_args(') == 3
        assert 'txt=<null>' in log
        assert 'small=-123, large=1234567890123, frac=1.25, yes=1' in log
        assert re.search(r'ptr=(?:\(nil\)|0x0|0), face=\{obj=', log)
        assert 'BEFORE_SET <.items' in log and 'SET <.items' in log
        assert 'data@' in log and 'decl=' in log
        box = re.search(r'DECL box .*box@(\S+)', log).group(1)
        receiver = re.search(r'DECL receiver .*iface_obj@(\S+)', log).group(1)
        assert box == receiver
        if lifetime:
            assert re.search(r'DROP receiver .*iface_obj@' + re.escape(box), log)
            assert 'MOVE box ' in log and 'MOVE fresh ' in log
            assert 'DROP box ' not in log and 'DROP fresh ' not in log
        print(f'PASS {"wasm" if wasm else "native"} diagnostics lifetime={lifetime}')
    quiet = run('tests/test_trace_diagnostics.aiml', 1, '', '', wasm)
    assert 'PASS trace_diagnostics' in quiet
    assert '[TRACE]' not in quiet and '[aiml-trace]' not in quiet
    print(f'PASS {"wasm" if wasm else "native"} tracing disabled')
    stress = run('tests/test_trace_stress.aiml', 1, '', 'trace_probe', wasm)
    assert 'PASS trace_stress' in stress
    assert stress.count('[TRACE] => trace_probe(') == 20000
    print(f'PASS {"wasm" if wasm else "native"} 20000 calls')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--wasm', action='store_true')
    args = parser.parse_args()
    diagnostics(False)
    if args.wasm:
        diagnostics(True)

#!/usr/bin/env python3
"""Helper for inspecting and importing patches/files from a local Pokemon-FireRed-3DS checkout.

Reads upstream.lock to track the pinned base and target commits of Pokemon-FireRed-3DS, audits
patches or 3ds_port scripts for #ifdef PLATFORM_3DS guards and 3DS-specific headers, and can
optionally apply allowlisted patches with PLATFORM_3DS -> PLATFORM_VITA remapping.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
LOCK_FILE = os.path.join(ROOT, 'upstream.lock')

FORBIDDEN_3DS_PATTERNS = (
    re.compile(r'#include\s*<3ds\.h>'),
    re.compile(r'#include\s*<citro3d\.h>'),
    re.compile(r'#include\s*<citro2d\.h>'),
    re.compile(r'\bromfsInit\b'),
)


def parse_lock():
    data = {'applied': [], 'pending': [], 'branched': '', 'target': ''}
    if not os.path.exists(LOCK_FILE):
        return data
    section = None
    with open(LOCK_FILE, encoding='utf-8') as f:
        for raw in f:
            line = raw.split('#', 1)[0].rstrip()
            if not line.strip():
                continue
            if 'branched_commit:' in line:
                data['branched'] = line.split(':', 1)[1].strip()
            elif 'target_commit:' in line:
                data['target'] = line.split(':', 1)[1].strip()
            elif 'applied_patches:' in line:
                section = 'applied'
            elif 'pending_patches:' in line:
                section = 'pending'
            elif line.lstrip().startswith('- ') and section in ('applied', 'pending'):
                data[section].append(line.lstrip()[2:].strip())
    return data


def audit_patch_text(name, text):
    p3ds_hits = len(re.findall(r'\bPLATFORM_3DS\b', text))
    pbridge_hits = len(re.findall(r'\bPORT_BRIDGE\b', text))
    hazards = [pat.pattern for pat in FORBIDDEN_3DS_PATTERNS if pat.search(text)]
    return {
        'name': name,
        'platform_3ds': p3ds_hits,
        'port_bridge': pbridge_hits,
        'hazards': hazards,
    }


def list_upstream(upstream_dir, lock):
    patch_dir = os.path.join(upstream_dir, 'patches', 'pokefirered')
    if not os.path.isdir(patch_dir):
        raise SystemExit(f'Missing patches/pokefirered in {upstream_dir}')
    patches = sorted(p for p in os.listdir(patch_dir) if p.endswith('.patch'))
    applied = set(lock['applied'])
    pending = set(lock['pending'])
    print(f"upstream.lock: branched={lock['branched']} target={lock['target']}")
    print(f'Found {len(patches)} patches in {patch_dir}:')
    for p in patches:
        txt = open(os.path.join(patch_dir, p), encoding='utf-8', errors='replace').read()
        info = audit_patch_text(p, txt)
        status = 'APPLIED' if p in applied else ('PENDING' if p in pending else 'NEW')
        notes = []
        if info['platform_3ds']:
            notes.append(f"PLATFORM_3DS={info['platform_3ds']}")
        if info['port_bridge']:
            notes.append(f"PORT_BRIDGE={info['port_bridge']}")
        if info['hazards']:
            notes.append(f"HAZARDS={','.join(info['hazards'])}")
        note_str = f" ({', '.join(notes)})" if notes else ''
        print(f'  [{status:7s}] {p}{note_str}')


def rewrite_and_preview_patch(upstream_dir, patch_name, out_dir=None, apply_patch=False):
    src = os.path.join(upstream_dir, 'patches', 'pokefirered', patch_name)
    if not os.path.isfile(src):
        raise SystemExit(f'Patch not found: {src}')
    raw = open(src, encoding='utf-8').read()
    mapped = re.sub(r'\bPLATFORM_3DS\b', 'PLATFORM_VITA', raw)
    info = audit_patch_text(patch_name, raw)
    print(f"{patch_name}: remapped PLATFORM_3DS ({info['platform_3ds']} hits) -> PLATFORM_VITA")
    if info['hazards']:
        print(f"  WARNING: 3DS-specific tokens detected: {info['hazards']}")
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, patch_name)
        with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(mapped)
        print(f'  Wrote Vita-mapped patch to {out_path}')
    if apply_patch:
        proc = subprocess.run(['git', 'apply', '--check', '-'], input=mapped.encode('utf-8'), cwd=ROOT)
        if proc.returncode != 0:
            raise SystemExit(f'git apply --check failed for {patch_name}')
        subprocess.run(['git', 'apply', '-'], input=mapped.encode('utf-8'), cwd=ROOT, check=True)
        print(f'  Applied {patch_name} cleanly to working tree.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--upstream', help='Path to local Pokemon-FireRed-3DS checkout')
    ap.add_argument('--list', action='store_true', help='Compare local upstream.lock against patches/pokefirered')
    ap.add_argument('--patch', help='Specific patch filename to audit/remap (e.g. 0024-animation-null-argument.patch)')
    ap.add_argument('--out-dir', help='Optional directory to write remapped patch')
    ap.add_argument('--apply', action='store_true', help='Apply remapped patch to working tree via git apply')
    args = ap.parse_args()

    lock = parse_lock()
    if not args.upstream:
        print(f"upstream.lock: branched={lock['branched']}, target={lock['target']}")
        print(f"  applied: {len(lock['applied'])}, pending: {len(lock['pending'])}")
        for p in lock['pending']:
            print(f'  pending: {p}')
        return

    if args.list:
        list_upstream(args.upstream, lock)
    if args.patch:
        rewrite_and_preview_patch(args.upstream, args.patch, args.out_dir, args.apply)


if __name__ == '__main__':
    main()

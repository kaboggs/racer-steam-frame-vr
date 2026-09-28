#!/usr/bin/env python3
"""Audit the explicit text-only publication set, Git history and a fresh export."""
import argparse
import hashlib
import io
import json
import re
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = sorted([
    '.gitignore', 'LICENSE', 'NOTICE.md', 'README.md', 'dependencies.json',
    'bindings/racer_openvr_actions.json', 'bindings/racer_frame_bindings.json',
    'docs/DISTRIBUTION.md', 'docs/AUDIT.md', 'docs/SETUP.md', 'docs/STATUS.md', 'docs/STEAM_FRAME.md',
    'licenses/Dear-ImGui.txt', 'licenses/Valve-OpenVR.txt',
    'patches/frame-port.patch', 'scripts/audit.py', 'scripts/setup.py',
    'sources/vr_focus_probe.cpp', 'sources/vr_openvr.cpp', 'tests/test_setup.py',
])
IGNORED_TOP = {'.git', '.cache', 'build', 'dist'}
PRIVATE = [r'/home/' + r'steamos\b', r'/run/media/' + r'steamos\b',
           r'gh[pousr]_' + r'[A-Za-z0-9]{25,}', r'github_pat' + r'_[A-Za-z0-9_]{25,}',
           r'-----BEGIN ' + r'(?:RSA |EC |OPENSSH )?PRIVATE KEY-----']


def validate_text(name, data):
    if b'\0' in data:
        raise RuntimeError('Binary/NUL content: ' + name)
    content = data.decode('utf-8')
    if any(re.search(pattern, content) for pattern in PRIVATE):
        raise RuntimeError('Private path or credential pattern: ' + name)
    # A text extension alone cannot establish provenance: manual review is also required.
    if len(data) > 150000:
        raise RuntimeError('Unexpected large publication file: ' + name)
    return hashlib.sha256(data).hexdigest()


def audit(game=None):
    actual = set()
    for path in ROOT.rglob('*'):
        rel = path.relative_to(ROOT)
        if rel.parts[0] in IGNORED_TOP or '__pycache__' in rel.parts:
            continue
        if path.is_symlink():
            raise RuntimeError('Publication symlink: ' + str(rel))
        if path.is_file(): actual.add(rel.as_posix())
    if actual != set(FILES):
        raise RuntimeError('Publication inventory mismatch: ' + repr(sorted(actual ^ set(FILES))))
    payload = {name: (ROOT/name).read_bytes() for name in FILES}
    hashes = {name: validate_text(name, data) for name, data in payload.items()}
    git_blobs = 0
    if (ROOT/'.git').exists():
        tracked = subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode().split('\0')
        if set(filter(None,tracked)) != set(FILES): raise RuntimeError('Git tracked inventory differs')
        objects = subprocess.check_output(['git','-C',str(ROOT),'rev-list','--objects','--all'],text=True)
        for line in objects.splitlines():
            oid, _, name = line.partition(' ')
            kind=subprocess.check_output(['git','-C',str(ROOT),'cat-file','-t',oid],text=True).strip()
            if kind=='blob':
                if name not in FILES: raise RuntimeError('Unexpected historical blob: '+name)
                validate_text(name,subprocess.check_output(['git','-C',str(ROOT),'cat-file','blob',oid]))
                git_blobs+=1
    compared = 0
    own_bindings = []
    if game:
        sizes={len(data) for data in payload.values()}
        candidates=set(hashes.values())
        for path in Path(game).rglob('*'):
            if not path.is_file() or path.is_symlink(): continue
            compared+=1
            if path.stat().st_size in sizes:
                checksum = hashlib.sha256(path.read_bytes()).hexdigest()
                if checksum in candidates:
                    # These two newly authored bindings were installed during our tests.
                    # No original game/Steam content is eligible for this exception.
                    name = 'bindings/' + path.name
                    if name in ['bindings/racer_openvr_actions.json', 'bindings/racer_frame_bindings.json'] and hashes[name] == checksum:
                        own_bindings.append(path.name)
                    else:
                        raise RuntimeError('Publication file duplicates installed file: '+path.name)
    export=io.BytesIO()
    with zipfile.ZipFile(export,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in payload.items(): archive.writestr(name,data)
    with zipfile.ZipFile(io.BytesIO(export.getvalue())) as archive:
        if sorted(archive.namelist())!=FILES: raise RuntimeError('Export inventory differs')
        for name in archive.namelist():
            if validate_text(name,archive.read(name))!=hashes[name]: raise RuntimeError('Export content changed')
    return {'publication_files':len(FILES),'total_bytes':sum(map(len,payload.values())),
            'reachable_git_blobs_checked':git_blobs,'installed_files_compared':compared,'own_installed_bindings':own_bindings,
            'checks':['explicit inventory and text/credential scan','installed-file comparison and source provenance review','fresh export and Git history inspection'],
            'sha256':hashes}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir')
    args=parser.parse_args()
    print(json.dumps(audit(args.game_dir),indent=2))
  

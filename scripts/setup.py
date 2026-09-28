#!/usr/bin/env python3
"""Build a pinned source port; install/restore only an allowlisted mod payload."""
import argparse
import datetime
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache'
PAYLOAD = ROOT / 'dist' / 'payload'
LOCK = json.loads((ROOT / 'dependencies.json').read_text())


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(url, dest, expected):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists() or digest(dest) != expected:
        temporary = dest.with_suffix(dest.suffix + '.download')
        with urllib.request.urlopen(url) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        if digest(temporary) != expected:
            temporary.unlink()
            raise RuntimeError('Downloaded file failed SHA256 verification: ' + dest.name)
        temporary.replace(dest)


def build(args):
    for command in ['git', 'cmake']:
        if not shutil.which(command):
            raise RuntimeError('Install build prerequisite: ' + command)
    arch = platform.machine()
    if arch not in LOCK['compiler']['sha256']:
        raise RuntimeError('Build host must be Linux aarch64 or x86_64')
    if platform.system() != 'Linux':
        raise RuntimeError('This build script targets Linux hosts')
    CACHE.mkdir(exist_ok=True)
    source = CACHE / 'upstream'
    if not source.exists():
        run('git', 'init', source)
        run('git', '-C', source, 'remote', 'add', 'origin', LOCK['upstream']['url'])
        run('git', '-C', source, 'fetch', '--depth=1', 'origin', LOCK['upstream']['commit'])
        run('git', '-C', source, 'checkout', '--detach', 'FETCH_HEAD')
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if commit != LOCK['upstream']['commit']:
        raise RuntimeError('Cache checkout does not match pinned commit; use a fresh cache')
    patch = ROOT / 'patches/frame-port.patch'
    # Idempotent: accept exactly our applied patch, otherwise apply cleanly.
    applied = subprocess.run(['git', '-C', str(source), 'apply', '--reverse', '--check', str(patch)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not applied:
        run('git', '-C', source, 'apply', '--check', patch)
        run('git', '-C', source, 'apply', patch)
    shutil.copy2(ROOT / 'sources/vr_openvr.cpp', source / 'dinput_hook/vr_openxr.cpp')
    version = LOCK['compiler']['version']
    name = f'llvm-mingw-{version}-ucrt-ubuntu-22.04-{arch}'
    archive = CACHE / (name + '.tar.xz')
    compiler = CACHE / name
    fetch(f'https://github.com/mstorsjo/llvm-mingw/releases/download/{version}/{archive.name}',
          archive, LOCK['compiler']['sha256'][arch])
    if not compiler.exists():
        with tarfile.open(archive) as tar:
            # Reject escaping paths and links; expected vendor archive stays under CACHE.
            for member in tar.getmembers():
                target = (CACHE / member.name).resolve()
                if not target.is_relative_to(CACHE.resolve()):
                    raise RuntimeError('Unsafe compiler archive path')
                if member.issym() or member.islnk():
                    link = ((target.parent if member.issym() else CACHE) / member.linkname).resolve()
                    if not link.is_relative_to(CACHE.resolve()):
                        raise RuntimeError('Unsafe compiler archive link')
            tar.extractall(CACHE)
    venv = CACHE / 'venv'
    if not (venv / 'bin/python').exists():
        run(sys.executable, '-m', 'venv', venv)
    python = venv / 'bin/python'
    run(python, '-m', 'pip', 'install', 'jinja2==' + LOCK['python']['jinja2'])
    toolchain = CACHE / 'mingw-toolchain.cmake'
    bin_dir = compiler / 'bin'
    toolchain.write_text('set(CMAKE_SYSTEM_NAME Windows)\nset(CMAKE_SYSTEM_PROCESSOR x86)\n' +
        '\n'.join(f'set({var} "{bin_dir / executable}")' for var, executable in [
            ('CMAKE_C_COMPILER', 'i686-w64-mingw32-clang'),
            ('CMAKE_CXX_COMPILER', 'i686-w64-mingw32-clang++'),
            ('CMAKE_RC_COMPILER', 'i686-w64-mingw32-windres')]) + '\n')
    # Upstream's CMake glob needs this generated file to exist at first configure.
    generated = source / 'src/generated/hook_generated.c'
    if not generated.exists():
        generated.parent.mkdir(parents=True, exist_ok=True)
        generated.touch()
    output = ROOT / 'build/artifacts'
    output.mkdir(parents=True, exist_ok=True)
    build_dir = ROOT / 'build/cmake'
    run('cmake', '-S', source, '-B', build_dir, '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain), '-DPYTHON_EXECUTABLE=' + str(python),
        '-DGAME_DIR=' + str(output))
    run('cmake', '--build', build_dir, '-j', str(args.jobs))
    PAYLOAD.mkdir(parents=True, exist_ok=True)
    shutil.copy2(output / 'dinput.dll', PAYLOAD / 'dinput.dll')
    commit = LOCK['openvr']['commit']
    fetch(f'https://raw.githubusercontent.com/ValveSoftware/openvr/{commit}/bin/win32/openvr_api.dll',
          PAYLOAD / 'openvr_api.dll', LOCK['openvr']['loader_sha256'])
    for file in sorted((source / 'assets/shaders').iterdir()):
        if file.suffix not in ['.vert', '.frag'] or file.is_symlink():
            continue
        dest = PAYLOAD / 'assets/shaders' / file.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, dest)
    for file in (ROOT / 'bindings').glob('*.json'):
        dest = PAYLOAD / 'assets' / file.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, dest)
    files = {str(f.relative_to(PAYLOAD)): digest(f) for f in PAYLOAD.rglob('*') if f.is_file()}
    (ROOT / 'dist/manifest.json').write_text(json.dumps({'files': files, 'upstream_commit':
        LOCK['upstream']['commit']}, indent=2) + '\n')
    print('Built local mod-only payload:', PAYLOAD)


def game_dir(value):
    game = Path(value).expanduser().resolve()
    if not (game / 'SWEP1RCR.EXE').is_file():
        raise RuntimeError('Game directory must contain your installed SWEP1RCR.EXE')
    # A running game may have a mapped DLL; replacing it is unsafe.
    ps = subprocess.check_output(['ps', '-eo', 'comm='], text=True)
    if any('SWEP1RCR' in line.upper() for line in ps.splitlines()):
        raise RuntimeError('Close Racer before installing or restoring')
    return game


def allowed(name):
    p = Path(name)
    return (name in ['dinput.dll', 'openvr_api.dll', 'assets/racer_openvr_actions.json',
                     'assets/racer_frame_bindings.json'] or
            (p.parent == Path('assets/shaders') and p.suffix in ['.vert', '.frag']))


def payload_files():
    manifest = json.loads((ROOT / 'dist/manifest.json').read_text())['files']
    required = {'dinput.dll', 'openvr_api.dll', 'assets/racer_openvr_actions.json',
                'assets/racer_frame_bindings.json'}
    if not required <= set(manifest):
        raise RuntimeError('Incomplete payload; rebuild')
    actual = {str(f.relative_to(PAYLOAD)) for f in PAYLOAD.rglob('*') if f.is_file()}
    if actual != set(manifest):
        raise RuntimeError('Payload has extra or missing files; rebuild in a clean dist directory')
    for path in PAYLOAD.rglob("*"):
        if path.is_symlink():
            raise RuntimeError("Payload symlink: " + str(path.relative_to(PAYLOAD)))
    for name, checksum in manifest.items():
        if not allowed(name) or (PAYLOAD / name).is_symlink():
            raise RuntimeError('Non-mod payload entry: ' + name)
        if digest(PAYLOAD / name) != checksum:
            raise RuntimeError('Payload checksum mismatch: ' + name)
    return manifest


def safe_target(game, name):
    target = game / name
    if not target.resolve().is_relative_to(game) or target.is_symlink():
        raise RuntimeError('Refusing destination symlink or escaping path: ' + name)
    return target


def install(args):
    game = game_dir(args.game_dir)
    files = payload_files()
    targets = {name: safe_target(game, name) for name in files}
    for target in targets.values():
        temporary = target.with_name(target.name + '.racer-frame-installing')
        if temporary.exists() or temporary.is_symlink():
            raise RuntimeError('Temporary installer file already exists: ' + str(temporary))
    for name in files:
        print(('Would install ' if args.dry_run else 'Install ') + name)
    if args.dry_run:
        print('Dry run: no files changed.'); return
    backup = Path.home() / '.local/share/racer-frame-vr/backups' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True)
    entries = {}
    # Back up the complete allowlisted destination set before the first overwrite.
    for name, target in targets.items():
        entries[name] = {'existed': target.exists(), 'installed_sha256': files[name]}
        if target.exists():
            dest = backup / 'files' / name
            dest.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(target, dest)
    (backup / 'record.json').write_text(json.dumps({'game_dir': str(game), 'files': entries}, indent=2) + '\n')
    for name, target in targets.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + '.racer-frame-installing')
        if temporary.exists():
            raise RuntimeError('Temporary installer file already exists: ' + str(temporary))
        shutil.copy2(PAYLOAD / name, temporary); temporary.replace(target)
    print('Backup/restore record:', backup)
    print('Set Proton Experimental ARM64 and the launch options in docs/SETUP.md. No game launched.')


def restore(args):
    backup = Path(args.backup).expanduser().resolve()
    record = json.loads((backup / 'record.json').read_text())
    game = game_dir(record['game_dir'])
    # Validate every entry before touching any file. Never overwrite later user changes.
    for name, entry in record['files'].items():
        if not allowed(name): raise RuntimeError('Unsafe backup entry: ' + name)
        target = safe_target(game, name)
        if not target.is_file() or digest(target) != entry['installed_sha256']:
            raise RuntimeError('Installed file changed or missing; preserve it and restore manually: ' + name)
        original = backup / 'files' / name
        if entry['existed'] and (not original.is_file() or original.is_symlink() or
                                 not original.resolve().is_relative_to(backup)):
            raise RuntimeError('Missing original backup: ' + name)
    for name, entry in record['files'].items():
        print(('Would restore ' if args.dry_run else 'Restore ') + name)
        if args.dry_run: continue
        target = game / name
        if entry['existed']: shutil.copy2(backup / 'files' / name, target)
        else: target.unlink()
    print('Steam launch settings are unchanged; clear the overrides manually if returning to vanilla.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    b = sub.add_parser('build'); b.add_argument('--jobs', type=int, default=4); b.set_defaults(func=build)
    i = sub.add_parser('install'); i.add_argument('--game-dir', required=True); i.add_argument('--dry-run', action='store_true'); i.set_defaults(func=install)
    r = sub.add_parser('restore'); r.add_argument('--backup', required=True); r.add_argument('--dry-run', action='store_true'); r.set_defaults(func=restore)
    args = parser.parse_args()
    try: args.func(args)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as e:
        print('Error:', e, file=sys.stderr); return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
  

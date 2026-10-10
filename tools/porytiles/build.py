#!/usr/bin/env python3
"""Fetch and build the pinned Porytiles version with extended tileset and palette support."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

COMMIT = '4c244d587c3daf16447366d0d8398b84a28370fe'
HERE = Path(__file__).resolve().parent


def run(*args, cwd=None, env=None):
    subprocess.run(args, cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=HERE / 'build')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'), help='C++23 compiler (GCC 15+ or Clang 18+)')
    parser.add_argument('--prefix-path', help='extra CMAKE_PREFIX_PATH, e.g. a conda env holding libpng and zlib')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--test', action='store_true', help='also build and run the test suite')
    parser.add_argument('--install', type=Path, help='copy the stripped porytiles binary into this directory')
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    directory = args.directory.resolve()
    source = directory / 'source'
    if not source.exists():
        source.mkdir(parents=True)
        run('git', 'init', str(source))
        run('git', 'remote', 'add', 'origin', 'https://github.com/grunt-lucas/porytiles.git', cwd=source)
        run('git', 'fetch', '--depth=1', 'origin', COMMIT, cwd=source)
        run('git', 'checkout', '--detach', 'FETCH_HEAD', cwd=source)
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    if actual != COMMIT:
        parser.error(f'{source} must be at {COMMIT}; refusing to change an existing checkout')
    patch = str(HERE / 'extended.patch')
    applied = subprocess.run(['git', 'apply', '--reverse', '--check', patch], cwd=source, capture_output=True)
    if applied.returncode:
        run('git', 'apply', '--check', patch, cwd=source)
        run('git', 'apply', patch, cwd=source)
    if args.no_build:
        return

    build = directory / 'cmake'
    configure = ['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release',
                 f'-DCMAKE_CXX_COMPILER={args.cxx}', '-DPORYTILES_BUILD_DOCS=OFF',
                 # Lets the binary run without the (often newer) C++ runtime of the compiler that built it.
                 '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc']
    if args.prefix_path:
        configure.append(f'-DCMAKE_PREFIX_PATH={args.prefix_path}')
    if shutil.which('ninja'):
        configure += ['-G', 'Ninja']
    run(*configure)
    targets = ['porytiles'] + (['PorytilesAllTests'] if args.test else [])
    run('cmake', '--build', str(build), '--parallel', str(args.jobs), '--target', *targets)
    if args.test:
        # The tests read their fixtures relative to the repository root.
        run(str(build / 'porytiles/tests/PorytilesAllTests'), cwd=source)
    binary = build / 'porytiles/tools/driver/porytiles'
    if args.install:
        args.install.mkdir(parents=True, exist_ok=True)
        target = args.install / 'porytiles'
        shutil.copy2(binary, target)
        if shutil.which('strip'):
            run('strip', str(target))
        binary = target
    print(f'Porytiles: {binary}')


if __name__ == '__main__':
    main()

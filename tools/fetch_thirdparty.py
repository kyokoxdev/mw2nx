"""Fetches the third-party sources of the SDK into sdk/thirdparty.

sdk/thirdparty only carries the files this project changed (see THIRD_PARTY_NOTICES.md). Everything else comes from
the ReXGlue SDK release the SDK is based on and its submodules. This script clones that release with its submodules
into a temporary folder and copies every file that sdk/thirdparty does not have yet; files already there are kept.

Usage: python tools/fetch_thirdparty.py
Needs git.

Derived from GPL-3.0 code; modified for bo2-zm-nx in 2026.
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile

UPSTREAM = 'https://github.com/rexglue/rexglue-sdk.git'
COMMIT = 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518'  # v0.10.0


def run(*args, cwd=None):
    print('>', ' '.join(args))
    subprocess.run(args, cwd=cwd, check=True)


def remove_readonly(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, 'sdk', 'thirdparty')
    # Kept between runs (out/ is ignored): a failed copy or submodule fetch resumes instead of re-downloading.
    work = os.path.join(root, 'out', 'rexglue-sdk-src')
    if not os.path.isdir(os.path.join(work, '.git')):
        os.makedirs(work, exist_ok=True)
        run('git', 'init', '-q', work)
        run('git', 'remote', 'add', 'origin', UPSTREAM, cwd=work)
        run('git', 'fetch', '-q', '--depth', '1', 'origin', COMMIT, cwd=work)
        run('git', 'checkout', '-q', 'FETCH_HEAD', cwd=work)
    run('git', 'submodule', 'update', '--init', '--recursive', cwd=work)
    source = os.path.join(work, 'thirdparty')
    copied = kept = 0
    for dirpath, dirnames, filenames in os.walk(source):
        dirnames[:] = [d for d in dirnames if d != '.git']
        for name in filenames:
            if name == '.git':
                continue
            src = os.path.join(dirpath, name)
            dst = os.path.join(target, os.path.relpath(src, source))
            if os.path.lexists(dst):
                kept += 1
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst, follow_symlinks=False)  # submodules hold dangling links (moltenvk)
            copied += 1
    print(f'{copied} files copied into sdk/thirdparty, {kept} files of this project kept')


if __name__ == '__main__':
    main()

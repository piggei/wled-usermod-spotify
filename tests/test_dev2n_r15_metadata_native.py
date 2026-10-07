#!/usr/bin/env python3
"""Compile and execute the exact diagnostic parser used by firmware, offline.
Set SPOTIFY_AUDIT_SANITIZERS=1 for the ASan/UBSan run; no PlatformIO needed.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cxx = shlex.split(os.environ.get('CXX', 'g++'))
if not cxx or not shutil.which(cxx[0]):
    raise SystemExit('metadata audit native test requires a host C++11 compiler (set CXX).')
with tempfile.TemporaryDirectory(prefix='spotify-metadata-audit-') as tmp:
    binary = Path(tmp) / 'audit_test'
    flags = ['-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic']
    if os.environ.get('SPOTIFY_AUDIT_SANITIZERS') == '1':
        flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    subprocess.run(cxx + flags + ['-I', str(root), str(root / 'spotify/SpotifyMetadataAudit.cpp'),
                   str(root / 'tests/host/metadata_audit_test.cpp'), '-o', str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)

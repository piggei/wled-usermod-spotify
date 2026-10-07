#!/usr/bin/env python3
"""Execute the real r16 state machine and the frozen RequestKey wire builder.
No network/Arduino/keys from a real account. Set SPOTIFY_PROBE_SANITIZERS=1 for ASan/UBSan.
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
    raise SystemExit('AudioKey probe native test requires a host C++11 compiler (CXX).')
source=(root/'spotify/SpotifySessionProbe.cpp').read_text()
a=source.index('std::vector<uint8_t> buildAudioKeyRequest(')
b=source.index('std::vector<uint8_t> buildApStreamChunkRequest(',a)
with tempfile.TemporaryDirectory(prefix='spotify-key-probe-') as tmp:
    p=Path(tmp); binary=p/'key_probe_test'
    wire=p/'wire.cpp'
    wire.write_text('#include <stdint.h>\n#include <stddef.h>\n#include <vector>\n'
                    'constexpr size_t AUDIO_FILE_ID_BYTES=20u; constexpr size_t TRACK_GID_BYTES=16u;\n'+source[a:b])
    flags=['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic']
    if os.environ.get('SPOTIFY_PROBE_SANITIZERS')=='1':
        flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run(cxx+flags+['-I',str(root),str(root/'spotify/SpotifyAudioKeyProbe.cpp'),str(wire),
                    str(root/'tests/host/audio_key_probe_test.cpp'),'-o',str(binary)],check=True,timeout=60)
    subprocess.run([str(binary)],check=True,timeout=30)

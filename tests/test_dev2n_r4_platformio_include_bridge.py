#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
lib = json.loads((root / 'library.json').read_text())
override = (root / 'platformio_override.example.ini').read_text()
bridge = (root / 'tools' / 'platformio_micro_vorbis_compat.py').read_text()
ui = (root / 'usermod_spotify_connect.h').read_text()

assert lib['version'] == '0.1.0-dev.2n-vorbis-r17'
assert 'USERMOD_REVISION = "r17"' in ui
assert 'lib_compat_mode = off' in override
assert 'esphome/micro-vorbis@^0.1.0' in override
assert 'pre:../wled-usermod-spotify/tools/platformio_micro_vorbis_compat.py' in override
assert 'framework = espidf' not in override

# r4 must bridge the CMake-only private include layout without patching the
# downloaded dependency. Both observed failures are covered: ivorbiscodec.h in
# src/tremor and namespace-style ogg/ogg.h in a nested include directory.
for token in (
    'src/tremor',
    'lib/micro-ogg-demuxer/include',
    'lib/ogg/include',
    'lib/libogg/include',
    'rglob("*.h")',
    'env.AppendUnique(CPPPATH=include_paths)',
):
    assert token in bridge, token
assert '.pio/libdeps' not in bridge  # use PlatformIO PROJECT_LIBDEPS_DIR instead
assert 'patch' not in bridge.lower() or 'patch files' in bridge.lower()

print('dev.2n-r4 PlatformIO private-include bridge: PASS')

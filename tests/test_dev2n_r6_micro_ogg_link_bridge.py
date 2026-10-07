#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
lib = json.loads((root / "library.json").read_text())
override = (root / "platformio_override.example.ini").read_text()
bridge = (root / "tools" / "platformio_micro_vorbis_compat.py").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()

assert lib["version"] == "0.1.0-dev.2n-vorbis-r17"
assert 'USERMOD_REVISION = "r17"' in ui
assert 'lib_compat_mode = off' in override
assert 'esphome/micro-vorbis@^0.1.0' in override
assert '${scripts_defaults.extra_scripts}' in override
assert 'pre:../wled-usermod-spotify/tools/platformio_micro_vorbis_compat.py' in override

# r6 fixes the r5 final-link failure: micro-vorbis includes the portable
# microOggDemuxer as a nested CMake subproject under lib/, which PlatformIO's
# Arduino library builder did not compile. The bridge must compile exactly that
# bundled source tree into the firmware instead of pulling a second floating copy.
for token in (
    'lib" / "micro-ogg-demuxer" / "src"',
    'env.BuildSources(',
    'micro-ogg-demuxer',
    'rglob("*.cpp")',
    'OggDemuxer link dependency',
    'micro-ogg source bridge:',
):
    assert token in bridge, token

# Keep the previously proven private include and WLED script-chain fixes.
assert 'env.AppendUnique(CPPPATH=include_paths)' in bridge
assert 'src/tremor' in bridge
assert 'lib/micro-ogg-demuxer/include' in bridge
assert 'framework = espidf' not in override

print('dev.2n-r6 micro-ogg nested-source link bridge: PASS')

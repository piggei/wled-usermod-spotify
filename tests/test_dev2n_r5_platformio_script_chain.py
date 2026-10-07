#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
lib = json.loads((root / "library.json").read_text())
override = (root / "platformio_override.example.ini").read_text()
bridge = (root / "tools" / "platformio_micro_vorbis_compat.py").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()

assert lib["version"] == "0.1.0-dev.2n-vorbis-r7"
assert 'USERMOD_REVISION = "r7"' in ui
assert 'lib_compat_mode = off' in override
assert 'esphome/micro-vorbis@^0.1.0' in override
assert 'extra_scripts =\n  ${scripts_defaults.extra_scripts}\n  pre:../wled-usermod-spotify/tools/platformio_micro_vorbis_compat.py' in override
assert 'framework = espidf' not in override

# Never replace WLED's own script chain: load_usermods.py and peers are required
# to prepare the normal library/usermod build context (including wled.h visibility).
assert '${scripts_defaults.extra_scripts}' in override

# Bridge must still cover the previously observed micro-vorbis private headers.
for token in (
    'src/tremor',
    'lib/micro-ogg-demuxer/include',
    'lib/ogg/include',
    'lib/libogg/include',
    'env.AppendUnique(CPPPATH=include_paths)',
):
    assert token in bridge, token

# Known include paths are exported even on a clean build before the dependency
# directory has been materialized; recursive discovery is conditional only.
assert 'if not root.is_dir():\n        return []' not in bridge
assert 'if root.is_dir():' in bridge

print('dev.2n-r5 PlatformIO WLED-script-chain + private-include bridge: PASS')

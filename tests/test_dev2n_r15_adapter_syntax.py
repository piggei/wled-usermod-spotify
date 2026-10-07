#!/usr/bin/env python3
"""Host-only syntax smoke of the real new AP adapter + UI method against real
SessionProbe declarations. Stubs only replace Arduino/FreeRTOS/JSON; this is NOT
an ESP32 or full WLED build. The decoder and network are not executed here.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'spotify/SpotifySessionProbe.cpp').read_text()
methods=source[source.index('void SpotifySessionProbe::clearMetadataAudit('):source.index('void SpotifySessionProbe::begin()')]
ui=(root/'usermod_spotify_connect.h').read_text()
helper=ui[ui.index('  void appendMetadataAudit('):ui.index('  void appendKeyProbeAudit(')]
with tempfile.TemporaryDirectory(prefix='spotify-audit-adapter-') as tmp:
    p=Path(tmp);(p/'freertos').mkdir()
    (p/'Arduino.h').write_text('''#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define F(x) x
class String { public: String() {} template<class T> String(const T&) {} };
template<class T> String operator+(const String&, const T&) { return String(); }
struct EspStub { uint32_t getFreeHeap() const; uint32_t getMinFreeHeap() const; };
extern EspStub ESP;
uint32_t micros();
''')
    (p/'freertos/FreeRTOS.h').write_text('''#pragma once
using UBaseType_t=unsigned int;
using TaskHandle_t=void*;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
''')
    (p/'freertos/task.h').write_text('#include "FreeRTOS.h"\n')
    unit=p/'adapter.cpp'
    unit.write_text('#include <memory>\n#include <new>\n#include "spotify/SpotifySessionProbe.h"\n'+methods+
                   '\nstruct JsonArray { template<class T> void add(const T&) {} };\nclass UiSmoke {\nSpotifySessionProbe sessionProbe_;\n'+helper+'\n};\n')
    subprocess.run(shlex.split(os.environ.get('CXX','g++'))+['-std=c++11','-Wall','-Wextra','-Werror',
                   '-pedantic','-fsyntax-only','-I',str(p),'-I',str(root),str(unit)],check=True,timeout=30)
print('r15 AP adapter / snapshot UI host syntax PASS (stubbed platform, not target build)')

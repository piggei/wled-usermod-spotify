#!/usr/bin/env python3
"""Compile/execute the actual AP adapter and new HTTP/JSON helpers with host stubs.
This deliberately does NOT claim a WLED target build or Shannon network test.
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
helpers=ui[ui.index('  void appendMetadataAudit('):ui.index('public:\n  void setup() override')]
stubs=r'''
#include <map>
#include <algorithm>
#include <iostream>
int hostCriticalDepth=0;
uint32_t nowMs=0;
EspStub ESP;
uint32_t millis(){return nowMs;}
uint32_t micros(){return nowMs*1000u;}
struct JsonArray {
 std::string text;
 template<class T> void add(const T& value){text+=String(value).c_str();text+='\n';}
};
struct AsyncWebParameter { String text; const String& value() const {return text;} };
struct AsyncWebServerResponse {
 int status; std::string body; std::map<std::string,std::string> headers;
 void addHeader(const char* k,const char* v){headers[k]=v;}
};
struct AsyncWebServerRequest {
 std::map<std::string,AsyncWebParameter> post,query;
 std::map<std::string,std::string> headers;
 int status=0; std::string body;
 bool hasParam(const char* key,bool isPost=false) const {
  const auto& p=isPost?post:query;return p.find(key)!=p.end();
 }
 const AsyncWebParameter* getParam(const char* key,bool isPost=false) const {
  const auto& p=isPost?post:query;return &p.at(key);
 }
 void send(int code,const char*,const String& data){status=code;body=data.c_str();}
 AsyncWebServerResponse* beginResponse(int code,const char*,const String& data){
  auto* r=new AsyncWebServerResponse;r->status=code;r->body=data.c_str();return r;
 }
 void send(AsyncWebServerResponse* r){status=r->status;body=r->body;headers=r->headers;delete r;}
};
'''
with tempfile.TemporaryDirectory(prefix='spotify-key-adapter-') as tmp:
    p=Path(tmp); unit=p/'adapter.cpp'; binary=p/'adapter_test'
    unit.write_text('#include <vector>\n#include <memory>\n#include <new>\n#include <Arduino.h>\n'
                   '#ifdef __clang__\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored "-Wkeyword-macro"\n#endif\n'
                   '#define private public\n#include "spotify/SpotifySessionProbe.h"\n#undef private\n'
                   '#ifdef __clang__\n#pragma clang diagnostic pop\n#endif\n'+stubs+methods+
                   '\nclass UiSmoke { public: bool enabled_=true; SpotifySessionProbe sessionProbe_;\n'+helpers+
                   '\n};\n'+(root/'tests/host/audio_key_probe_adapter_test.inc').read_text())
    flags=['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic']
    if os.environ.get('SPOTIFY_PROBE_SANITIZERS')=='1':
        flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run(shlex.split(os.environ.get('CXX','g++'))+flags+[
        '-I',str(root/'tests/host/r16_stubs'),'-I',str(root),str(unit),
        str(root/'spotify/SpotifyMetadataAudit.cpp'),str(root/'spotify/SpotifyAudioKeyProbe.cpp'),
        str(root/'spotify/SpotifySessionKeyProbe.cpp'),str(root/'decoder/SpotifyApMediaChunkSource.cpp'),
        str(root/'decoder/SpotifyApContinuousRing.cpp'),'-o',str(binary)],check=True,timeout=60)
    subprocess.run([str(binary)],check=True,timeout=30)

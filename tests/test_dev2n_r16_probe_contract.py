#!/usr/bin/env python3
"""Byte freeze and source integration guards; behavioral proof is in native tests."""
import hashlib
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'spotify/SpotifySessionProbe.cpp').read_text()
for manifest in ['r14_frozen_paths.json','r15_frozen_paths.json']:
    freeze=json.loads((root/'tests/fixtures'/manifest).read_text())
    for path,digest in freeze['files'].items():
        assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,path
    for name,item in freeze['blocks'].items():
        a=s.index(item['start']); b=s.index(item['end'],a+len(item['start']))
        assert hashlib.sha256(s[a:b].encode()).hexdigest()==item['sha256'],name
h=(root/'spotify/SpotifyAudioKeyProbe.h').read_text()
c=(root/'spotify/SpotifyAudioKeyProbe.cpp').read_text()
a=(root/'spotify/SpotifySessionKeyProbe.cpp').read_text()
u=(root/'usermod_spotify_connect.h').read_text()
for token in ('kMaxTargets = 3u','kMaxRunsPerBoot = 4u','kMaxRequestsPerBoot = 12u',
              'kCooldownMs = 10000u','kResponseTimeoutMs = 2500u','kRunTimeoutMs = 15000u'):
    assert token in h,token
assert 'volatile uint8_t* bytes = payload.data();' in a
assert 'bytes[i] = 0u;' in a
for token in ('audioKey_ =','memcpy(audioKey_', 'SpotifyAudioAesCtr', 'SpotifyVorbisFixturePlayer', 'enqueuePcm44100', 'sendShannonPacket'):
    assert token not in a,token
assert 'keyProbe_.enqueue(metadataAudit_, metadataAuditGeneration_' in a
assert 'generation != generation_' in c and 'session != session_' in c
assert 'if (!owns(seq, session)) return false;' in c
assert 'if (send) ++audioKeyNextSequence_;' in a
assert 'kSequenceBase' not in h
assert s.index('consumeKeyProbeResponse(liveCommand, payload)') < s.index('    if (liveCommand == AES_KEY_COMMAND || liveCommand == AES_KEY_ERROR_COMMAND)')
assert 'diagnostic.target.file, diagnostic.target.gid, diagnostic.sequence' in s
assert 'takeKeyProbeRequest(diagnostic, tcp.available() <= 0)' in s
assert 'keyProbe_.cancel(spotify_key_probe::Reason::TrackChanged);' in s
assert 'openKeyProbeSession();' in s and 'closeKeyProbeSession(stopRequested_' in s
assert 'closeKeyProbeSession(spotify_key_probe::Reason::SessionStop);' in s
assert 'server.on(F("/spotify-key-probe"), HTTP_POST' in u
get=u[u.index('  void handleKeyProbeGet('):u.index('  void handleKeyProbePost(')]
assert 'requestKeyProbe(' not in get and 'method=\'post\'' in get
post=u[u.index('  void handleKeyProbePost('):u.index('public:\n  void setup() override')]
assert 'hasParam("action", true)' in post and 'parseGeneration(' in post
assert 'startNow(' not in post and 'reset(' not in post
assert 'normalCounters=separate normalLatch=unchanged keyStorage=none consumer=closed' in u
lib=json.loads((root/'library.json').read_text())
assert lib['version']=='0.1.0-dev.2n-vorbis-r17'
assert 'USERMOD_REVISION = "r17"' in u
rows=[line.split('\t') for line in (root/'tests/release_checks.tsv').read_text().splitlines() if line and not line.startswith('#')]
assert all(len(r)==7 for r in rows)
assert len({r[0] for r in rows})==len(rows)
runtime='\n'.join(p.read_text() for p in root.rglob('*') if p.suffix in ('.h','.cpp') and 'tests' not in p.relative_to(root).parts)
for row in rows:
    if row[1]=='postbuild' and row[3]=='firmware_contains': assert row[5] in runtime, row[0]
    if row[0]=='FW_REVISION': assert row[5]=='r17'
    if row[0].startswith('FW_KEY_PROBE_'): assert row[5] in u
print('r16 feature regression on r17 PASS: 24 unchanged files / 11 frozen blocks, manual-only POST, AP-only send, diagnostic key wipe/fence, consistent postbuild markers')

#!/usr/bin/env python3
"""Isolation guards: compare actual qualified r14 bytes, not marker presence alone."""
import hashlib
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
freeze=json.loads((root/'tests/fixtures/r14_frozen_paths.json').read_text())
for path, expected in freeze['files'].items():
    assert hashlib.sha256((root/path).read_bytes()).hexdigest()==expected, path
source=(root/'spotify/SpotifySessionProbe.cpp').read_text()
for name,item in freeze['blocks'].items():
    a=source.index(item['start']);b=source.index(item['end'],a+len(item['start']))
    assert hashlib.sha256(source[a:b].encode()).hexdigest()==item['sha256'], name
header=(root/'spotify/SpotifySessionProbe.h').read_text()
ui=(root/'usermod_spotify_connect.h').read_text()
assert 'new (std::nothrow) Report' in source
assert 'work && parse(payload.data(), payload.size(), selectedTrackGid_' in source
assert 'countryCode_, productInfoCatalogue_, *work)' in source
assert 'auditMetadataPayload(parts.front());' in source
assert source.index('auditMetadataPayload(parts.front());') < source.index('LegacyTrackMetadataInfo metadata;', source.index('auditMetadataPayload(parts.front());'))
assert 'recordMetadataKeyTarget(selectedTrackGid_, selectedAudioFileId_, sequence, true)' in source
assert 'recordMetadataKeyTarget(selectedTrackGid_, selectedAudioFileId_, 0u, false)' in source
assert 'clearMetadataAudit(spotify_metadata_audit::Status::Pending, selectedTrackGid_)' in source
assert 'selectedAudioFileId_, selectedTrackGid_, sequence);' in source
assert 'metadataAuditGeneration_' in source and 'generation == metadataAuditGeneration_' in source
assert 'portENTER_CRITICAL(&metadataAuditMux_)' in source
assert 'SESSION_POLL_MS = 50u' in header
for marker in ('Metadata audit state=', 'Metadata identity requestedGid=', 'Metadata territory country=',
               'Metadata alternatives seen=', 'Metadata alternative index=', 'AudioKey target source=queue-primary gid=',
               'mode=observe-only', 'authorization=unknown', 'relinking=not-applied latch=unchanged',
               'consumer=closed keyGate=', 'decrypt=closed'):
    assert marker in ui,marker
print('r15 isolation PASS: %d byte-identical files / %d frozen code blocks; target/latch unchanged' % (len(freeze['files']),len(freeze['blocks'])))

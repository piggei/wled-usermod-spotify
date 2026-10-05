from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
lib = json.loads((root / "library.json").read_text())
header = (root / "usermod_spotify_connect.h").read_text()
out_h = (root / "audio" / "WavesharePcmOutput.h").read_text()
out_cpp = (root / "audio" / "WavesharePcmOutput.cpp").read_text()
cfg = (root / "audio" / "SpotifyAudioConfig.h").read_text()
pcm_h = (root / "spotify" / "SpotifyPcmTestSource.h").read_text()
pcm_cpp = (root / "spotify" / "SpotifyPcmTestSource.cpp").read_text()

assert lib["name"] == "wled-usermod-spotify"
assert lib["version"] == "0.1.0-dev.2c-pcm-ingress-r1"
assert lib["build"]["libArchive"] is False
assert 'USERMOD_VERSION = "0.1.0-dev.2c-pcm-ingress"' in header
assert 'USERMOD_REVISION = "r1"' in header
assert "toneTestLoop" not in header
assert "playTestTone" not in header
assert "audio_.startTestTone" in header
assert "audio_.stopTestTone" in header
assert 'action == "start-pcm"' in header
assert "pcmTest_.start(audio_, hz)" in header
assert "r.type='range'" in header and "r.min='0'" in header and "r.max='100'" in header
assert "SHARED_BITS_PER_SAMPLE = 32" in cfg
assert "STANDALONE_BITS_PER_SAMPLE = 16" in cfg
assert "streamBitsPerSample_ == 32u" in out_cpp
assert "static_cast<int32_t>(source16[i]) * 65536" in out_cpp
assert "xTaskCreate(taskThunk" in out_cpp
assert "xTaskCreatePinnedToCore" not in out_cpp
assert "SpotifyAudioConfig::DMA_BUFFER_COUNT" in out_cpp
assert "SpotifyAudioConfig::DMA_FRAMES" in out_cpp
assert "testToneActive_" in out_h
assert "enqueuePcm44100" in out_h and "enqueuePcm44100" in out_cpp
assert "streamSampleRate_ == SpotifyAudioConfig::SAMPLE_RATE" in out_cpp
assert "streamSampleRate_ != SpotifyAudioConfig::SHARED_SAMPLE_RATE" in out_cpp
assert "downsampleHavePending_" in out_h
assert "pcm44100InFrames" in out_h and "pcmStreamOutFrames" in out_h
assert "SpotifyPcmTestSource" in pcm_h
assert "SOURCE_RATE = SpotifyAudioConfig::SAMPLE_RATE" in pcm_cpp
assert "output_->enqueuePcm44100" in pcm_cpp
assert "SOURCE_FRAMES_PER_BLOCK = 512u" in pcm_cpp
# r2 cold-boot volume + single-slider UI regressions retained.
assert "volumePending_ = true" in out_cpp
assert "err == ESP_OK && written == bytesToWrite && volumePending_" in out_cpp
assert "spotifyVolRange" in header
assert "a.forEach(e=>e.style.display='none')" in header
assert "e.type='range'" not in header
print("static checks: PASS")

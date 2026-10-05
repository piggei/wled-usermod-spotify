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
assert lib["version"] == "0.1.0-dev.2f-mercury-session-r1"
assert not lib.get("dependencies")
assert lib["build"]["libArchive"] is False
assert 'USERMOD_VERSION = "0.1.0-dev.2f-mercury-session"' in header
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
login_h = (root / "spotify" / "SpotifyLoginBlob.h").read_text()
login_cpp = (root / "spotify" / "SpotifyLoginBlob.cpp").read_text()
zc_h = (root / "spotify" / "SpotifyZeroConfProbe.h").read_text()
zc_cpp = (root / "spotify" / "SpotifyZeroConfProbe.cpp").read_text()
assert "decodeAndStore" in login_h and "SpotifyLoginBlob::decodeAndStore" in login_cpp
assert "DH_PRIME_HEX" in login_cpp and "mbedtls_mpi_exp_mod" in login_cpp
assert "mbedtls_aes_crypt_ctr" in login_cpp
aes192_h = (root / "spotify" / "Aes192Software.h").read_text()
aes192_cpp = (root / "spotify" / "Aes192Software.cpp").read_text()
assert "aes192DecryptEcbInPlace" in login_cpp and "aes192DecryptEcbInPlace" in aes192_h
assert "kRounds = 12u" in aes192_cpp and "kKeyBytes = 24u" in aes192_cpp
assert "mbedtls_aes_setkey_dec(&aes, aesKey, 192u)" not in login_cpp
assert "pbkdf2Sha1" in login_cpp and "MacMismatch" in login_h
assert "spotify_auth.bin" in login_cpp
assert 'action != "addUser"' in zc_cpp
assert 'action == "resetUsers"' in zc_cpp
assert "ERROR-INVALID-PUBLICKEY" in zc_cpp
assert "acceptedAddUserRequests" in zc_h
assert "diagnosticStageName" in login_h and "stageName(Stage" in login_cpp
assert "addUser params user=" in header and "LoginBlob stage=" in header
assert "decoded blob=" in header and "credentialUserBytes=" in header
assert "lastAddUserUserPresent_" in zc_h and 'request->hasParam("userName", true)' in zc_cpp
assert "mbedtls_sha1_starts(&ctx)" in login_cpp
assert "normalizedKey[i] ^ 0x36u" in login_cpp and "normalizedKey[i] ^ 0x5cu" in login_cpp
assert "mbedtls_md_info_from_type" not in login_cpp
assert "mbedtls_md_hmac" not in login_cpp
assert "mbedtls_sha1_ret" not in login_cpp
assert "mbedtls_sha1(data" not in login_cpp
assert login_cpp.count("++persistSuccesses_;") == 1


# dev.2e-r2 AP handshake + Shannon stored-credential authentication gate
sess_h = (root / "spotify" / "SpotifySessionProbe.h").read_text()
sess_cpp = (root / "spotify" / "SpotifySessionProbe.cpp").read_text()
shan_h = (root / "spotify" / "SpotifyShannon.h").read_text()
shan_cpp = (root / "spotify" / "SpotifyShannon.cpp").read_text()
assert "SpotifySessionProbe sessionProbe_" in header
assert 'server.on(F("/spotify-session")' in header
assert 'action == "probe"' in header and 'action == "stop"' in header and 'action == "reset"' in header
assert "Spotify persistent Shannon/Mercury gate" in header
assert "scope=persistent Shannon + PING/PONG + country + Mercury remote-user SUB; SPIRC/metadata/playback next gate" in header
assert "xTaskCreate(taskThunk" in sess_cpp
assert "xTaskCreatePinnedToCore" not in sess_cpp
assert "http://apresolve.spotify.com/?type=accesspoint" in sess_cpp
assert 'KEY_CURRENT = "accesspoint"' in sess_cpp
assert 'KEY_LEGACY = "ap_list"' in sess_cpp
assert 'AP_FALLBACK = "ap.spotify.com:443"' in sess_cpp
assert "tcp.connect(host.c_str(), port, CONNECT_TIMEOUT_MS)" in sess_cpp
assert "buildClientHello" in sess_cpp and "parseApDhPublicKey" in sess_cpp
assert "LOGIN_REQUEST_COMMAND = 0xABu" in sess_cpp
assert "AUTH_SUCCESSFUL_COMMAND = 0xACu" in sess_cpp
assert "AUTH_DECLINED_COMMAND = 0xADu" in sess_cpp
assert "resultData.begin() + 20" in sess_cpp
assert "resultData.begin() + 52" in sess_cpp
assert "resultData.begin() + 84" in sess_cpp
assert "sendShannonPacket" in sess_cpp and "recvShannonPacket" in sess_cpp
assert "state_ = State::Authenticated" in sess_cpp
assert "zeroConf_.userName()" in header and "zeroConf_.authData()" in header
assert "const String& userName() const" in zc_h
assert "const std::vector<uint8_t>& authData() const" in zc_h
assert "INITIAL_CONSTANT = 0x6996c53a" in shan_h
assert "void SpotifyShannon::encrypt" in shan_cpp
assert "void SpotifyShannon::decrypt" in shan_cpp
assert "void SpotifyShannon::finish" in shan_cpp
assert 'NetworkClientSecure' not in sess_cpp
assert 'WiFiClientSecure' not in sess_cpp
assert 'resolveWithHttps' not in sess_cpp

assert "MERCURY_SUB_COMMAND = 0xB3u" in sess_cpp
assert "PONG_COMMAND = 0x49u" in sess_cpp
assert "persistSkips_" in login_cpp
print("static checks: PASS")

# Third-party/provenance guard: do not regress to the earlier incorrect cspot-MIT assumption.
notices = (root / "THIRD_PARTY_NOTICES.md").read_text()
assert "GPL-3.0-or-later" in notices
assert "independent C++ implementation" in notices
assert "cspot" in notices
assert "cspot/bell" not in notices or "MIT" not in notices.split("cspot/bell", 1)[-1][:120]

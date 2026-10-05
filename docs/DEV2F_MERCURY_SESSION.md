# dev.2f-r1 persistent Shannon / Mercury gate

## Scope

This gate extends the hardware-qualified dev.2e-r2 AP authentication baseline without changing the audio path.
After APWelcome (`0xAC`) the AP socket and Shannon ciphers remain alive instead of being closed immediately.

The task now:

1. keeps the authenticated AP connection open;
2. receives and authenticates Shannon packets continuously;
3. answers Spotify `PING` (`0x04`) with `PONG` (`0x49`) carrying the same payload;
4. captures the two-byte country response (`0x1B`);
5. sends one Mercury `SUB` (`0xB3`) for `hm://remote/3/user/<username>/`;
6. parses the Mercury sequence envelope/header sufficiently to match the SUB response and count pushed `0xB5` events;
7. attempts a bounded automatic reconnect if the AP closes the session or Wi-Fi is lost/recovered;
8. skips LittleFS rewrites when a repeated Zeroconf `addUser` decodes to the credential already cached.

No SPIRC frame generation/parsing, metadata lookup, audio-key retrieval, CDN access, codec decode, or Spotify PCM playback is included yet.

## Protocol grounding

The Mercury envelope and request command values follow public Spotify Connect interoperability implementations used as references: `SUB=0xB3`, `SUBRES=0xB5`, `SEND=0xB2`, with a Mercury header containing URI field 1 and method field 3. Observed embedded cspot traces respond to AP `PING=0x04` with command `0x49` and the unchanged timestamp payload; this gate waits for that first PING/PONG before sending the remote-user SUB. No cspot source is copied into this module.

## Qualification targets

A healthy `/json/info` should eventually show values similar to:

```
state=session-active
Shannon ... macFail=0
live starts=1 uptime=... rx>0 tx>0
keepalive ping>=1 pong>=1 country=XX
Mercury SUB attempts=1 ok=1 responses>=1
lastError=none
```

Leave the device online for at least 130 seconds so the persistent-session/keepalive gate is meaningful. Then select the device once in the Spotify app and confirm that WLED remains responsive and that no I2S regression appears.

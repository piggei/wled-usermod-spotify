# Next milestone after dev.2d-r3

Do not change the qualified I2S/ES8311/DMA/PCM ingress runtime unless a regression demonstrates a need.

After the dev.2d LoginBlob gate passes on the official Spotify app, the next build should consume the cached credential and introduce only the cspot network/session bootstrap:

1. AP resolve;
2. PlainConnection;
3. APHello / DH challenge;
4. Shannon keys;
5. stored-credential authorization;
6. ping/time sync and minimum Mercury/SPIRC session establishment.

Keep decoded audio disabled in that first session build. Once session/auth is independently qualified, connect cspot's 44.1 kHz / 16-bit stereo `feedPCMFrames` callback directly to the already-qualified `enqueuePcm44100()` ingress.

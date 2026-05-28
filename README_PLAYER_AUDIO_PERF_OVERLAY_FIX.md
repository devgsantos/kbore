# Player audio/performance/overlay fix

Changes:

1. Audio
   - Adds basic FFmpeg audio stream detection.
   - Decodes audio and queues PCM S16 stereo 48 kHz to SDL_Audio.
   - If audio initialization fails, video still plays.

2. Playback performance
   - Uses `SWS_FAST_BILINEAR`.
   - Downscales decoded video to screen size before rendering.
   - Consumes multiple packets per UI frame to reduce slow-motion playback.
   - Enables FFmpeg low-latency options and codec fast flags.

3. Loading state
   - Replaces Portuguese/incorrect "Nenhum frame disponivel" during startup with "Loading...".
   - Error text is now only shown when playback fails to open.

4. Overlay
   - Top and bottom video overlays hide automatically after about 5 seconds once video frames start rendering.
   - Pressing A/B or pausing shows the overlay again.
   - Overlay text changed to English.

Notes:
- Audio requires FFmpeg + SDL audio support in the Switch portlibs.
- This is software decoding. Some high bitrate 1080p streams may still be heavy on Switch CPU.

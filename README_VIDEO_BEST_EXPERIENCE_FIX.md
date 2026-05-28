# NSTV video playback best-experience patch

This package focuses on the video player experience:

1. Audio
   - SDL is now initialized with SDL_INIT_AUDIO.
   - The player explicitly initializes the SDL audio subsystem before opening the audio device.
   - Audio packets are decoded and queued for a longer packet window so video packets do not starve the audio queue.

2. Playback performance
   - Removed software renderer fallback on Switch. Switch now requests SDL_RENDERER_ACCELERATED only.
   - Video frame textures are now reused and updated with SDL_UpdateTexture instead of recreating/destroying SDL textures every frame.
   - Video is scaled down before upload/render to reduce CPU/GPU pressure.
   - FFmpeg uses fast decode flags, multi-threading and SWS_FAST_BILINEAR.

3. Loading/error UX
   - Startup shows "Loading..." while the first frame is being decoded.
   - Error text is only shown when playback fails to open or the player is not open.

4. Overlay
   - Overlay hide is now time-based using steady_clock instead of frame-count based.
   - Overlay hides 5 seconds after the first video frame appears.
   - Any input, pause, or playback error shows the overlay again.

Notes:
- This is still software video decoding through FFmpeg. The patch removes the SDL software renderer path and avoids per-frame texture creation, but CPU decode of heavy 1080p streams can still be expensive.
- For best Switch performance, prefer 720p or lower bitrate streams, or add a server/proxy transcode profile later.

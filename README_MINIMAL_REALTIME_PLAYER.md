# NSTV minimal realtime player

This package replaces the previous RGBA video path with a lighter realtime path:

- FFmpeg still performs software decoding, but the renderer no longer uploads RGBA frames.
- Video is converted/downscaled to YUV420P at up to 960x540.
- SDL renders the frame with `SDL_UpdateYUVTexture`.
- This reduces CPU memory bandwidth and avoids the RGBA texture path.
- The player remains inside NSTV; no second homebrew is opened.
- Audio remains FFmpeg + swresample + SDL_QueueAudio.

Why this helps:

Old path:
`decode -> swscale RGBA -> copy RGBA -> SDL texture`

New path:
`decode -> swscale YUV420P 960x540 -> SDL YUV texture`

This is not hardware decoding, but it is the best practical minimal player improvement without SwitchWave/mpv/deko3d.

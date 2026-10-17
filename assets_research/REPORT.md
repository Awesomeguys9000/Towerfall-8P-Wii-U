# TowerFall content research (summary)

## Music (XACT): Content/Music/Win/{TowerFallMusic.xgs, MusicSoundBank.xsb, MusicWaveBank.xwb}
- 45 waves, all MS-ADPCM stereo, block align 524 (xwb stores raw+22 times channels), 512 samples/block, standard 7 coefs. In-memory bank (not streaming), no wave name table, little-endian.
- Sample rates 44089..44122 Hz (not exactly 44100). Every wave loops whole length (loop start 0).
- Music category (.xgs): instance limit 1, replace oldest, crossfade fade-in 1000ms / fade-out 1000ms, volume +2.99 dB. Every sound: volume -12.09 dB. Overall gain ~0.702 x Music.MasterVolume (FNA multiplies category volume by SetVolume(2*master)).
- Music.Play(name): ignored if already CurrentSong; else plays cue without stopping old -> category limit 1 => 1s crossfade. PlayImmediate: hard stop then play. Stop(): AsAuthored -> 1s fade out. Pause halves volume.
- Cues: 32 normal cues (one wave, loops forever, except 5 Unlock* cues and WhiteArcherReveal which play once).
  13 Victory cues (Victory<Name>): track0 = jingle once at t=0; track1 at fixed delay = random (no immediate repeat) of TheArchives(wave 7)/TheTale(wave 43), looping. Delays ms: Team 11333, White 10000, Pink 10833, Orange 9833, Green 12500, Blue 10833, Yellow 10333, Purple 9800, Cyan 9800, Red 13000, Kyle 6400, OrangeAlt 10399, WhiteAlt 10000.
- Parser reference: extract_music.py (XACT v46), music_manifest.json has full cue->wave mapping.

## Shaders
LightingEffect (ps_2_0): s0 SourceSampler, s1 LightSampler, c0.x Alpha.
  out.rgb = S.rgb - Alpha * L.a * L.rgb ; out.a = S.a  (clamped to [0,1] by RGBA8 target). L.a is always 1.
  Level.CoreRender: light canvas L (420x240) cleared to DarknessColor.Invert() a=255, lights drawn with premult AlphaBlend.
  Foreground target F cleared transparent, layers drawn into it. Background drawn directly to screen (never darkened).
  Lighting pass: Textures[1] = L; F drawn at (0,0) with AlphaBlend + LightingEffect, k = Alpha param.
  SDL emulation: DARKEN = ComposeCustomBlendMode(SRC_ALPHA, ONE, REV_SUBTRACT(dst-src), ZERO, ONE, ADD);
     set target F; draw L with DARKEN, alphaMod = k*255  => F.rgb = max(0, F.rgb - k*L.rgb); then draw F to screen with PREMUL.
  Verify REV_SUBTRACT direction with a 1-pixel readback (SDL headers comments contradict).
ScreenEffects (ps_2_0): c0.x Counter, c1.x Amplitude, c2.x Frequency.
  x = fmod(uv.x + Amplitude*sin(uv.y*Frequency + Counter) + 1, 1); out = tex2D(s0, (x, uv.y)).
  Applied in Screen.Render (Opaque, point) when SineAmplitude != 0. Emulate per output row j of dest rect D:
  v=(j+0.5)/Hd; s=A*sin(v*F+C); src row floor(v*240); draw 420x1 row at (x0 - s*Wd, y0+j, Wd, 1) plus wrap copy at +-Wd, clipped to D. Batch with SDL_RenderGeometry.

## Fonts (XNB SpriteFont, uncompressed, flag byte 0x00, DXT3 texture, premultiplied white)
- Archer (TFGame.Font) 64x64, 95 glyphs (32..126), LineSpacing 5, Spacing 0, crop height 6.
- OpenSans 128x128, LineSpacing 22. MonocleDefault 128x128, LineSpacing 22. No DefaultCharacter (substitute/skip).
- JSON reference decoder: xnb_font.py. rect = glyph source rect, cropping = (x,y,w,h), kerning = (A,B,C).
- MeasureString: w=0,maxW=0,h=0,lineH=LineSpacing,first=true; skip '\r'; '\n': maxW=max(maxW,w), h+=LineSpacing, w=0, first=true;
  per char: (A,B,C)=kerning; w += first ? abs(A) : Spacing + A; first=false; w += B + C; lineH=max(lineH, crop.h). return (max(maxW,w), h+lineH).
- DrawString: same walk; glyph local=(x+crop.x, y+crop.y) [x already advanced by A as above]; top-left = pos + Rotate(rot)*((local-origin)*scale); x += B + C.

## Other formats
- Atlases: TexturePacker xml (name,x,y,width,height). PNGs up to 2048x4096; some are palette PNGs (indexed). RGBA atlases are ALREADY premultiplied -> do not premultiply, use premult blending.
- XML files: UTF-8 BOM mostly, CRLF line endings.
- Levels .oel (Ogmo XML). Directory enumeration must be sorted by filename (Windows order).
- SFX: 713 wav, PCM16 LE 44100 (mono & stereo). Chunks: 'bext' often before 'fmt ', odd chunks padded: scan chunks properly.
- All binary formats little-endian: byte-swap on Wii U (big-endian).

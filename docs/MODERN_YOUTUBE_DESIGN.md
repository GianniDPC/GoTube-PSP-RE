# Standalone modern YouTube design

## Requirement

The application must run completely on the PSP. A companion PC, phone,
transcoding proxy, public Invidious/Piped instance, or authenticated YouTube
session is not part of the runtime architecture.

## SmartTube findings

SmartTube remains useful as a description of current YouTube behavior, but is
not directly portable. Its media-service code uses Android/Java networking,
modern JavaScript execution for player and `n` challenges, PO-token providers,
adaptive DASH/SABR streams, and ExoPlayer. Those dependencies exceed both the
software environment and practical memory/CPU budget of PSP homebrew.

The useful portable idea is Innertube client selection. Current yt-dlp also
identifies `ANDROID_VR` 1.65.10 as its default JS-less client. Live requests
made during development confirmed that it can return itag 18 without login,
signature deciphering, or a PO token after an anonymous visitor-data handshake.

## PSP architecture

1. Existing GoTube network selection and provider UI remain unchanged.
2. A native `YouTube` descriptor replaces the dead historical YouTube.js
   descriptor. Other reconstructed providers remain preserved.
3. PSP libcurl 7.64.1 plus mbedTLS 2.28.10 handles TLS 1.2. Only the Google
   Trust Services roots needed by YouTube, ytimg and googlevideo are packaged.
4. Search and continuation responses have a hard 512 KiB allocation cap.
   The parser extracts at most ten compact video records per page.
5. Player lookup requests the anonymous Android VR client and accepts only
   progressive itag 18 (MP4, H.264 Baseline, AAC-LC, normally 640x360).
6. A 256 KiB producer/consumer ring connects libcurl to the recovered FFmpeg
   custom IO path. Current itag-18 files place `moov` before `mdat`, so the old
   MP4 demuxer can initialize without seeking or downloading the full file.
7. HTTPS thumbnails and Save use the same verified transport.

## Resource bounds

- Search/player response: 512 KiB maximum, freed after parsing.
- Video network ring: 256 KiB.
- TLS/HTTP worker stack: 128 KiB.
- Result set: ten UI records per page.
- No adaptive-track merge, JavaScript VM for current player code, video
  transcode, or full-media RAM buffer.

The linked PRX is approximately 4.4 MiB. libcurl/mbedTLS increases it by about
1.1 MiB compared with the preservation build while dynamic working buffers
remain bounded as listed above.

## Verified off-device

- Anonymous current Innertube search returned nineteen compact video records
  and a continuation token.
- The continuation request returned the next result page.
- Android VR player lookup returned itag 18 for two current test videos.
- The selected stream was 640x360 H.264 Baseline plus AAC-LC, with `moov` at
  byte 28 and media data later in the file.
- Native parser host harness extracted ten results, pagination state, metadata,
  visitor data, and the full signed stream URL from captured live responses.
- The packaged CA roots validate current YouTube and googlevideo certificate
  chains.
- A clean PSP cross-build and installable package complete successfully.

## Known compatibility boundary

This route intentionally favors something the PSP can decode over broad format
coverage. A video is unsupported if Android VR does not expose progressive
itag 18. This commonly includes live, DRM, account/age restricted, some
made-for-kids, and adaptive-only media. Supporting those would require either
credentials, a changing JavaScript/PO-token implementation, merging tracks,
or transcoding—none is a sound default on PSP hardware.

YouTube is a private, changing service. Client identifiers and response-field
handling are isolated in `src/media/modern.c`; TLS transport is isolated in
`src/net/curl_http.c` so updates do not disturb the recovered interface.

## Hardware qualification

The first real-PSP qualification should be performed as a single flow:

1. Boot and connect through the normal PSP network dialog.
2. Press Select until `YouTube` is active.
3. Search, load a thumbnail, and move to page two with R.
4. Play a short ordinary prerecorded video with itag 18.
5. Verify video, AAC audio, pause, overlays, size mode, return-to-list, and Save.

If it fails, the failure boundary should be logged at TLS connect, HTTP status,
JSON parse, format selection, MP4 demux, first decoded video frame, or first
audio frame. Those are stable diagnostic stages; arbitrary renderer changes
are not part of this work.

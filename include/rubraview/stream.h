#ifndef RUBRAVIEW_STREAM_H
#define RUBRAVIEW_STREAM_H

/*
 * RV-157 (owner, 2026-10-22: "인터넷 라디오 주소를 가지고 재생목록을 만들어서 라디오들을 듣거나 선택할 수
 * 있게"): a station's address in a playlist. Which addresses count, what FFmpeg is asked to open for one
 * (`mms://` has two forms there), and the name shown for one that has no title of its own.
 */

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest address taken, NUL included. */
#define RUBRAVIEW_STREAM_URL_MAX 2048u

/** The only protocols FFmpeg may use for a station: never `file`, `pipe`, `concat` or `subfile`, which an
    address in a list made elsewhere could otherwise reach through a redirect or a stream's own playlist. */
#define RUBRAVIEW_STREAM_PROTOCOLS "http,https,tcp,tls,crypto,mmsh,mmst"

/** An address of a station: `http`, `https`, `mms`, `mmsh` or `mmst`, `://`, a host, no control characters,
    shorter than RUBRAVIEW_STREAM_URL_MAX. Upper or lower case scheme. */
bool rubraview_stream_is_address(u8str_t text);

/** What FFmpeg is asked to open, in order, NUL-terminated: `mms://` as `mmsh://` (MMS over HTTP) and then
    `mmst://` (over TCP); any other address as it is, once. 0 when `address` is not one. */
size_t rubraview_stream_ffmpeg_urls(u8str_t address, char out[2][RUBRAVIEW_STREAM_URL_MAX]);

/** The name shown for an address with no title: host and path, the scheme, a user name and password, the
    query and a trailing `/` left out. A view into `address`. */
u8str_t rubraview_stream_display_name(u8str_t address);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_STREAM_H */

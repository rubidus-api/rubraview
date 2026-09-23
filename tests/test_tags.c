/* What a music file says about itself (owner, 2026-09-24, RV-076). Five
   formats keep it in five places, and each has a detail that a first
   reading gets wrong — the sizes that are seven bits to the byte, the
   0xFF 0x00 pairs put in to hide an 0xFF, text that is UTF-16 with a
   mark, the four bytes of version that only `meta` has, a cover base64'd
   into a text comment. Every one of those is in a fixture here. */
#include "rubraview/tags.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[1 << 20];

static bool is(u8str_t s, const char *text) {
    size_t n = strlen(text);
    return s.len == n && (n == 0 || memcmp(s.ptr, text, n) == 0);
}

static void put(unsigned char *out, size_t *at, const void *bytes, size_t len) {
    memcpy(out + *at, bytes, len);
    *at += len;
}
static void put32be(unsigned char *out, size_t *at, uint32_t v) {
    out[(*at)++] = (unsigned char)(v >> 24); out[(*at)++] = (unsigned char)(v >> 16);
    out[(*at)++] = (unsigned char)(v >> 8);  out[(*at)++] = (unsigned char)v;
}
static void put32le(unsigned char *out, size_t *at, uint32_t v) {
    out[(*at)++] = (unsigned char)v;         out[(*at)++] = (unsigned char)(v >> 8);
    out[(*at)++] = (unsigned char)(v >> 16); out[(*at)++] = (unsigned char)(v >> 24);
}
static void put_syncsafe(unsigned char *out, size_t *at, uint32_t v) {
    out[(*at)++] = (unsigned char)((v >> 21) & 0x7F); out[(*at)++] = (unsigned char)((v >> 14) & 0x7F);
    out[(*at)++] = (unsigned char)((v >> 7) & 0x7F);  out[(*at)++] = (unsigned char)(v & 0x7F);
}

/* A picture the reader can find again by its first bytes. */
static const unsigned char JPEG[] = { 0xFF, 0xD8, 0xFF, 0xE0, 'J', 'F', 'I', 'F', 0x11, 0x22, 0x33 };

/* ---- ID3v2.3: plain sizes, a UTF-16 title, a cover ---- */
static size_t build_id3v23(unsigned char *out, bool unsynchronised) {
    unsigned char body[512];
    size_t at = 0;

    /* TIT2, UTF-16 with a byte-order mark: "Song". */
    static const unsigned char TITLE[] = { 0x01, 0xFF, 0xFE, 'S', 0, 'o', 0, 'n', 0, 'g', 0 };
    put(body, &at, "TIT2", 4); put32be(body, &at, (uint32_t)sizeof(TITLE));
    body[at++] = 0; body[at++] = 0;
    put(body, &at, TITLE, sizeof(TITLE));

    /* TPE1, Latin-1. */
    static const unsigned char ARTIST[] = { 0x00, 'B', 'a', 'n', 'd' };
    put(body, &at, "TPE1", 4); put32be(body, &at, (uint32_t)sizeof(ARTIST));
    body[at++] = 0; body[at++] = 0;
    put(body, &at, ARTIST, sizeof(ARTIST));

    /* APIC: the encoding, the mime, the kind, a description, the picture. */
    unsigned char apic[128];
    size_t p = 0;
    apic[p++] = 0x00;
    put(apic, &p, "image/jpeg", 11);     /* with its NUL */
    apic[p++] = 0x03;                    /* front cover */
    apic[p++] = 0x00;                    /* an empty description */
    put(apic, &p, JPEG, sizeof(JPEG));
    put(body, &at, "APIC", 4); put32be(body, &at, (uint32_t)p);
    body[at++] = 0; body[at++] = 0;
    put(body, &at, apic, p);

    /* The whole tag, with every 0xFF given a 0x00 when the flag says so. */
    unsigned char kept[1024];
    size_t kept_len = 0;
    if (unsynchronised) {
        for (size_t i = 0; i < at; ++i) {
            kept[kept_len++] = body[i];
            if (body[i] == 0xFF) kept[kept_len++] = 0x00;
        }
    } else {
        memcpy(kept, body, at);
        kept_len = at;
    }

    size_t o = 0;
    put(out, &o, "ID3", 3);
    out[o++] = 3; out[o++] = 0;                       /* 2.3.0 */
    out[o++] = unsynchronised ? 0x80 : 0x00;
    put_syncsafe(out, &o, (uint32_t)kept_len);
    put(out, &o, kept, kept_len);
    /* An MPEG frame after the tag, so the codec and rate are read too:
       MPEG-1 layer III, 128 kbps, 44100 Hz, stereo. */
    out[o++] = 0xFF; out[o++] = 0xFB; out[o++] = 0x90; out[o++] = 0x00;
    for (int i = 0; i < 64; ++i) out[o++] = 0x00;
    return o;
}

/* ---- ID3v2.4: the frame sizes are seven bits to the byte as well ---- */
static size_t build_id3v24(unsigned char *out) {
    unsigned char body[256];
    size_t at = 0;
    static const unsigned char TITLE[] = { 0x03, 'H', 'i' };        /* UTF-8 */
    put(body, &at, "TIT2", 4); put_syncsafe(body, &at, (uint32_t)sizeof(TITLE));
    body[at++] = 0; body[at++] = 0;
    put(body, &at, TITLE, sizeof(TITLE));
    /* A size with a byte over 127 in it, which only the syncsafe reading
       gets right: 200 bytes of album name. */
    unsigned char album[201];
    album[0] = 0x00;
    memset(album + 1, 'A', 200 - 1);
    put(body, &at, "TALB", 4); put_syncsafe(body, &at, 200);
    body[at++] = 0; body[at++] = 0;
    put(body, &at, album, 200);

    static const unsigned char YEAR[] = { 0x00, '1', '9', '9', '9' };
    put(body, &at, "TDRC", 4); put_syncsafe(body, &at, (uint32_t)sizeof(YEAR));
    body[at++] = 0; body[at++] = 0;
    put(body, &at, YEAR, sizeof(YEAR));

    size_t o = 0;
    put(out, &o, "ID3", 3);
    out[o++] = 4; out[o++] = 0;
    out[o++] = 0x00;
    put_syncsafe(out, &o, (uint32_t)at);
    put(out, &o, body, at);
    out[o++] = 0xFF; out[o++] = 0xFB; out[o++] = 0x90; out[o++] = 0x00;
    for (int i = 0; i < 32; ++i) out[o++] = 0x00;
    return o;
}

/* ---- FLAC: the stream's shape, the comments, the cover ---- */
static size_t build_flac(unsigned char *out) {
    size_t o = 0;
    put(out, &o, "fLaC", 4);

    /* STREAMINFO: 44100 Hz, two channels, sixteen bits, 88200 samples. */
    unsigned char info[34];
    memset(info, 0, sizeof(info));
    info[10] = (unsigned char)(44100 >> 12);
    info[11] = (unsigned char)((44100 >> 4) & 0xFF);
    info[12] = (unsigned char)(((44100 & 0x0F) << 4) | (1 << 1) | 0);   /* rate, 2 channels, bits high */
    info[13] = (unsigned char)(((16 - 1) & 0x0F) << 4);                 /* bits low, samples high */
    info[14] = 0x00; info[15] = 0x01; info[16] = 0x58; info[17] = 0x88; /* 88200 samples */
    out[o++] = 0x00; out[o++] = 0; out[o++] = 0; out[o++] = 34;
    put(out, &o, info, sizeof(info));

    /* VORBIS_COMMENT. */
    unsigned char comment[256];
    size_t c = 0;
    put32le(comment, &c, 4); put(comment, &c, "test", 4);     /* the vendor */
    put32le(comment, &c, 3);
    put32le(comment, &c, 11); put(comment, &c, "TITLE=Waltz", 11);
    put32le(comment, &c, 13); put(comment, &c, "artist=Chopin", 13);   /* the key's case is nothing */
    put32le(comment, &c, 13); put(comment, &c, "TRACKNUMBER=7", 13);
    out[o++] = 0x04; out[o++] = 0; out[o++] = (unsigned char)(c >> 8); out[o++] = (unsigned char)(c & 0xFF);
    put(out, &o, comment, c);

    /* PICTURE. */
    unsigned char pic[128];
    size_t p = 0;
    put32be(pic, &p, 3);                       /* front cover */
    put32be(pic, &p, 10); put(pic, &p, "image/jpeg", 10);
    put32be(pic, &p, 0);                       /* no description */
    put32be(pic, &p, 16); put32be(pic, &p, 16); put32be(pic, &p, 24); put32be(pic, &p, 0);
    put32be(pic, &p, (uint32_t)sizeof(JPEG));
    put(pic, &p, JPEG, sizeof(JPEG));
    out[o++] = 0x86; out[o++] = 0; out[o++] = (unsigned char)(p >> 8); out[o++] = (unsigned char)(p & 0xFF);
    put(out, &o, pic, p);
    return o;
}

/* ---- MP4: atoms inside atoms, and `meta` has four bytes nothing else has ---- */
static size_t atom(unsigned char *out, size_t at, const char *name, const unsigned char *body, size_t len) {
    size_t o = at;
    put32be(out, &o, (uint32_t)(len + 8));
    put(out, &o, name, 4);
    put(out, &o, body, len);
    return o;
}

static size_t build_mp4(unsigned char *out) {
    /* ilst: one name, one artist, one cover. */
    unsigned char ilst[256];
    size_t i = 0;
    {
        unsigned char data[64];
        size_t d = 0;
        put32be(data, &d, 16 + 4); put(data, &d, "data", 4);
        put32be(data, &d, 1); put32be(data, &d, 0);
        put(data, &d, "Rain", 4);
        i = atom(ilst, i, "\xA9nam", data, d);
    }
    {
        unsigned char data[64];
        size_t d = 0;
        put32be(data, &d, 16 + 5); put(data, &d, "data", 4);
        put32be(data, &d, 1); put32be(data, &d, 0);
        put(data, &d, "Choir", 5);
        i = atom(ilst, i, "\xA9""ART", data, d);
    }
    {
        unsigned char data[64];
        size_t d = 0;
        put32be(data, &d, (uint32_t)(16 + sizeof(JPEG))); put(data, &d, "data", 4);
        put32be(data, &d, 13); put32be(data, &d, 0);      /* 13 is a JPEG */
        put(data, &d, JPEG, sizeof(JPEG));
        i = atom(ilst, i, "covr", data, d);
    }

    unsigned char meta[512];
    size_t m = 0;
    put32be(meta, &m, 0);                                  /* the version `meta` alone carries */
    m = atom(meta, m, "ilst", ilst, i);

    unsigned char udta[512];
    size_t u = atom(udta, 0, "meta", meta, m);

    unsigned char mvhd[32];
    size_t v = 0;
    put32be(mvhd, &v, 0); put32be(mvhd, &v, 0); put32be(mvhd, &v, 0);
    put32be(mvhd, &v, 1000);                               /* the timescale */
    put32be(mvhd, &v, 123000);                             /* 123 seconds of it */
    put32be(mvhd, &v, 0);

    unsigned char moov[1024];
    size_t mo = atom(moov, 0, "mvhd", mvhd, v);
    mo = atom(moov, mo, "udta", udta, u);

    size_t o = 0;
    unsigned char ftyp[8] = { 'M', '4', 'A', ' ', 0, 0, 0, 0 };
    o = atom(out, o, "ftyp", ftyp, sizeof(ftyp));
    o = atom(out, o, "moov", moov, mo);
    return o;
}

/* ---- Ogg Opus: the same comments as FLAC, and a cover base64'd into one ---- */
static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const unsigned char *p, size_t len, char *out) {
    size_t at = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < len) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < len) v |= p[i + 2];
        out[at++] = BASE64[(v >> 18) & 0x3F];
        out[at++] = BASE64[(v >> 12) & 0x3F];
        out[at++] = i + 1 < len ? BASE64[(v >> 6) & 0x3F] : '=';
        out[at++] = i + 2 < len ? BASE64[v & 0x3F] : '=';
    }
    return at;
}

static size_t build_opus(unsigned char *out) {
    size_t o = 0;
    put(out, &o, "OggS", 4);
    for (int i = 0; i < 22; ++i) out[o++] = 0x00;
    put(out, &o, "OpusHead", 8);
    out[o++] = 1;                        /* the version */
    out[o++] = 2;                        /* two channels */
    out[o++] = 0; out[o++] = 0;          /* the pre-skip */
    put32le(out, &o, 48000);

    /* The picture block a comment carries, base64'd. */
    unsigned char pic[128];
    size_t p = 0;
    put32be(pic, &p, 3);
    put32be(pic, &p, 9); put(pic, &p, "image/png", 9);
    put32be(pic, &p, 0);
    put32be(pic, &p, 8); put32be(pic, &p, 8); put32be(pic, &p, 24); put32be(pic, &p, 0);
    put32be(pic, &p, (uint32_t)sizeof(JPEG));
    put(pic, &p, JPEG, sizeof(JPEG));
    char coded[256];
    size_t coded_len = base64_encode(pic, p, coded);

    unsigned char comment[512];
    size_t c = 0;
    put32le(comment, &c, 4); put(comment, &c, "test", 4);
    put32le(comment, &c, 2);
    put32le(comment, &c, 10); put(comment, &c, "TITLE=Bell", 10);
    put32le(comment, &c, (uint32_t)(23 + coded_len));
    put(comment, &c, "METADATA_BLOCK_PICTURE=", 23);
    put(comment, &c, coded, coded_len);

    put(out, &o, "OggS", 4);
    for (int i = 0; i < 22; ++i) out[o++] = 0x00;
    put(out, &o, "OpusTags", 8);
    put(out, &o, comment, c);
    return o;
}

/* ---- WAV ---- */
static size_t build_wav(unsigned char *out) {
    size_t o = 0;
    put(out, &o, "RIFF", 4); put32le(out, &o, 0); put(out, &o, "WAVE", 4);
    put(out, &o, "fmt ", 4); put32le(out, &o, 16);
    out[o++] = 1; out[o++] = 0;                      /* PCM */
    out[o++] = 2; out[o++] = 0;                      /* two channels */
    put32le(out, &o, 48000);
    put32le(out, &o, 48000 * 4);                     /* bytes a second */
    out[o++] = 4; out[o++] = 0;                      /* the block */
    out[o++] = 16; out[o++] = 0;                     /* sixteen bits */
    /* A LIST INFO with an odd length, which is padded — the trap that
       loses every chunk after it when a reader forgets. */
    put(out, &o, "LIST", 4); put32le(out, &o, 4 + 8 + 6 + 8 + 4);
    put(out, &o, "INFO", 4);
    put(out, &o, "INAM", 4); put32le(out, &o, 5); put(out, &o, "Tide\0", 5); out[o++] = 0x00;
    put(out, &o, "IART", 4); put32le(out, &o, 4); put(out, &o, "Sea\0", 4);
    put(out, &o, "data", 4); put32le(out, &o, 48000 * 4 * 2);   /* two seconds */
    return o;
}

int main(void) {
    printf("[test_tags] Starting music tag tests...\n");

    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = memory, .size = sizeof(memory) });
    static unsigned char file[4096];

    /* 1. ID3v2.3: UTF-16 with a mark, Latin-1, and a cover. */
    {
        size_t len = build_id3v23(file, false);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.title, "Song"));
        assert(is(t.artist, "Band"));
        assert(t.art_size == sizeof(JPEG) && t.art[0] == 0xFF && t.art[1] == 0xD8);
        assert(is(t.art_mime, "image/jpeg"));
        assert(is(t.codec, "MP3") && t.sample_rate == 44100 && t.bitrate_kbps == 128);
    }
    printf("  [PASS] ID3v2.3: text in three encodings, and the cover beside it\n");

    /* 2. The same tag with every 0xFF hidden reads the same. */
    {
        size_t len = build_id3v23(file, true);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.title, "Song"));
        assert(is(t.artist, "Band"));
        assert(t.art_size == sizeof(JPEG));
        assert(t.art[0] == 0xFF && t.art[1] == 0xD8 && t.art[2] == 0xFF && t.art[3] == 0xE0);
    }
    printf("  [PASS] A tag written unsynchronised gives back the bytes it hid\n");

    /* 3. ID3v2.4 counts a frame's size seven bits to the byte too. */
    {
        size_t len = build_id3v24(file);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.title, "Hi"));
        assert(t.album.len == 199);           /* the 200-byte frame, less its encoding byte */
        assert(is(t.year, "1999"));
    }
    printf("  [PASS] ID3v2.4 frame sizes are read the way 2.4 writes them\n");

    /* 4. ID3v1 fills in only what nothing else said. */
    {
        unsigned char tail[128];
        memset(tail, 0, sizeof(tail));
        memcpy(tail, "TAG", 3);
        memcpy(tail + 3, "Old title", 9);
        memcpy(tail + 33, "Old artist", 10);
        memcpy(tail + 93, "1984", 4);
        tail[125] = 0;
        tail[126] = 12;
        size_t len = build_id3v23(file, false);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .tail = tail, .tail_size = sizeof(tail), .file_size = len });
        assert(is(t.title, "Song"));          /* v2 said so, and it wins */
        assert(is(t.year, "1984"));           /* v2 did not */
        assert(is(t.track_number, "12"));
    }
    printf("  [PASS] ID3v1 is the fallback, never the overruling word\n");

    /* 5. FLAC: the stream's shape, the comments whatever case their keys
          are in, and the cover. */
    {
        size_t len = build_flac(file);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.codec, "FLAC"));
        assert(t.sample_rate == 44100 && t.channels == 2 && t.bits_per_sample == 16);
        assert(t.duration_seconds > 1.99 && t.duration_seconds < 2.01);
        assert(is(t.title, "Waltz") && is(t.artist, "Chopin") && is(t.track_number, "7"));
        assert(t.art_size == sizeof(JPEG) && is(t.art_mime, "image/jpeg"));
        assert(t.bitrate_kbps > 0);           /* from the size and the length */
    }
    printf("  [PASS] FLAC: the shape from STREAMINFO, the words from the comments\n");

    /* 6. MP4: the four bytes only `meta` carries, and a cover in `covr`. */
    {
        size_t len = build_mp4(file);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.title, "Rain"));
        assert(is(t.artist, "Choir"));
        assert(t.art_size == sizeof(JPEG));
        assert(t.duration_seconds > 122.9 && t.duration_seconds < 123.1);
    }
    printf("  [PASS] MP4: the atoms walked, `meta` counted right, `covr` found\n");

    /* 7. Opus: the comments of Ogg, and a cover base64'd into one. */
    {
        size_t len = build_opus(file);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.codec, "Opus"));
        assert(t.channels == 2 && t.sample_rate == 48000);
        assert(is(t.title, "Bell"));
        assert(t.art_size == sizeof(JPEG));
        assert(t.art[0] == 0xFF && t.art[10] == 0x33);
    }
    printf("  [PASS] Opus: a cover that arrived as text comes back as a picture\n");

    /* 8. WAV: the shape, and the INFO chunk after an odd-length one. */
    {
        size_t len = build_wav(file);
        rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(is(t.codec, "WAV"));
        assert(t.sample_rate == 48000 && t.channels == 2 && t.bits_per_sample == 16);
        assert(t.duration_seconds > 1.99 && t.duration_seconds < 2.01);
        assert(is(t.title, "Tide"));
        assert(is(t.artist, "Sea"));
    }
    printf("  [PASS] WAV: an odd-length chunk is padded, and the ones after it survive\n");

    /* 9. Nothing, nonsense and a lie all cost their own tag and no more. */
    {
        rubraview_tags_t empty = rubraview_tags_read(&arena, (rubraview_tags_source_t){ 0 });
        assert(empty.title.len == 0 && empty.art_size == 0);

        size_t len = build_flac(file);
        for (size_t cut = 4; cut < len; cut += 7) {
            rubraview_tags_t t = rubraview_tags_read(&arena, (rubraview_tags_source_t){
                .head = file, .head_size = cut, .file_size = len });
            assert(t.art_size <= sizeof(JPEG));
        }
        /* A frame that says it is longer than the file is not believed. */
        len = build_id3v23(file, false);
        file[10 + 4] = 0x7F; file[10 + 5] = 0xFF;      /* TIT2's size, made huge */
        rubraview_tags_t torn = rubraview_tags_read(&arena, (rubraview_tags_source_t){
            .head = file, .head_size = len, .file_size = len });
        assert(torn.title.len == 0);
    }
    printf("  [PASS] A file that says nothing, or lies, is read without harm\n");

    /* 10. Which names are a music file's. */
    {
        assert(rubraview_tags_is_music_name(U8("a.mp3")));
        assert(rubraview_tags_is_music_name(U8("A.FLAC")));
        assert(rubraview_tags_is_music_name(U8("x.opus")));
        assert(!rubraview_tags_is_music_name(U8("a.mp4")));
        assert(!rubraview_tags_is_music_name(U8("a")));
    }
    printf("  [PASS] A music file is known by its name before it is opened\n");

    printf("[test_tags] All tests passed successfully!\n");
    return 0;
}

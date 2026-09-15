#include "rubraview/settings_doc.h"
#include <string.h>

/*
 * The settings window, as the document the interpreter reads (§3.22,
 * D-13). This is the one list of settings in the program: a key read
 * anywhere must be declared here, and one marked `wired` must be read
 * somewhere — scripts/check-settings.py holds both.
 *
 * A choice's words are what settings.ini stores, in the order the page
 * shows them. A default is written as the value itself, not as an index.
 */
/* ISO C only promises string literals up to 4095 characters, and the
   build holds us to that — so the document is kept page by page and
   joined once, the first time it is asked for. */
static const char *const PARTS[] = {
"# rubraview settings window\n"
"\n",
"page general \"General\"\n"
"  section \"Window\"\n"
"  choice general.startup          \"On startup\"              blank | last_file | last_folder = last_folder\n"
"  toggle general.single_instance  \"Reuse the open window\"   = true wired\n"
"  toggle general.frameless        \"Frameless window\"        = true\n"
"  action shell.register           \"Register file types\"\n"
"  action shell.unregister         \"Remove file types\"\n"
"  section \"Title bar\"\n"
"  int    general.titlebar_trigger_px \"Titlebar trigger height\" 4..40 step 1 unit \"px\" = 12\n"
"  int    general.titlebar_hide_ms    \"Titlebar hide delay\"     100..3000 step 50 unit \"ms\" = 500\n"
"  section \"Where this is kept\"\n"
"  info   \"Settings file\"           {config.path}\n"
"\n",
"page viewer \"Viewer\"\n"
"  section \"Pages\"\n"
"  choice viewer.fit_mode          \"Default fit\"             window | width | height | actual | smart | stretch = window\n"
"  choice viewer.layout            \"Default layout\"          single | dual | book | webtoon = single\n"
"  toggle viewer.spread_autosplit  \"Split wide spreads\"      = true\n"
"  int    viewer.gutter_px         \"Gutter\"                  0..32 step 1 unit \"px\" = 8\n"
"  toggle viewer.portrait_collapse \"Collapse in a portrait window\" = true\n"
"  section \"Zoom\"\n"
"  int    viewer.zoom_step_percent \"Zoom step\"               5..50 step 1 unit \"%\" = 10\n"
"  choice viewer.interpolation     \"Scaling filter\"          nearest | bilinear | bicubic | lanczos3 = bicubic\n"
"  toggle viewer.pixel_grid        \"Pixel grid past 400%\"    = false\n"
"\n",
"page files \"Files\"\n"
"  section \"Folders and archives\"\n"
"  choice files.sort_mode          \"Sort by\"                 natural | lexical | date | size = natural\n"
"  toggle files.sort_ascending     \"Ascending\"               = true\n"
"  choice files.archive_codepage   \"Archive filenames\"       auto | utf8 | cp949 | shift_jis | gbk | big5 | cp1252 = auto\n"
"  toggle files.comicinfo          \"Read ComicInfo.xml\"      = true\n"
"  section \"Reading history\"\n"
"  toggle files.reading_history    \"Remember the page\"       = true\n"
"  toggle files.resume_prompt      \"Offer to resume\"         = true\n"
"  section \"Number keys send the file to\"\n"
"  choice curation.curation_mode   \"Number keys\"             move | copy = move wired\n"
"  path   curation.dir_1           \"Folder 1\"                wired\n"
"  path   curation.dir_2           \"Folder 2\"                wired\n"
"  path   curation.dir_3           \"Folder 3\"                wired\n"
"  path   curation.dir_4           \"Folder 4\"                wired\n"
"  path   curation.dir_5           \"Folder 5\"                wired\n"
"  path   curation.dir_6           \"Folder 6\"                wired\n"
"  path   curation.dir_7           \"Folder 7\"                wired\n"
"  path   curation.dir_8           \"Folder 8\"                wired\n"
"  path   curation.dir_9           \"Folder 9\"                wired\n"
"\n",
"page audio \"Audio\"\n"
"  section \"Volume\"\n"
"  int    audio.volume             \"Volume\"                  0..100 step 5 unit \"%\" = 100 wired\n"
"  toggle audio.mute               \"Mute\"                    = false wired\n"
"  section \"Playback\"\n"
"  toggle audio.gapless            \"Gapless playback\"        = true\n"
"  float  audio.crossfade_seconds  \"Crossfade\"               0.0..5.0 step 0.1 unit \"s\" = 0.0\n"
"  choice audio.replaygain         \"Volume levelling\"        off | track | album = off\n"
"  int    audio.wasapi_latency_ms  \"Audio latency\"           20..100 step 5 unit \"ms\" = 40\n"
"  toggle audio.bgm_pause_on_video \"Pause music during video\" = true\n"
"\n",
"page video \"Video\"\n"
"  section \"Decoding\"\n"
"  choice video.decoder            \"Decoder\"                 windows | ffmpeg = windows wired\n"
"  toggle video.hardware_decode    \"GPU decoding\"            = true\n"
"  info   \"FFmpeg\"                  {media.ffmpeg}\n"
"  section \"Subtitles\"\n"
"  int    video.subtitle_size      \"Subtitle size\"           10..72 step 1 unit \"pt\" = 24 wired\n"
"  int    video.subtitle_outline   \"Subtitle outline\"        0..8 step 1 unit \"px\" = 2 wired\n"
"  preview subtitle 3\n"
"  section \"Playing\"\n"
"  float  video.ab_step_seconds    \"A-B step\"                0.1..1.0 step 0.1 unit \"s\" = 0.5\n"
"  toggle video.video_wheel_zoom   \"Wheel zoom during video\" = true\n"
"\n",
"page display \"Display\"\n"
"  section \"Floating boxes\"\n"
"  int    ui.menubox_opacity       \"Menu box opacity\"        30..100 step 5 unit \"%\" = 90\n"
"  int    ui.toolbox_opacity       \"Toolbox opacity\"         30..100 step 5 unit \"%\" = 90\n"
"  section \"Tiles and colour\"\n"
"  choice display.tile_base_px     \"Touch tile size\"         48 | 64 | 96 = 64\n"
"  toggle display.color_management \"Use embedded ICC profiles\" = true\n"
"  choice display.accent           \"Accent colour\"           crimson | cobalt | emerald | amber | teal | purple = crimson\n"
"  info   \"Graphics\"                {gpu.adapter}\n"
"\n",
"page cache \"Cache\"\n"
"  section \"Memory\"\n"
"  int    cache.memory_cap_mb      \"Memory cap\"              256..4096 step 64 unit \"MB\" = 512\n"
"  int    cache.lookahead          \"Pages read ahead\"        1..16 step 1 = 2\n"
"  int    cache.lookbehind         \"Pages kept behind\"       0..8 step 1 = 1\n"
"  info   \"In use now\"              {cache.used}\n"
"  section \"Privacy\"\n"
"  toggle cache.privacy_clean      \"Always strip metadata on export\" = false\n"
"\n",
"page keys \"Keys\"\n"
"  section \"Bindings\"\n"
"  toggle keys.use_keymap_file     \"Use keymap.ini\"          = true\n"
"  table  keymap\n",
};

static char g_document[16384];
static size_t g_document_len;

u8str_t rubraview_default_settings_document(void) {
    if (g_document_len == 0) {
        for (size_t i = 0; i < sizeof(PARTS) / sizeof(PARTS[0]); ++i) {
            size_t n = strlen(PARTS[i]);
            if (g_document_len + n >= sizeof(g_document)) break;   /* the parse will say a page is missing */
            memcpy(g_document + g_document_len, PARTS[i], n);
            g_document_len += n;
        }
        g_document[g_document_len] = '\0';
    }
    return (u8str_t){ .ptr = g_document, .len = g_document_len };
}

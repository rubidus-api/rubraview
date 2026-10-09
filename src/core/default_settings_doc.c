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
"  choice general.startup          \"On startup\"              blank | last_file | last_folder = last_folder wired\n"
"  toggle general.single_instance  \"Reuse the open window\"   = true wired\n"
"  toggle general.always_on_top    \"Always on top\"           = false wired\n"
"  toggle general.frameless        \"Frameless window\"        = true wired\n"
"  section \"File types opened with Rubraview\"\n"
"  toggle shell.pictures           \"Pictures\"                = true wired\n"
"  toggle shell.comics             \"Comics (CBZ, CB7, CBR)\"  = true wired\n"
"  toggle shell.video              \"Video\"                   = true wired\n"
"  toggle shell.music              \"Music\"                   = true wired\n"
"  action shell.register           \"Register for this user\"\n"
"  action shell.register_all       \"Register for every user (administrator)\"\n"
"  action shell.unregister         \"Remove for this user\"\n"
"  action shell.unregister_all     \"Remove for every user (administrator)\"\n"
"  action shell.defaults           \"Open Windows' Default apps\"\n"
"  note \"The kinds switched on above are the ones registered or removed.\"\n"
"  note \"For every user, Windows asks for an administrator first.\"\n"
"  note \"If another program stays the default, choose Rubraview in Default apps.\"\n"
"  section \"Title bar\"\n"
"  int    general.titlebar_trigger_px \"Titlebar trigger height\" 4..40 step 1 unit \"px\" = 12 wired\n"
"  int    general.titlebar_hide_ms    \"Titlebar hide delay\"     100..3000 step 50 unit \"ms\" = 500 wired\n"
"  section \"Where this is kept\"\n"
"  info   \"Settings file\"           {config.path}\n"
"\n",
"page viewer \"Viewer\"\n"
"  section \"Pages\"\n"
"  choice viewer.fit_mode          \"Default fit\"             window | width | height | actual | smart | stretch = window wired\n"
"  choice viewer.layout            \"Layout\"                  single | dual | book | webtoon | comic = single wired\n"
"  int    viewer.gutter_px         \"Gutter\"                  0..32 step 1 unit \"px\" = 8 wired\n"
"  toggle viewer.portrait_collapse \"Collapse in a portrait window\" = true wired\n"
"  section \"Zoom\"\n"
"  int    viewer.zoom_step_percent \"Zoom step\"               5..50 step 1 unit \"%\" = 10 wired\n"
"  choice viewer.interpolation     \"Scaling filter\"          nearest | bilinear | bicubic | lanczos3 = bicubic wired\n"
"  toggle viewer.pixel_grid        \"Pixel grid past 400%\"    = false wired\n"
"\n",
"page files \"Files\"\n"
"  section \"Folders and archives\"\n"
"  choice files.sort_mode          \"Sort by\"                 text | explorer | modified | size | created = explorer wired\n"
"  toggle files.sort_ascending     \"Ascending\"               = true wired\n"
"  choice files.archive_codepage   \"Archive filenames\"       auto | utf8 | cp949 | shift_jis | gbk | big5 | cp1252 = auto wired\n"
"  toggle files.comicinfo          \"Read ComicInfo.xml\"      = true wired\n"
"  section \"Reading history\"\n"
"  toggle files.reading_history    \"Remember the page\"       = true wired\n"
"  toggle files.resume_prompt      \"Offer to resume\"         = true wired\n"
"  section \"The file on screen, to a folder\"\n"
"  path   curation.copy_dir        \"Copy to (F6)\"            wired\n"
"  path   curation.move_dir        \"Move to (F7)\"            wired\n"
"  note \"F6 copies the file on screen to the first folder and F7 moves it to the second; a page inside an archive can only be copied. A name already there gets -1, -2, ... added.\"\n"
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
"  choice audio.at_end             \"When a file ends\"        stop | next | one | all | shuffle = next wired\n"
"  toggle audio.gapless            \"Gapless playback\"        = true wired\n"
"  float  audio.crossfade_seconds  \"Crossfade\"               0.0..5.0 step 0.1 unit \"s\" = 0.0 wired\n"
"  choice audio.replaygain         \"Volume levelling\"        off | track | album = off wired\n"
"  section \"Sound\"\n"
"  choice audio.eq_preset          \"Equaliser\"               flat | rock | pop | jazz | classical | bass | vocal | acoustic | custom = flat wired\n"
"  toggle audio.night_mode         \"Night mode\"              = false wired\n"
"  choice audio.visualizer         \"Analyser\"                off | spectrum | scope = spectrum wired\n"
"  note \"Volume levelling uses the ReplayGain the file carries. Lyrics: a .lrc beside the song, else the words in its tags; a .cue that names the song cuts it into tracks.\"\n"
"  int    audio.wasapi_latency_ms  \"Audio latency\"           20..100 step 5 unit \"ms\" = 40 wired\n"
"  toggle audio.bgm_pause_on_video \"Pause music during video\" = true wired\n"
"  section \"Equaliser: Custom\"\n"
"  float  audio.eq_band1          \"31 Hz\"                    -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band2          \"62 Hz\"                    -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band3          \"125 Hz\"                   -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band4          \"250 Hz\"                   -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band5          \"500 Hz\"                   -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band6          \"1 kHz\"                    -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band7          \"2 kHz\"                    -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band8          \"4 kHz\"                    -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band9          \"8 kHz\"                    -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  float  audio.eq_band10         \"16 kHz\"                   -12.0..12.0 step 0.5 unit \"dB\" = 0.0 wired\n"
"  note \"Used when the equaliser is Custom. The equaliser window (EQ bands) moves them, and moving one there makes it Custom.\"\n"
"\n",
"page video \"Video\"\n"
"  section \"Decoding\"\n"
"  choice video.decoder            \"Decoder\"                 windows | ffmpeg = windows wired\n"
"  choice video.hardware_decode    \"GPU decoding\"            off | on | always = on wired\n"
"  note   \"off: the processor decodes.  on: the graphics card does, where it\"\n"
"  note   \"offers decoders - this is the one to use.  always: hand it to the\"\n"
"  note   \"card even when it says it has none (for testing only).\"\n"
"  info   \"FFmpeg\"                  {media.ffmpeg}\n"
"  section \"Subtitles\"\n"
"  int    video.subtitle_size      \"Subtitle size\"           10..72 step 1 unit \"pt\" = 24 wired\n"
"  int    video.subtitle_outline   \"Subtitle outline\"        0..8 step 1 unit \"px\" = 2 wired\n"
"  int    video.subtitle_background \"Subtitle box shade\"     0..100 step 5 unit \"%\" = 35 wired\n"
"  preview subtitle 3\n"
"  section \"Playing\"\n"
"  float  video.ab_step_seconds    \"A-B step\"                0.1..1.0 step 0.1 unit \"s\" = 0.5 wired\n"
"  toggle video.video_wheel_zoom   \"Wheel zoom during video\" = true wired\n"
"  section \"Adding FFmpeg (optional)\"\n"
"  note \"For files Windows cannot play. FFmpeg's DLLs are needed,\"\n"
"  note \"not ffmpeg.exe: put these five beside rubraview.exe,\"\n"
"  note \"then start rubraview again:\"\n"
"  note \"  avcodec-63  avformat-63  avutil-61  swscale-10  swresample-7\"\n"
"  note \"They must be FFmpeg 9.0; other versions are refused.\"\n"
"  note \"Get: github.com/BtbN/FFmpeg-Builds/releases\"\n"
"  note \"  ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip, its bin folder\"\n"
"  note \"The name must say shared; the lib folder's .dll.a is for building.\"\n"
"  note \"The LGPL build is enough. FFmpeg is not shipped with rubraview.\"\n"
"\n",
"page display \"Display\"\n"
"  section \"Floating boxes\"\n"
"  int    ui.menubox_opacity       \"Menu box opacity\"        30..100 step 5 unit \"%\" = 90 wired\n"
"  int    ui.toolbox_opacity       \"Toolbox opacity\"         30..100 step 5 unit \"%\" = 90 wired\n"
"  int    ui.menubox_columns       \"Menu box tiles a row\"    1..8 step 1 = 4 wired\n"
"  int    ui.menubox_size          \"Menu box tile size\"      75..200 step 5 unit \"%\" = 100 wired\n"
"  int    ui.toolbox_columns       \"Toolbox buttons a row\"   2..24 step 1 = 8 wired\n"
"  int    ui.toolbox_size          \"Toolbox button size\"     75..200 step 5 unit \"%\" = 100 wired\n"
"  note \"The grip beside a box's pin sets these too: drag it sideways or up and down.\"\n"
"  section \"Tiles and colour\"\n"
"  choice display.tile_base_px     \"Touch tile size\"         48 | 64 | 96 = 64 wired\n"
"  toggle display.color_management \"Use embedded ICC profiles\" = true wired\n"
"  choice display.accent           \"Accent colour\"           crimson | cobalt | emerald | amber | teal | purple = crimson wired\n"
"  info   \"Graphics\"                {gpu.adapter}\n"
"  section \"Resizing (export and batch)\"\n"
"  choice display.gpu_resize       \"Resize on the graphics card\" off | on | always = on wired\n"
"  note   \"on: bicubic and Lanczos resizes of a megapixel or more go to the\"\n"
"  note   \"card when it can run compute shaders; the CPU otherwise.\"\n"
"  note   \"always: every such resize, in software when there is no card\"\n"
"  note   \"(for testing). The result matches the CPU's to within one level.\"\n"
"\n",
"page cache \"Cache\"\n"
"  section \"Memory\"\n"
"  int    cache.memory_cap_mb      \"Memory cap\"              256..4096 step 64 unit \"MB\" = 512 wired\n"
"  int    cache.lookahead          \"Pages read ahead\"        1..16 step 1 = 2 wired\n"
"  int    cache.lookbehind         \"Pages kept behind\"       0..8 step 1 = 1 wired\n"
"  info   \"In use now\"              {cache.used}\n"
"  section \"Privacy\"\n"
"  toggle cache.privacy_clean      \"Always strip metadata on export\" = false wired\n"
"\n",
"page keys \"Keys\"\n"
"  section \"Bindings\"\n"
"  toggle keys.use_keymap_file     \"Use keymap.ini\"          = true wired\n"
"  action keys.export              \"Export keys to a file\"\n"
"  action keys.import              \"Import keys from a file\"\n"
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

#ifndef RUBRAVIEW_BATCHRUN_H
#define RUBRAVIEW_BATCHRUN_H

#include "rubraview/core.h"
#include "rubraview/batch.h"
#include "rubraview/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The batch execution engine and its command line (RFC-0001 §3.11),
 * RV-017.
 *
 * Two things live here that have no window in them, and so are checked
 * by the host suite rather than by running the program on a directory:
 *
 *  1. Turning `--batch --resize=50% --format=webp <dir>` into the job
 *     model RV-033 already defines. A mistyped flag has to be refused
 *     with a reason, not ignored — a batch run that silently skips an
 *     action would ruin a directory quietly.
 *  2. Running that job over a list of files with a bounded memory
 *     footprint: one arena per worker, reset between files (§3.11's
 *     execution engine), and the actual pixel work handed to a callback
 *     so the engine can be exercised without a decoder.
 */

/* ---- the command line ---- */

typedef enum rubraview_cli_err {
    RUBRAVIEW_CLI_OK = 0,
    RUBRAVIEW_CLI_ERR_UNKNOWN_FLAG,
    RUBRAVIEW_CLI_ERR_BAD_VALUE,
    RUBRAVIEW_CLI_ERR_NO_INPUT,
    RUBRAVIEW_CLI_ERR_TOO_MANY_ACTIONS,
} rubraview_cli_err_t;

#define RUBRAVIEW_BATCH_MAX_ACTIONS 16

typedef struct rubraview_cli_result {
    rubraview_cli_err_t err;
    u8str_t             offending;  /* the argument that failed, for the message */

    bool                batch_mode; /* --batch was given */
    bool                register_shell;    /* §3.19.3 --register-shell */
    bool                unregister_shell;  /* §3.19.3 --unregister-shell */
    bool                new_instance;      /* --new-instance: ignore §3.19.1 for this launch */
    bool                diagnostics;       /* --diag: report what the graphics device is and stop */
    bool                recursive;  /* --recursive */
    u8str_t             input;      /* the file or directory to work on */
    u8str_t             output_dir; /* --out=..., empty for "beside the source" */

    rubraview_batch_job_t      job;
    rubraview_batch_action_t   actions[RUBRAVIEW_BATCH_MAX_ACTIONS];
    rubraview_export_options_t export_options;
} rubraview_cli_result_t;

/**
 * Parse `--batch` and its flags. Arguments are taken as they arrive from
 * the OS, already UTF-8. Recognised:
 *
 *   --batch                  run without a window
 *   --recursive              descend into subdirectories
 *   --resize=50%             percentage
 *   --resize=1920x1080       bounding box
 *   --resize=w1920           fixed width
 *   --resize=h1080           fixed height
 *   --filter=lanczos3|bicubic|bilinear|nearest
 *   --format=jpg|png|webp|gif|bmp|tif|ico
 *   --quality=1..100
 *   --grayscale
 *   --privacy-clean
 *   --rotate=0|90|180|270    (--rotate=exif for the EXIF tag)
 *   --flip=h|v
 *   --sharpen=amount[,radius]
 *   --exposure=EV            (-3.0 .. +3.0)
 *   --contrast=N             (-100 .. +100)
 *   --include=*.jpg;*.png
 *   --exclude=*_thumb.*
 *   --min-size=BYTES  --max-size=BYTES
 *   --name={name}_thumb.{ext}
 *   --out=DIR
 *
 * A flag that is not on this list is an error and the run does not
 * start. This is deliberate: `--resiez=50%` must not quietly convert a
 * thousand files at full size.
 */
/*
 * The result is filled in place rather than returned. `job.actions`
 * points into `actions` in the same struct, and a returned-by-value
 * struct would leave that pointer aimed at a dead stack frame — a trap
 * this API refuses to set rather than document.
 */
void rubraview_cli_parse(rubraview_cli_result_t *out, proven_arena_t *arena,
                         int argc, const char *const *argv);

/** A one-line explanation of a parse failure, for the console. */
u8str_t rubraview_cli_error_text(rubraview_cli_err_t err);

/* ---- the execution engine ---- */

typedef struct rubraview_batch_input {
    u8str_t  path;
    uint64_t size_bytes;
} rubraview_batch_input_t;

typedef enum rubraview_batch_status {
    RUBRAVIEW_BATCH_STATUS_OK = 0,
    RUBRAVIEW_BATCH_STATUS_SKIPPED,  /* did not pass the job's filters */
    RUBRAVIEW_BATCH_STATUS_FAILED,   /* the worker reported a failure */
} rubraview_batch_status_t;

typedef struct rubraview_batch_item_result {
    rubraview_batch_status_t status;
    u8str_t                  input_path;
    u8str_t                  output_name;  /* the name the pattern produced */
} rubraview_batch_item_result_t;

/**
 * Process one file. `arena` is that worker's arena, already reset, so
 * the callback may allocate freely without the run's footprint growing
 * with the number of files. Return false to mark the item failed.
 */
typedef bool (*rubraview_batch_process_fn)(proven_arena_t *arena,
                                           const rubraview_batch_job_t *job,
                                           const rubraview_batch_input_t *input,
                                           u8str_t output_name,
                                           void *ctx);

typedef struct rubraview_batch_report {
    size_t total, processed, skipped, failed;
} rubraview_batch_report_t;

/**
 * Run a job over a list of inputs. Files are filtered first, named by
 * the job's pattern, and handed to `process` one at a time; `arena` is
 * reset before each file, which is what bounds the run's memory
 * regardless of how many files it holds.
 *
 * Results are written to `out_results` when it is given, up to
 * `results_capacity`; the counts in the report are complete either way.
 */
rubraview_batch_report_t rubraview_batch_run(proven_arena_t *arena,
                                             const rubraview_batch_job_t *job,
                                             const rubraview_batch_input_t *inputs, size_t input_count,
                                             u8str_t date_str,
                                             rubraview_batch_process_fn process, void *ctx,
                                             rubraview_batch_item_result_t *out_results,
                                             size_t results_capacity);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_BATCHRUN_H */

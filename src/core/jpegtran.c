#include "rubraview/jpegtran.h"

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* The vendored libjpeg-turbo (owner decision D-5). Only this module
   includes it, per the RFC-0001 §8.1 one-module rule. It is used for
   coefficient-level work exclusively: no pixel ever passes through it,
   which is what keeps WIC the only pixel codec in the program. */
#include <stdio.h>
#include "jpeglib.h"
#include "transupp.h"

/* libjpeg reports errors by longjmp'ing out of its own call stack, so
   the error handler is replaced with one that comes back here instead
   of calling exit(). A viewer must survive a corrupt file. */
typedef struct jt_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf escape;
} jt_error_mgr_t;

static void jt_error_exit(j_common_ptr cinfo) {
    jt_error_mgr_t *err = (jt_error_mgr_t*)cinfo->err;
    longjmp(err->escape, 1);
}

static void jt_output_message(j_common_ptr cinfo) {
    (void)cinfo;   /* a viewer does not write to stderr */
}

static JXFORM_CODE to_jxform(rubraview_jpegtran_op_t op) {
    switch (op) {
        case RUBRAVIEW_JPEGTRAN_ROT_90:     return JXFORM_ROT_90;
        case RUBRAVIEW_JPEGTRAN_ROT_180:    return JXFORM_ROT_180;
        case RUBRAVIEW_JPEGTRAN_ROT_270:    return JXFORM_ROT_270;
        case RUBRAVIEW_JPEGTRAN_FLIP_H:     return JXFORM_FLIP_H;
        case RUBRAVIEW_JPEGTRAN_FLIP_V:     return JXFORM_FLIP_V;
        case RUBRAVIEW_JPEGTRAN_TRANSPOSE:  return JXFORM_TRANSPOSE;
        case RUBRAVIEW_JPEGTRAN_TRANSVERSE: return JXFORM_TRANSVERSE;
        case RUBRAVIEW_JPEGTRAN_NONE:
        default:                            return JXFORM_NONE;
    }
}

bool rubraview_jpegtran_is_exact(rubraview_jpegtran_op_t op,
                                 int32_t width, int32_t height,
                                 int32_t mcu_width, int32_t mcu_height) {
    if (mcu_width <= 0 || mcu_height <= 0) return false;

    bool width_fits = (width % mcu_width) == 0;
    bool height_fits = (height % mcu_height) == 0;

    switch (op) {
        case RUBRAVIEW_JPEGTRAN_NONE:   return true;
        /* A horizontal flip mirrors columns of blocks, so only the width
           has to divide; the vertical flip is the mirror image of that. */
        case RUBRAVIEW_JPEGTRAN_FLIP_H: return width_fits;
        case RUBRAVIEW_JPEGTRAN_FLIP_V: return height_fits;
        /* Everything that turns the image needs both. */
        default: return width_fits && height_fits;
    }
}

bool rubraview_jpegtran_probe(const uint8_t *data, size_t size,
                              int32_t *out_width, int32_t *out_height,
                              int32_t *out_mcu_width, int32_t *out_mcu_height) {
    if (!data || size < 4) return false;

    struct jpeg_decompress_struct src;
    jt_error_mgr_t err;
    src.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = jt_error_exit;
    err.pub.output_message = jt_output_message;

    if (setjmp(err.escape)) {
        jpeg_destroy_decompress(&src);
        return false;
    }

    jpeg_create_decompress(&src);
    jpeg_mem_src(&src, data, (unsigned long)size);
    if (jpeg_read_header(&src, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&src);
        return false;
    }

    /* The MCU size follows from the chroma sampling: 4:2:0 makes it
       16x16, 4:4:4 makes it 8x8. libjpeg works it out during
       jpeg_calc_output_dimensions, but the sampling factors are enough. */
    int h_max = 1, v_max = 1;
    for (int c = 0; c < src.num_components; ++c) {
        if (src.comp_info[c].h_samp_factor > h_max) h_max = src.comp_info[c].h_samp_factor;
        if (src.comp_info[c].v_samp_factor > v_max) v_max = src.comp_info[c].v_samp_factor;
    }

    if (out_width) *out_width = (int32_t)src.image_width;
    if (out_height) *out_height = (int32_t)src.image_height;
    if (out_mcu_width) *out_mcu_width = (int32_t)(DCTSIZE * h_max);
    if (out_mcu_height) *out_mcu_height = (int32_t)(DCTSIZE * v_max);

    jpeg_destroy_decompress(&src);
    return true;
}

/*
 * The part that calls into libjpeg, kept in a function of its own.
 *
 * libjpeg reports an error by longjmp'ing back to the setjmp above it,
 * and a local variable written before the setjmp and read after the jump
 * is undefined unless it is volatile. Keeping this frame small — nothing
 * in it but what libjpeg needs — is what makes that rule easy to hold to,
 * and MinGW's -Wclobbered is what pointed it out.
 */
static rubraview_jpegtran_result_t run_transform(proven_arena_t *arena,
                                                 const uint8_t *data, size_t size,
                                                 JXFORM_CODE jxform, bool trim,
                                                 bool copy_markers) {
    rubraview_jpegtran_result_t result = {
        .err = RUBRAVIEW_JPEGTRAN_ERR_CORRUPT,
        .data = { .ptr = "", .len = 0 },
        .was_exact = true,
    };

    struct jpeg_decompress_struct src;
    struct jpeg_compress_struct dst;
    jt_error_mgr_t src_err, dst_err;
    jvirt_barray_ptr *coefficients = NULL;
    jvirt_barray_ptr *out_coefficients = NULL;
    jpeg_transform_info info;
    /* libjpeg writes both of these through pointers, so they live in
       memory rather than in a register and survive the longjmp; the two
       flags below do not, and are volatile for that reason. */
    unsigned char *out_buffer = NULL;
    unsigned long out_size = 0;
    volatile bool src_created = false, dst_created = false;

    memset(&info, 0, sizeof(info));
    info.transform = jxform;
    info.trim = trim;
    info.perfect = false;
    info.force_grayscale = false;
    info.crop = false;

    src.err = jpeg_std_error(&src_err.pub);
    src_err.pub.error_exit = jt_error_exit;
    src_err.pub.output_message = jt_output_message;
    dst.err = jpeg_std_error(&dst_err.pub);
    dst_err.pub.error_exit = jt_error_exit;
    dst_err.pub.output_message = jt_output_message;

    if (setjmp(src_err.escape) || setjmp(dst_err.escape)) {
        if (out_buffer) free(out_buffer);
        if (dst_created) jpeg_destroy_compress(&dst);
        if (src_created) jpeg_destroy_decompress(&src);
        result.err = RUBRAVIEW_JPEGTRAN_ERR_CORRUPT;
        return result;
    }

    jpeg_create_decompress(&src);
    src_created = true;
    jpeg_create_compress(&dst);
    dst_created = true;

    jpeg_mem_src(&src, data, (unsigned long)size);

    /* §3.10: which markers survive is decided here — copying none is the
       privacy-clean strip, and it costs nothing because it is simply not
       asking for them. */
    if (copy_markers) jcopy_markers_setup(&src, JCOPYOPT_ALL);
    else jcopy_markers_setup(&src, JCOPYOPT_NONE);

    if (jpeg_read_header(&src, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_compress(&dst);
        jpeg_destroy_decompress(&src);
        result.err = RUBRAVIEW_JPEGTRAN_ERR_CORRUPT;
        return result;
    }

    if (!jtransform_request_workspace(&src, &info)) {
        jpeg_destroy_compress(&dst);
        jpeg_destroy_decompress(&src);
        result.err = RUBRAVIEW_JPEGTRAN_ERR_UNSUPPORTED;
        return result;
    }

    /* This is the whole point: coefficients in, coefficients out. No
       IDCT runs, so nothing is requantised and nothing is lost. */
    coefficients = jpeg_read_coefficients(&src);
    if (!coefficients) {
        jpeg_destroy_compress(&dst);
        jpeg_destroy_decompress(&src);
        result.err = RUBRAVIEW_JPEGTRAN_ERR_CORRUPT;
        return result;
    }

    jpeg_copy_critical_parameters(&src, &dst);
    out_coefficients = jtransform_adjust_parameters(&src, &dst, coefficients, &info);

    jpeg_mem_dest(&dst, &out_buffer, &out_size);
    jpeg_write_coefficients(&dst, out_coefficients);
    jcopy_markers_execute(&src, &dst, copy_markers ? JCOPYOPT_ALL : JCOPYOPT_NONE);
    jtransform_execute_transform(&src, &dst, coefficients, &info);

    jpeg_finish_compress(&dst);
    jpeg_destroy_compress(&dst);
    dst_created = false;
    jpeg_finish_decompress(&src);
    jpeg_destroy_decompress(&src);
    src_created = false;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)out_size + 1);
    if (!proven_is_ok(res.err)) {
        free(out_buffer);
        result.err = RUBRAVIEW_JPEGTRAN_ERR_OUT_OF_MEMORY;
        return result;
    }
    char *copy = (char*)(void*)res.value.ptr;
    memcpy(copy, out_buffer, out_size);
    copy[out_size] = '\0';
    free(out_buffer);

    result.err = RUBRAVIEW_JPEGTRAN_OK;
    result.data = (u8str_t){ .ptr = copy, .len = (size_t)out_size };
    return result;
}

/* Everything that can be decided without libjpeg is decided here, before
   the frame that longjmp can jump back into. */
static rubraview_jpegtran_result_t transform(proven_arena_t *arena,
                                             const uint8_t *data, size_t size,
                                             rubraview_jpegtran_op_t op,
                                             rubraview_jpegtran_edge_t edge_policy,
                                             bool copy_markers) {
    rubraview_jpegtran_result_t result = {
        .err = RUBRAVIEW_JPEGTRAN_ERR_NOT_A_JPEG,
        .data = { .ptr = "", .len = 0 },
        .was_exact = true,
    };
    if (!arena || !data || size < 4) return result;
    if (data[0] != 0xFF || data[1] != 0xD8) return result;

    int32_t width = 0, height = 0, mcu_w = 0, mcu_h = 0;
    if (!rubraview_jpegtran_probe(data, size, &width, &height, &mcu_w, &mcu_h)) {
        result.err = RUBRAVIEW_JPEGTRAN_ERR_CORRUPT;
        return result;
    }

    bool exact = rubraview_jpegtran_is_exact(op, width, height, mcu_w, mcu_h);
    if (!exact && edge_policy == RUBRAVIEW_JPEGTRAN_REFUSE_IF_INEXACT) {
        result.err = RUBRAVIEW_JPEGTRAN_ERR_INEXACT;
        return result;
    }

    result = run_transform(arena, data, size, to_jxform(op),
                           edge_policy == RUBRAVIEW_JPEGTRAN_TRIM, copy_markers);
    if (result.err == RUBRAVIEW_JPEGTRAN_OK) result.was_exact = exact;
    return result;
}

rubraview_jpegtran_result_t rubraview_jpegtran_apply(proven_arena_t *arena,
                                                     const uint8_t *data, size_t size,
                                                     rubraview_jpegtran_op_t op,
                                                     rubraview_jpegtran_edge_t edge_policy,
                                                     bool privacy_clean) {
    return transform(arena, data, size, op, edge_policy, !privacy_clean);
}

rubraview_jpegtran_result_t rubraview_jpegtran_strip_metadata(proven_arena_t *arena,
                                                              const uint8_t *data, size_t size) {
    /* No transform, and no markers copied: the coefficients pass
       straight through and the metadata does not. */
    return transform(arena, data, size, RUBRAVIEW_JPEGTRAN_NONE,
                     RUBRAVIEW_JPEGTRAN_KEEP_EDGE, false);
}

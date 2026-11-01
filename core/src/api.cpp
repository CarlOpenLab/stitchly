// C API 实现
#include "internal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace ls;

namespace {

void sanitize(LsOptions& o) {
    if (o.align_width <= 0) o.align_width = 200;
    if (o.band_count <= 0) o.band_count = 4;
    if (o.dedupe_ncc <= 0) o.dedupe_ncc = 0.995f;
    if (o.min_accept_ncc <= 0) o.min_accept_ncc = 0.55f;
    if (o.max_chrome_ratio <= 0) o.max_chrome_ratio = 15;
    if (o.crop_top < 0) o.crop_top = 0;
    if (o.crop_bottom < 0) o.crop_bottom = 0;
}

int32_t array_get_frame(int32_t index, LsImage* out, void* user) {
    const LsImage* imgs = (const LsImage*)user;
    *out = imgs[index];
    return LS_OK;
}

} // namespace

extern "C" {

void ls_options_init(LsOptions* o) {
    if (!o) return;
    std::memset(o, 0, sizeof(*o));
    o->auto_detect_chrome = 1;
    sanitize(*o);
}

const char* ls_error_string(int32_t code) {
    switch (code) {
        case LS_OK: return "ok";
        case LS_ERR_INVALID_ARG: return "invalid argument";
        case LS_ERR_TOO_FEW_FRAMES: return "too few frames";
        case LS_ERR_OUT_OF_MEMORY: return "out of memory";
        case LS_ERR_STATE: return "invalid state (frame fetch failed?)";
        default: return "unknown error";
    }
}

void ls_result_free(LsResult* r) {
    if (!r) return;
    std::free(r->data);
    std::free(r->frames);
    std::free(r);
}

LsPlan* ls_plan_create(int32_t frame_width, int32_t frame_height,
                       const LsOptions* options) {
    if (frame_width < 16 || frame_height < 16) return nullptr;
    LsPlan* p = new (std::nothrow) LsPlan();
    if (!p) return nullptr;
    if (options)
        p->opt = *options;
    else
        ls_options_init(&p->opt);
    sanitize(p->opt);
    if (p->opt.align_width > frame_width) p->opt.align_width = frame_width;
    p->full_w = frame_width;
    p->full_h = frame_height;
    p->align_w = p->opt.align_width;
    p->scaled_h = std::max(1, (int)std::lround((double)frame_height * p->align_w / frame_width));
    p->yscale = (float)frame_height / (float)p->scaled_h;
    return p;
}

int32_t ls_plan_add_frame(LsPlan* p, const LsImage* f) {
    if (!p || !f || !f->data) return LS_ERR_INVALID_ARG;
    if (f->width != p->full_w || f->height != p->full_h) return LS_ERR_INVALID_ARG;
    p->grays.push_back(downscale_rgba_to_gray(*f, p->align_w));
    if (p->grays.size() >= 2)
        p->seq_diff.push_back(mean_abs_diff(p->grays[p->grays.size() - 1],
                                            p->grays[p->grays.size() - 2]));
    return LS_OK;
}

int32_t ls_plan_frame_count(const LsPlan* p) {
    return p ? (int32_t)p->grays.size() : 0;
}

int32_t ls_plan_last_frame_duplicate(const LsPlan* p) {
    if (!p || p->seq_diff.empty()) return 0;
    return p->seq_diff.back() < kDupDiffThreshold ? 1 : 0;
}

int32_t ls_plan_compose(LsPlan* p, ls_get_frame_fn get_frame, void* user,
                        LsResult** out_result) {
    if (!p || !get_frame || !out_result) return LS_ERR_INVALID_ARG;
    return run_pipeline(*p, get_frame, user, out_result);
}

void ls_plan_free(LsPlan* p) { delete p; }

int32_t ls_stitch_images(const LsImage* images, int32_t count,
                         const LsOptions* options, LsResult** out_result) {
    if (out_result) *out_result = nullptr;
    if (!images || !out_result) return LS_ERR_INVALID_ARG;
    if (count < 1) return LS_ERR_TOO_FEW_FRAMES;
    for (int32_t i = 0; i < count; i++)
        if (!images[i].data || images[i].width < 16 || images[i].height < 16)
            return LS_ERR_INVALID_ARG;

    LsPlan* p = ls_plan_create(images[0].width, images[0].height, options);
    if (!p) return LS_ERR_OUT_OF_MEMORY;
    for (int32_t i = 0; i < count; i++) {
        const int32_t rc = ls_plan_add_frame(p, &images[i]);
        if (rc != LS_OK) {
            ls_plan_free(p);
            return rc;
        }
    }
    const int32_t rc = ls_plan_compose(p, array_get_frame, (void*)images, out_result);
    ls_plan_free(p);
    return rc;
}

} // extern "C"

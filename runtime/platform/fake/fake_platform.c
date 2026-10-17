#include "fake_platform.h"
#include <math.h>
#include <string.h>
#include "ar_math.h"
static int f_wait(void *u, ar_frame_info *out) {
    fake_platform *fp = (fake_platform *)u;
    if (fp->stop_after && fp->frames >= fp->stop_after) return 0;
    memset(out, 0, sizeof *out);
    out->display_time_ns = fp->time_ns;
    fp->time_ns += fp->step_ns;
    fp->frames++;
    out->should_render = 1;
    out->view_count = fp->view_count > AR_MAX_VIEWS ? AR_MAX_VIEWS : fp->view_count;
    for (uint32_t i = 0; i < out->view_count; i++) {
        ar_pose off = ar_pose_identity();
        off.pos[0] = ((float)i - (float)(out->view_count - 1) * 0.5f) * fp->ipd;
        out->views[i].tracking_from_eye = ar_pose_mul(fp->head, off);
        out->views[i].fov = fp->fov;
        out->views[i].width = fp->width;
        out->views[i].height = fp->height;
        out->views[i].near_m = fp->near_m;
    }
    out->tracking_from_head = fp->head;
    out->head_tracked = fp->head_tracked; out->display_hz = fp->display_hz;
    return 1;
}
static int f_table(void *u, ar_table *out) {
    fake_platform *fp = (fake_platform *)u;
    out->tracking_from_table = fp->table;
    out->half_extent[0] = fp->table_half_extent[0]; out->half_extent[1] = fp->table_half_extent[1];
    return fp->table_valid;
}
static void f_begin(void *u) { ((fake_platform *)u)->begun++; }
static void f_end(void *u, const ar_submit_info *s) { fake_platform *fp = (fake_platform *)u; fp->submitted++; fp->last_submit = *s; }
static int f_pad(void *u, uint32_t player, ar_pad *out) {
    fake_platform *fp = (fake_platform *)u;
    if (player != 0 || !fp->pad_connected) { memset(out, 0, sizeof *out); return 0; }
    *out = fp->pad; return 1;
}
static ar_session_state f_state(void *u) { return ((fake_platform *)u)->state; }
void fake_platform_init(fake_platform *fp, double hz) {
    memset(fp, 0, sizeof *fp);
    fp->api.abi_version = AR_PLATFORM_ABI_VERSION;
    fp->api.user = fp;
    fp->api.wait_frame = f_wait; fp->api.get_table = f_table; fp->api.get_pad = f_pad; fp->api.session_state = f_state;
    fp->api.begin_frame = f_begin; fp->api.end_frame = f_end; fp->api.get_preview = NULL; fp->head_tracked = 1; fp->display_hz = (float)hz;
    fp->step_ns = (int64_t)ceil(1e9 / hz);   /* rounded up: frame k lands on or just after k/hz s, never below a tick boundary */
    fp->view_count = 1; fp->ipd = 0.063f;
    fp->fov.left = -0.5f; fp->fov.right = 0.5f; fp->fov.up = 0.5f; fp->fov.down = -0.5f;
    fp->width = 64; fp->height = 64;
    fp->head = ar_pose_identity(); fp->head.pos[1] = 1.5f; fp->head.pos[2] = 2.f;
    fp->table = ar_pose_identity(); fp->table.pos[1] = 0.75f;
    fp->table_valid = 1;
    fp->pad_connected = 1;
    fp->state = AR_SESSION_FOCUSED;
}

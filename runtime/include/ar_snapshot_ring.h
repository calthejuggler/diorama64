/* runtime/include/ar_snapshot_ring.h — 4 refcounted slots. The writer never blocks:
 * with every non-latest slot pinned by readers, begin_write returns NULL and the frame is dropped. */
#pragma once
#include "fc_builder.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ar_snapshot_ring ar_snapshot_ring;
ar_snapshot_ring *ar_ring_create(void);
void ar_ring_destroy(ar_snapshot_ring *r);
fc_builder *ar_ring_begin_write(ar_snapshot_ring *r);     /* game thread; NULL = drop this frame */
void ar_ring_publish(ar_snapshot_ring *r);
const fc_frame *ar_ring_acquire_latest(ar_snapshot_ring *r);
void ar_ring_release(ar_snapshot_ring *r, const fc_frame *f);  /* any thread (GPU completion) */
uint32_t ar_ring_dropped(ar_snapshot_ring *r);
#ifdef __cplusplus
}
#endif

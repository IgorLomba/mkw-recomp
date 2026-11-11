// GPU time per submission part (vk_sync_ts.c), for vk_sync.c. Called with its sub_lock held, except the completion
// thread's reads.
#ifndef VK_SYNC_TS_H
#define VK_SYNC_TS_H
#include <stdint.h>

#include "vk.h"

void vk_ts_start(VkCtx *c);                                       // vk_sync_start: timestamps if wanted, the stats hook
void vk_ts_shutdown(VkCtx *c);
int vk_ts_open(VkCommandBuffer cb);                               // the pair a command buffer times, -1 untimed
void vk_ts_close(VkCommandBuffer cb, int pair, uint64_t fence);   // before vkEndCommandBuffer
void vk_ts_begin(VkCommandBuffer cb);                             // the part being recorded
void vk_ts_end(VkCommandBuffer cb, uint64_t fence);
void vk_ts_drop(void);                                            // an empty submission: its pair is never written
int vk_ts_pending(void);                                          // the part being recorded is timed
#endif

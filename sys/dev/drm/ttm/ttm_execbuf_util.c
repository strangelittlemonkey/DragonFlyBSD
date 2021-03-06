/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <linux/export.h>
#include <linux/wait.h>

/* XXX this should go to dma-buf driver, for now just to avoid undef */
DEFINE_WW_CLASS(reservation_ww_class);

static void ttm_eu_backoff_reservation_locked(struct list_head *list,
					      struct ww_acquire_ctx *ticket)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		if (!entry->reserved)
			continue;

		entry->reserved = false;
		if (entry->removed) {
			ttm_bo_unreserve_ticket_locked(bo, ticket);
			entry->removed = false;

		} else {
			atomic_set(&bo->reserved, 0);
			wake_up_all(&bo->event_queue);
		}
	}
}

static void ttm_eu_del_from_lru_locked(struct list_head *list)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		if (!entry->reserved)
			continue;

		if (!entry->removed) {
			entry->put_count = ttm_bo_del_from_lru(bo);
			entry->removed = true;
		}
	}
}

static void ttm_eu_list_ref_sub(struct list_head *list)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;

		if (entry->put_count) {
			ttm_bo_list_ref_sub(bo, entry->put_count, true);
			entry->put_count = 0;
		}
	}
}

void ttm_eu_backoff_reservation(struct ww_acquire_ctx *ticket,
				struct list_head *list)
{
	struct ttm_validate_buffer *entry;
	struct ttm_bo_global *glob;

	if (list_empty(list))
		return;

	entry = list_first_entry(list, struct ttm_validate_buffer, head);
	glob = entry->bo->glob;
	lockmgr(&glob->lru_lock, LK_EXCLUSIVE);
	ttm_eu_backoff_reservation_locked(list, ticket);
	ww_acquire_fini(ticket);
	lockmgr(&glob->lru_lock, LK_RELEASE);
}
EXPORT_SYMBOL(ttm_eu_backoff_reservation);

/*
 * Reserve buffers for validation.
 *
 * If a buffer in the list is marked for CPU access, we back off and
 * wait for that buffer to become free for GPU access.
 *
 * If a buffer is reserved for another validation, the validator with
 * the highest validation sequence backs off and waits for that buffer
 * to become unreserved. This prevents deadlocks when validating multiple
 * buffers in different orders.
 */

int ttm_eu_reserve_buffers(struct ww_acquire_ctx *ticket,
			   struct list_head *list)
{
	struct ttm_bo_global *glob;
	struct ttm_validate_buffer *entry;
	int ret;

	if (list_empty(list))
		return 0;

	list_for_each_entry(entry, list, head) {
		entry->reserved = false;
		entry->put_count = 0;
		entry->removed = false;
	}

	entry = list_first_entry(list, struct ttm_validate_buffer, head);
	glob = entry->bo->glob;

	ww_acquire_init(ticket, &reservation_ww_class);
	lockmgr(&glob->lru_lock, LK_EXCLUSIVE);

retry:
	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		int owned;

		/* already slowpath reserved? */
		if (entry->reserved)
			continue;

		ret = ttm_bo_reserve_nolru(bo, true, true, true, ticket);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
			ttm_eu_del_from_lru_locked(list);
			owned = lockstatus(&glob->lru_lock, curthread);
			if (owned == LK_EXCLUSIVE)
				lockmgr(&glob->lru_lock, LK_RELEASE);
			ret = ttm_bo_reserve_nolru(bo, true, false,
						   true, ticket);
			if (owned == LK_EXCLUSIVE)
				lockmgr(&glob->lru_lock, LK_EXCLUSIVE);
			if (!ret)
				break;

			if (unlikely(ret != -EAGAIN))
				goto err;

			/* fallthrough */
		case -EAGAIN:
			ttm_eu_backoff_reservation_locked(list, ticket);
			lockmgr(&glob->lru_lock, LK_RELEASE);
			ttm_eu_list_ref_sub(list);
			ret = ttm_bo_reserve_slowpath_nolru(bo, true, ticket);
			if (unlikely(ret != 0))
				goto err_fini;

			lockmgr(&glob->lru_lock, LK_EXCLUSIVE);
			entry->reserved = true;
			if (unlikely(atomic_read(&bo->cpu_writers) > 0)) {
				ret = -EBUSY;
				goto err;
			}
			goto retry;
		default:
			goto err;
		}

		entry->reserved = true;
		if (unlikely(atomic_read(&bo->cpu_writers) > 0)) {
			ret = -EBUSY;
			goto err;
		}
	}

	ww_acquire_done(ticket);
	ttm_eu_del_from_lru_locked(list);
	lockmgr(&glob->lru_lock, LK_RELEASE);
	ttm_eu_list_ref_sub(list);
	return 0;

err:
	ttm_eu_backoff_reservation_locked(list, ticket);
	lockmgr(&glob->lru_lock, LK_RELEASE);
	ttm_eu_list_ref_sub(list);
err_fini:
	ww_acquire_done(ticket);
	ww_acquire_fini(ticket);
	return ret;
}
EXPORT_SYMBOL(ttm_eu_reserve_buffers);

void ttm_eu_fence_buffer_objects(struct ww_acquire_ctx *ticket,
				 struct list_head *list, void *sync_obj)
{
	struct ttm_validate_buffer *entry;
	struct ttm_buffer_object *bo;
	struct ttm_bo_global *glob;
	struct ttm_bo_device *bdev;
	struct ttm_bo_driver *driver;

	if (list_empty(list))
		return;

	bo = list_first_entry(list, struct ttm_validate_buffer, head)->bo;
	bdev = bo->bdev;
	driver = bdev->driver;
	glob = bo->glob;

	lockmgr(&glob->lru_lock, LK_EXCLUSIVE);
	lockmgr(&bdev->fence_lock, LK_EXCLUSIVE);

	list_for_each_entry(entry, list, head) {
		bo = entry->bo;
		entry->old_sync_obj = bo->sync_obj;
		bo->sync_obj = driver->sync_obj_ref(sync_obj);
		ttm_bo_unreserve_ticket_locked(bo, ticket);
		entry->reserved = false;
	}
	lockmgr(&bdev->fence_lock, LK_RELEASE);
	lockmgr(&glob->lru_lock, LK_RELEASE);
	ww_acquire_fini(ticket);

	list_for_each_entry(entry, list, head) {
		if (entry->old_sync_obj)
			driver->sync_obj_unref(&entry->old_sync_obj);
	}
}
EXPORT_SYMBOL(ttm_eu_fence_buffer_objects);

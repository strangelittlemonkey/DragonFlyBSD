/*-
 * Copyright (c) 2003 David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libpthread/thread/thr_barrier.c,v 1.1 2003/09/04 14:06:43 davidxu Exp $
 */

#include "namespace.h"
#include <machine/tls.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

int
_pthread_barrier_destroy(pthread_barrier_t *barrier)
{
	pthread_barrier_t bar;

	if (barrier == NULL || *barrier == NULL)
		return (EINVAL);

	bar = *barrier;
	if (bar->b_waiters > 0)
		return (EBUSY);
	*barrier = NULL;
	free(bar);
	return (0);
}

int
_pthread_barrier_init(pthread_barrier_t *barrier,
    const pthread_barrierattr_t *attr __unused, unsigned count)
{
	pthread_barrier_t bar;

	if (barrier == NULL || count == 0 || count > INT_MAX)
		return (EINVAL);

	bar = malloc(sizeof(struct pthread_barrier));
	if (bar == NULL)
		return (ENOMEM);

	_thr_umtx_init(&bar->b_lock);
	bar->b_cycle	= 0;
	bar->b_waiters	= 0;
	bar->b_count	= count;
	*barrier	= bar;

	return (0);
}

int
_pthread_barrier_wait(pthread_barrier_t *barrier)
{
	struct pthread *curthread;
	pthread_barrier_t bar;
	int64_t cycle;
	int ret;

	if (barrier == NULL || *barrier == NULL)
		return (EINVAL);

	bar = *barrier;
	curthread = tls_get_curthread();
	THR_UMTX_LOCK(curthread, &bar->b_lock);
	if (++bar->b_waiters == bar->b_count) {
		/* Current thread is lastest thread */
		bar->b_waiters = 0;
		bar->b_cycle++;
		_thr_umtx_wake(&bar->b_cycle, 0);
		THR_UMTX_UNLOCK(curthread, &bar->b_lock);
		ret = PTHREAD_BARRIER_SERIAL_THREAD;
	} else {
		cycle = bar->b_cycle;
		THR_UMTX_UNLOCK(curthread, &bar->b_lock);
		do {
			_thr_umtx_wait(&bar->b_cycle, cycle, NULL, 0);
			/* test cycle to avoid bogus wakeup */
		} while (cycle == bar->b_cycle);
		ret = 0;
	}
	return (ret);
}

__strong_reference(_pthread_barrier_init,	pthread_barrier_init);
__strong_reference(_pthread_barrier_wait,	pthread_barrier_wait);
__strong_reference(_pthread_barrier_destroy,	pthread_barrier_destroy);

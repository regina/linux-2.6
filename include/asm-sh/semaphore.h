#ifndef __ASM_SH_SEMAPHORE_H
#define __ASM_SH_SEMAPHORE_H

#include <linux/linkage.h>

#ifdef __KERNEL__
/*
 * SMP- and interrupt-safe semaphores.
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * SuperH verison by Niibe Yutaka
 *  (Currently no asm implementation but generic C code...)
 */

/*
 * On !PREEMPT_RT all semaphores are compat:
 */
#ifndef CONFIG_PREEMPT_RT
# define compat_semaphore semaphore
#endif

#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/wait.h>

#include <asm/system.h>
#include <asm/atomic.h>

struct compat_semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __COMPAT_SEMAPHORE_INIT(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.sleepers	= 0,						\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __COMPAT_MUTEX_INITIALIZER(name) \
	__COMPAT_SEMAPHORE_INIT(name,1)

#define __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct compat_semaphore name = __COMPAT_SEMAPHORE_INIT(name,count)

#define COMPAT_DECLARE_MUTEX(name) __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,1)
#define COMPAT_DECLARE_MUTEX_LOCKED(name) __COMPAT_DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void compat_sema_init (struct compat_semaphore *sem, int val)
{
/*
 *	*sem = (struct semaphore)__COMPAT_SEMAPHORE_INIT((*sem),val);
 *
 * i'd rather use the more flexible initialization above, but sadly
 * GCC 2.7.2.3 emits a bogus warning. EGCS doesn't. Oh well.
 */
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
	init_waitqueue_head(&sem->wait);
}

static inline void compat_init_MUTEX (struct compat_semaphore *sem)
{
	compat_sema_init(sem, 1);
}

static inline void compat_init_MUTEX_LOCKED (struct compat_semaphore *sem)
{
	compat_sema_init(sem, 0);
}

#if 0
asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);
#endif

asmlinkage void __compat_down(struct compat_semaphore * sem);
asmlinkage int  __compat_down_interruptible(struct compat_semaphore * sem);
asmlinkage int  __compat_down_trylock(struct compat_semaphore * sem);
asmlinkage void __compat_up(struct compat_semaphore * sem);
extern int compat_sem_is_locked(struct compat_semaphore *sem);

extern spinlock_t semaphore_wake_lock;

static inline void compat_down(struct compat_semaphore * sem)
{
	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		__compat_down(sem);
}

static inline int compat_down_interruptible(struct compat_semaphore * sem)
{
	int ret = 0;

	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		ret = __compat_down_interruptible(sem);
	return ret;
}

static inline int compat_down_trylock(struct compat_semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) < 0)
		ret = __compat_down_trylock(sem);
	return ret;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
static inline void compat_up(struct compat_semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__compat_up(sem);
}

#endif

#define compat_sema_count(sem) atomic_read (& (sem)->count)
#include <linux/semaphore.h>

#endif /* __ASM_SH_SEMAPHORE_H */
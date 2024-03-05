#ifndef _LINUX_CS1550_H
#define _LINUX_CS1550_H

#include <linux/list.h>

/**
 * A generic semaphore, providing serialized signaling and
 * waiting based upon a user-supplied integer value.
 */
struct cs1550_sem
{
	/* Current value. If nonpositive, wait() will block */
	long value;

	/* Unique numeric ID of the semaphore */
	long sem_id;

	/* Per-semaphore lock, serializes access to value and FIFO queue*/
	spinlock_t lock;

  struct list_head list; // this is the element that points to the previous and next semaphores in the list
  struct list_head waiting_tasks; // this is the head node of the list of processes waiting on this specific semaphore
};

struct cs1550_task
{
	struct list_head list; // previous and next pointers
	struct task_struct *task; // pointer to the task control block
};
#endif

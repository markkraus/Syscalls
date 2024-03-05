#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/cs1550.h>

static DEFINE_RWLOCK(sem_rwlock); // Enforces one process at a time to read/write
static LIST_HEAD(sem_list); // Global static list of semaphores
static long sem_id_count = 0; // Unique semaphore ID given at the creation of a new semaphore

/**
 * Finds the semaphore within the system-wide list of semaphores.
 * 
 * Returns the found semaphore.
 *
 * Returns NULL if no semaphore has the passed-in sem_id.
 */
static struct cs1550_sem *get_sem_id (long sem_id) 
{
  struct cs1550_sem *sem = NULL;

  // Safely search the semaphore list for the corresponding semaphore ID
  read_lock(&sem_rwlock);
  list_for_each_entry(sem, &sem_list, list) {
    if(sem->sem_id == sem_id) {
      // The semaphore was found in the list
      break;
    }
  }
  read_unlock(&sem_rwlock);

  if (sem->sem_id == sem_id) {
    // The semaphore was found in the list
    return sem;
  }

  // Semaphore ID did not match any semaphore in the list
  return NULL;
}

/**
 * Creates a new semaphore. The long integer value is used to
 * initialize the semaphore's value.
 *
 * The initial `value` must be greater than or equal to zero.
 *
 * On success, returns the identifier of the created
 * semaphore, which can be used with up(), down(), and close().
 *
 * On failure, returns -EINVAL if the initial value is less than zero,
 * or -ENOMEM if memory allocation fails for the new semaphore.
 */
SYSCALL_DEFINE1(cs1550_create, long, value)
{
  if (value < 0) {
    // Unacceptable initial value
    return -EINVAL;
  }

  // Allocate memory to the new semaphore
  struct cs1550_sem *sem = kmalloc(sizeof(struct cs1550_sem), GFP_KERNEL); // The allocation might sleep if no memory is available
  if (!sem) {
    // Memory allocation failed
    return -ENOMEM;
  }

  // Initialize semaphore struct members
  sem->value = value;
  sem->sem_id = sem_id_count++;
  spin_lock_init(&sem->lock);
  INIT_LIST_HEAD(&sem->list);
  INIT_LIST_HEAD(&sem->waiting_tasks);

  // Add the semaphore to the system-wide list of semaphores
  write_lock(&sem_rwlock);
  list_add(&sem->list, &sem_list);
  write_unlock(&sem_rwlock);

	return sem->sem_id;
}

/**
 * Performs the down() operation on an existing semaphore
 * using the semaphore identifier obtained from a previous call
 * to cs1550_create().
 *
 * This decrements the value of the semaphore and causes the
 * calling process to sleep if the semaphore's value was 0 or less,
 * until up() is called on the semaphore by another process.
 *
 * Returns 0 when successful, -EINVAL if the semaphore ID is invalid, 
 * or -ENOMEM if memory allocation fails while adding to the FIFO queue.
 */
SYSCALL_DEFINE1(cs1550_down, long, sem_id)
{
  // Get the semaphore corresponding to the ID argument
  read_lock(&sem_rwlock);
  struct cs1550_sem *sem = get_sem_id(sem_id);
  if (sem == NULL) {
    // Invalid semaphore ID
    read_unlock(&sem_rwlock);
    return -EINVAL;
  }
  read_unlock(&sem_rwlock);

  // Update the semaphore value
  write_lock(&sem_rwlock);
  spin_lock(&sem->lock);
  sem->value--;
  spin_unlock(&sem->lock);

  if (sem->value < 0) {
    // No more available resources - suspend current process

    // Allocate memory for the process
    struct cs1550_task *task = kmalloc(sizeof(struct cs1550_task), GFP_KERNEL);
    if (!task) {
      // Memory allocation failed
      write_unlock(&sem_rwlock);
      return -ENOMEM;
    }
    
    // Initialize, link, and add task to waiting list
    INIT_LIST_HEAD(&task->list);
    task->task = current; // Assign the task control block to the current executing task
    list_add_tail(&task->list, &sem->waiting_tasks); // Add the task to the semaphore's waiting tasks list

    write_unlock(&sem_rwlock);

    // Set the process to sleep
    set_current_state(TASK_INTERRUPTIBLE);
    schedule();
  } else {
    // No need to sleep process, but unlock the RWLock
    write_unlock(&sem_rwlock);
  }

	return 0;
}

/**
 * Performs the up() operation on an existing semaphore
 * using the semaphore identifier obtained from a previous call
 * to cs1550_create().
 *
 * This increments the value of the semaphore and wakes up a process 
 * waiting on the semaphore if such a process exists in the semaphore's FIFO queue.
 *
 * Returns 0 when successful, or -EINVAL if the semaphore ID is
 * invalid.
 */
SYSCALL_DEFINE1(cs1550_up, long, sem_id)
{
  // Find the semaphore from the system-wide semaphore list
  read_lock(&sem_rwlock);
  struct cs1550_sem *sem = get_sem_id(sem_id);
  if (sem == NULL) {
    // Invalid semaphore ID
    read_unlock(&sem_rwlock);
    return -EINVAL;
  }
  read_unlock(&sem_rwlock);

  // Update the semaphore value
  write_lock(&sem_rwlock);
  spin_lock(&sem->lock);
  sem->value++;
  spin_unlock(&sem->lock);

  // Wake up a process waiting on the semaphore
  if (sem->value <= 0) {
    // There are processes waiting - delete top process and wake up next process
    struct cs1550_task *task = list_first_entry(&sem->waiting_tasks, struct cs1550_task, list);
    list_del(&task->list);
    wake_up_process(task->task);
  }
  write_unlock(&sem_rwlock);

	return 0;
}

/**
 * Removes an already-created semaphore from the system-wide
 * semaphore list using the identifier obtained from a previous
 * call to cs1550_create().
 *
 * Returns 0 when successful or -EINVAL if the semaphore ID is
 * invalid or the semaphore's FIFO queue is not empty.
 */
SYSCALL_DEFINE1(cs1550_close, long, sem_id)
{
  // Find the semaphore from the system-wide semaphore list
  read_lock(&sem_rwlock);
  struct cs1550_sem *sem = get_sem_id(sem_id);
  if (sem == NULL) {
    // Invalid semaphore ID
    read_unlock(&sem_rwlock);
    return -EINVAL;
  }
  read_unlock(&sem_rwlock);

  // Free each task waiting on the semaphore
  write_lock(&sem_rwlock);
  struct cs1550_sem *task, *temp_task;
  list_for_each_entry_safe(task, temp_task, &sem->waiting_tasks, list) {
    kfree(task);
  }

  // Remove the semaphore from the system-wide list and free its allocated memory
  list_del(&sem->list);
  kfree(sem);
  write_unlock(&sem_rwlock);

	return 0;
}

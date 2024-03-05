Project 1: Syscalls
===================

To submit your project code, submit your GitHub Classroom repository to Gradescope. __Only the following files will be used for grading__:
- `include/linux/cs1550.h`
- `kernel/cs1550.c`
- `arch/x86/entry/syscalls/syscall_32.tbl`
- `p1-writeup.md` (the writeup in [GitHub Markdown](https://guides.github.com/features/mastering-markdown/))

Don't forget to name your writeup `p1-writeup.md`.

- Due date: Friday, 2/16/2024, at 11:59 pm (EST)
- Late due date: Sunday, 2/18/2024, at 11:59 pm (EST)

(This project is based upon Project 2 of Dr. Misurda's CS 1550 course.)

---

Any time we share data between two or more processes or threads, we risk having a race condition and corrupting data. To avoid these situations, we discussed various mechanisms to ensure that critical regions are mutually exclusive. In this project, you will create a semaphore data type and implement the two semaphore operations we described in class, namely `down()` and `up()`. Recall that `down()` is used to try to decrement a semaphore, potentially causing the requesting process to block, whereas `up()` is used to increment the semaphore, potentially waking up a process waiting on the semaphore.


Semaphore data type
-------------------

To encapsulate the semaphore, we will use a C struct that contains an integer value, an identifier, a spinlock, and a **FIFO** queue of [__task control blocks__](https://elixir.bootlin.com/linux/latest/source/include/linux/sched.h#L746):

```c
struct cs1550_sem
{
	long value;
	unsigned long sem_id;
	spinlock_t lock;
	// Add a FIFO queue (see FIFO queues below)
};
```

The value field represents the value of the semaphore. Each semaphore has a unique `unsigned long` integer identifier. You are free to generate the identifier at random (making sure the identifiers are unique) or use a global integer that gets incremented with the creation of each semaphore. Each element in the FIFO queue stores a pointer to the task control block of a process waiting on the semaphore. The spinlock inside the semaphore struct is used to ensure that the increment or decrement of the semaphore's value and the updates to the FIFO queue are performed __atomically__.

Spinlocks
---------

Recall from class that the semaphore value and FIFO queue are shared data and must be updated atomically. The spinlock inside the semaphore is used to do the increment or decrement of the semaphore's value atomically. An alternative to using spinlocks would be disabling interrupts, which is viable because the semaphore implementation you will write is part of the kernel. However, in Linux (and in general, except for some embedded systems), disabling interrupts is not the preferred way of doing in-kernel synchronization, since your code might be running on a multi-core processor and disabling interrupts on one core does not disable them on ther other cores.

You must initialize each semaphore's spinlock with the following function that takes a pointer to the spinlock. **Make sure you pass a pointer to the function!**

```c
spin_lock_init(spinlock_t *);
```

You can then surround the critical regions with calls to the following functions, which also take a pointer to a spinlock:
```c
spin_lock(spinlock_t *);
//critical region code
spin_unlock(spinlock_t *);
```

Global Semaphore List and RWLock
---------------------------------

Your implementation must store a system-wide list of semaphores. We will allow _concurrent reads_ to this system-wide list of semaphores (to perform `up()` and `down()` on the semaphores), but allow only one process at a time to write (i.e., to add and remove from the list). To enforce that, access to this global list of semaphores should be protected using a system-wide [**rwlock**](https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_api.html#spinlock), which needs to be declared and initialized using the following macro, placed at the global level in the `cs1550.c` file:

```c
static DEFINE_RWLOCK(sem_rwlock);
```

As before, you can then surround the protected regions with the following functions:
```c
read_lock(rwlock_t *);
//critical region with read-only access to shared data
read_unlock(rwlock_t *);

write_lock(rwlock_t *);
//critical region with write access to shared data
write_unlock(rwlock_t *);
```
Note that the macro `DEFINE_RWLOCK(sem_rwlock)` will create a variable (not a pointer) of type `rwlock_t`, which means that you will need to pass the address of the variable to the read and write functions:

```c
read_lock(&sem_rwlock);
```

FIFO Queue
-----------

As part of your implementation, you will need to implement both a global list of semaphores, protected by the rwlock mentioned above, and a FIFO queue of waiting tasks for each semaphore. The FIFO queue for a semaphore contains pointers to the task control blocks of the processes waiting on the semaphore. If you wish to do so, you can write your own list implementations. However, Linux provides a [(circular, doubly-linked) linked list implementation](https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_api.html#lists) in [`include/linux/list.h`](https://github.com/torvalds/linux/blob/master/include/linux/list.h) which can be used to implement the global semaphore list and the FIFO queues.

Be sure to read the following section very carefully, because improperly using the built-in lists may lead to odd bugs and crashes and hours of debugging! You have been warned ;-).

The basic structure of the list implementation is
```c
struct list_head
{
	struct list_head *next, *prev;
};
```

To declare a global static list (e.g., the global semaphore list), use the `LIST_HEAD` macro:
```c
static LIST_HEAD(sem_list);
```

This variable declaration automatically defines and initializes the head and tail pointers of a linked list node.

To append an item to the list, you must first declare a struct for the list item and include a field of type `struct list_head`. To add semaphores to the global list, modify your `struct cs1550_sem` to add the `struct list_head` field:

```c
struct cs1550_sem
{
	// ...
	struct list_head list; // this is the element that points to the previous and next semaphores in the list
};
```

This added field contains a pointer to the previous semaphore and the next semaphore in the list.

Recall that the global `sem_list` node has already been initialized using the macro `LIST_HEAD`. To add a semaphore to the list, the `struct list_head` field inside the new semaphore has to be initialized. You can use the below code to initialize the previous and next pointers and then append to the global list:

```c
struct cs1550_sem *sem = ...; // some memory allocation and initialization
INIT_LIST_HEAD(&sem->list); // initialize previous and next pointers

list_add(&sem->list, &sem_list);
```
Note that both `INIT_LIST_HEAD` and `list_add` take **pointers** to `struct list_head`.

To remove a specific semaphore (pointed to be a pointer named `sem`) from the global list, simply call `list_del` on a pointer to that semaphore's list field:

```c
list_del(&sem->list);
```

To traverse a list, you can use `list_for_each_entry()`, which takes three parameters:
1. a pointer to the embedding struct type (e.g., `struct cs1550_sem`)
2. a pointer to the `list_head` where you want to start the traversal
3. the name of the `struct list_head` field inside the embedding struct (e.g., `list` for the `struct cs1550_sem`)

For example, the following code traverses the global semaphore list.
```c
struct cs1550_sem *sem = NULL;

list_for_each_entry(sem, &sem_list, list) {
	// do something with sem
}
```

To declare the list for the FIFO queue for each semaphore, you can follow a slightly different procedure than what we did for the global semaphore list. First, add the head and tail pointers of the list to the `struct cs1550_sem`.

```c
struct cs1550_sem
{
	// ...
	struct list_head waiting_tasks; //this is the head node of the list of processes waiting on this specific semaphore
};
```

Then, declare a structure for the list node, which contains the previous and next pointers (i.e., `struct list_head`) and a pointer to the task control block (`struct task_struct *`).

```c
struct cs1550_task
{
	struct list_head list;
	struct task_struct *task;
};
```

Similarly to before, you must initialize the next and previous pointers for the head node and for each node added to the list.

```c
struct cs1550_sem *sem = ....; // some memory allocation and initialization
INIT_LIST_HEAD(&sem->list);
INIT_LIST_HEAD(&sem->waiting_tasks); // initialize head and tail for head node
...
// ...

struct cs1550_task *task_node = ...; // some memory allocation and initialization
INIT_LIST_HEAD(&task_node->list); // initialize previous and next pointers for each added node
```

To implement a FIFO queue, you may need to add at the tail of the list. To do that, use the `list_add_tail` function:

```c
list_add_tail(&task_node->list, &sem->waiting_tasks);
```

To check if a list is empty, use the `list_empty` function. For example:

```c
if (!list_empty(&sem->waiting_tasks))
	// ...
```

To get the first entry in a list, you can call `list_first_entry`, which takes three arguments:

1. a pointer to the list's head node
2. the name of the embedding struct (`struct cs1550_task`) that contains the `list_head` field
3. the name of the `struct list_head` field inside the embedding struct

An example call is:
```c
list_first_entry(&sem->waiting_task, struct cs1550_task, list) 
```

Here is a summary of the functions to manipulate the list mentioned above.
- Add a new entry immediately after the list head.
```c
list_add(struct list_head *new, struct list_head *head);
```
- Add a new entry to the end of the list.
```c
list_add_tail(struct list_head *new, struct list_head *head);
```

- Remove a given entry from the list.
```c
list_del(struct list_head *entry);
```

- Return true if the given list is empty.
```c
list_empty(struct list_head *head);
```

Syscalls for synchronization
----------------------------

You will add four new system calls with the following signatures to create, operate on, and close semaphores.

```c
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
	return -1;
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
	return -1;
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
	return -1;
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
	return -1;
}
```

Sending a process to sleep
--------------------------

As part of the `down()` operation, the current task may need to sleep. In Linux, you can do that as part of a two-step process.

1. Mark the task as no longer being ready by changing its state to [`TASK_INTERRUPTIBLE`](https://tldp.org/LDP/tlk/kernel/processes.html)
   ```c
   set_current_state(TASK_INTERRUPTIBLE);
   ```
2. Invoke the scheduler to pick a (different) ready task:
   ```c
   schedule();
   ```

**Think carefully about when to unlock the semaphore's spinlock in this two-step process**. Should the unlock happen before the first step, between the two steps, or after both steps?

Waking up a process
-------------------

As part of `up()`, you potentially need to wake up a sleeping process. You can do this via:

```c
wake_up_process(sleeping_task);
```

where `sleeping_task` is a **pointer to a task contol block** (i.e., `struct task_struct *`) that represents a process that was put to sleep in the `down()` operation.


Adding a new syscall
--------------------

To add a new syscall to the Linux kernel, there are two main files that need to be modified:

1. `kernel/cs1550.c`. This contains the actual implementation of the system calls.
2. `arch/x86/entry/syscalls/syscall_32.tbl`. This declares the numbers corresponding to the syscalls.

Add the four new syscall definitions at the bottom of `syscall_32.tbl`, starting with number 441. The contents of the syscall table should be self-explanatory; just follow the pattern.

When implementing your system calls, you may find it useful to look at the definitions of some of the existing syscalls, such as `fork()` in `kernel/fork.c` or `wait()` in `kernel/exit.c`.


Testing the syscalls
--------------------

As you implement your syscalls, you are also going to want to test them via a userspace program. The first thing you need is a way to use the new syscalls. We do this by using the `syscall()` function. The syscall function takes as its first parameter the number that represents the system call you would like to make (e.g., `cs1550_down`). The remainder of the parameters are passed as the parameters to your syscall function. You have the syscall numbers exported as `#define`s of the form `__NR_syscall` via the syscall table that you modified when you added your syscalls (`syscall_32.tbl`). We have provided wrapper functions or macros to make the syscalls appear more natural in a C program. For example, check the following function definition inside `initramfs/cs1550-syscall.h`:

```c
long down(long sem_id)
{
	return syscall(__NR_cs1550_down, sem_id);
}
```

There are multiple test programs already provided with this code inside the project, in the directory `initramfs`. They are:

- `trafficsim.c`
- `trafficsim-mutex.c`
- `trafficsim-strict-order.c`

Please check these programs to get an understanding of how your semaphores will be used.  Note that we will use these programs to test your implementation, that is, they are not only illustrative.

To build and automatically test your kernel semaphore implementation, run `make test` from the root directory (see section below on running and testing programs). If all goes well, your output should be similar to the following:

```
This line has to print first
This line has to print second
This line has to print last
parent process entering critical section, iteration 0.
parent process exiting CS, iteration 0. j=1
Child 1 process entering critical section, iteration 0.
Child 1 process exiting CS, iteration 0. j=1
Child 2 process entering critical section, iteration 0.
Child 2 process exiting CS, iteration 0. j=1
This line has to print first
This line has to print second
This line has to print last
reboot: Restarting system
```
or

```
This line has to print first
This line has to print second
This line has to print last
parent process entering critical section, iteration 0.
parent process exiting CS, iteration 0. j=1
Child 2 process entering critical section, iteration 0.
Child 2 process exiting CS, iteration 0. j=1
Child 1 process entering critical section, iteration 0.
Child 1 process exiting CS, iteration 0. j=1
This line has to print first
This line has to print second
This line has to print last
reboot: Restarting system
```


Other Requirements and Hints
----------------------------

- There should be no artificial limit on the size of the global semaphore list and the FIFO queue of each semaphore. One way of achieving that is by [allocating memory as needed using `kmalloc` and freeing it using `kfree`](https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_api.html#memory-allocation). Recall that the semaphore lists are declared in the kernel, and therefore you must use the "k" versions of malloc and free.
- If you use `kmalloc` while a spinlock or rwlock is currently held, you must use the `GFP_ATOMIC` or `GFP_NOWAIT` options.
- There should be no memory leaks (e.g., by forgetting to free dynamically allocated memory).
- You can get a pointer to the current process's `task_struct` by accessing the global variable `current`. You will need to save these pointers in the semaphore's FIFO queue.
- **Avoid calling `spin_unlock` on an already-unlocked semaphore**. This will cause your process to block.
- Do not put a process to sleep while the process is holding a spinlock. This will cause a deadlock.
- Use [`printk()`](https://linux-kernel-labs.github.io/refs/heads/master/labs/kernel_api.html#printk) with the `KERN_WARNING` log level for printing debugging messages from kernel code. Here is an example:
   ```c
   printk(KERN_WARNING "cs1550_down syscall: pid=%d entered the critical section.\n", current->pid);
   ```
- In general, you can use some standard C library functions, but not all. If they do a syscall, they may not work.
- You should add some logic to catch potential errors before they propagate further, especially if they will propagate into kernel code. For example, if the user program wrongly initializes the semaphore to a negative value or if the queue logic has a bug, you do not want that error to cause a segmentation fault in kernel code.
- **It is important for all labs and projects of this class that you commit frequently into your GitHub Classroom repository.**
- __The recitations include very helpful hints and diagrams to help clarify many issues you may encounter in this project.__

Setting up the kernel sources
-----------------------------

These instructions will help you clone the starter code of the project from your GitHub Classroom private repository into Thoth.
1. Click on the GitHub Classroom link for the project on Canvas. After you link your GitHub username to the course and accept the assignment, a private repository with the name `cs1550-2244/cs1550-project1-GITHUB_USERNAME.git`, where `GITHUB_USERNAME` is your GitHub username that you have used when accepting the GitHub Classroom assignment, will be created for you.
2. Log in to Thoth using your Pitt account. From a UNIX box, you can type: `ssh PITT_USERNAME@thoth.cs.pitt.edu`, assuming `PITT_USERNAME` is your Pitt ID. From Windows, you may use PuTTY or the PowerShell ssh client.
3. Make sure that you are in a private directory (e.g., your private AFS folder or `/u/OSLab/PITT_USERNAME` on Thoth).
4. From the server's command prompt, run the following command to download the starter code of the project:
`git clone https://github.com/cs1550-2244/cs1550-project1-GITHUB_USERNAME.git` where `GITHUB_USERNAME` is your GitHub username.
5. Enter your GitHub username and password. An alternative that is more secure is to [generate and use personal access tokens](https://docs.github.com/en/github/authenticating-to-github/creating-a-personal-access-token) instead of passwords.
6. Change directory into the project folder using `cd cs1550-project1-GITHUB_USERNAME/project1`.
7. Build the kernel:
   ```sh
   make kernel
   ```

8. Build and run the test programs (to be done **after** the syscalls are added to `syscall_32.tbl`):
   ```sh
   make debug
   ```
You should only need to do steps 5-7 once. Redoing these steps will undo any uncommitted changes you've made and give you a fresh copy of your code, should things go horribly awry.

When you build for the first time, and after you add the syscalls to the table, the kernel may take a long time to compile (8-10 minutes). Once you are done with these steps, subsequent rebuilds should be much faster.


Rebuilding the kernel
---------------------

To build any changes you made (that is, after you change any files you will submit), from the `project1` directory simply type:

```sh
make kernel
```


Running the modified kernel and the test programs
-------------------------------------------------

We will be using an x86-based version of the Linux kernel and the QEMU emulator for this project to run the modified kernel.

You can run QEMU on the thoth server by simply running `make debug` from the `project1` directory. This will build the kernel, the test programs, and then start QEMU with a prompt allowing you to pick which program to run:

```
1) trafficsim 2) trafficsim-mutex 3) trafficsim-strict-order
Select an option:
```

**To exit QEMU, press Ctrl+A, then x.**


Debugging the modified kernel
-----------------------------

You can use GDB to debug the modified kernel. The following instructions assume you run QEMU on thoth.

1. `cd` into `project1` folder
2. `make debug`
3. Open up a new SSH connection to thoth.
4. `cd` into `project1` folder
5. `make debug-gdb`

Steps 2 and 5 have to be repeated every time you modify the kernel sources.

Feel free to use your GDB skills. For example, you can place a breakpoint at a given function or code line in the kernel source code. 

```
Reading symbols from linux-5.10.10/vmlinux...
Remote debugging using localhost:26000
0xc11578b6 in default_idle () at ./arch/x86/include/asm/irqflags.h:60
60		asm volatile("sti; hlt": : :"memory");
(gdb) b cs1550.c:23
(gdb) c
Continuing.
```

To debug a user program (e.g., `trafficsim`), you can add the symbol table inside the user program to the GDB session as follows:

```
(gdb) add-symbol-file initramfs/trafficsim 0x08049000
add symbol table from file "initramfs/trafficsim" at
	.text_addr = 0x8049000
(y or n) y
Reading symbols from initramfs/trafficsim...
```

You will need to replace the `0x8049000` value by the address of the .text section; you can get the address by running `readelf -WS initramfs/trafficsim`. You can then add breakpoints in the user program:

```
(gdb) b trafficsim.c:main
Breakpoint 2 at 0x8049cf5: file trafficsim.c, line 17.
(gdb) c
Continuing.

Breakpoint 2, main () at trafficsim.c:17
17	{
```

By using the `next` command in the GDB prompt (`n`) you can trace inside kernel code and out to the user code.

Inside GDB, you can use `layout src` to enable a table UI that shows the source code of the file you are currently debugging.

Here is a nice reference for gdb commands: https://visualgdb.com/gdbreference/commands/


File Backups
------------

The `/u/OSLab/` partition is **not** part of AFS space. Thus, any files you modify under these folders are not backed up on Thoth. If there is a catastrophic disk failure, all of your work will be irrecoverably lost. As such, it is our recommendation that you:

**Commit and push all the files you change to your GitHub repository frequently!**

**BE FOREWARNED:** Loss of work not backed up is not grounds for an extension.


Submission and Writeup
----------------------

We will use an automatic grader for Project 1. It runs the same code as `make test`. You can test your code on the autograder before the deadline. You get unlimited attempts until the deadline. It takes about five minutes to grade your solution. You need to submit your GitHub repository into Gradescope. **You may not change the folder structure and file locations**.

- Your repository should also contain a short report in [GitHub Markdown](https://guides.github.com/features/mastering-markdown/) named `p1-writeup.md` at the root of the repository that answers the following question:
__Explain the pros and cons of using a FIFO queue (as compared to a hash table) for the global semaphore list.__


Grading rubric
--------------

The rubric items can be found on the project submission page on Gradescope. Non-compiling code gets **zero** points. 

|Item|Grade|
|--|--|
|Test cases on the autograder|60%|
|Correct implementation of the global semaphore list, using kmalloc and kfree|5%|
|Correct implementation of the process queue, using kmalloc and kfree|5%|
|Correct usage of the spinlock and rwlock|5%|
|Defensive programming to catch invalid initialization of the semaphore|5%|
|Explanatory comments and style|10%|
|Report|10%|

**If the autograder score is 24 points or less (out of 60), a partial grade will be assigned using manual grading of your code. The maximum autograder score you can get in this manual grading is 40 points. Again, non-compiling code gets **zero** points.**

Please note that the score that you get from the autograder is not your final score. We still do manual grading. We may discover bugs and mistakes that were not caught by the test scripts and take penalty points off. Please use the autograder (or the local test harness `make test`) only as a tool to get immediate feedback and discover bugs in your program. Please note that certain bugs (e.g, deadlocks) in your program may or may not manifest themselves when the test cases are run on your program. It may be the case that the same exact code fails in some tests then the same code passes the same tests when resubmitted. The fact that a test once fails should make you debug your code, not simply resubmit and hope that the situation that caused the bug won't happen the next time around.

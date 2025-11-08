# RTOS
The goal of this project is write an RTOS solution for an M4F controller that implements a preemptive RTOS solution with support for mutexes, semaphores, yielding, sleep, priority scheduling, memory protection, and a shell interface.

Scheduler:
Each time the scheduler is called, it will look at all ready threads and will choose the next task to execute. Modify the task to add prioritization to 8 levels (0 highest to 7 lowest).
Note: The method used to prioritize the relative importance of the tasks is implicit in the assignment of the prioritization when createThread() is called.

## Kernel Functions:
Add a function yield() that will yield execution back to the kernel that will save the context necessary for the resuming the task later.

Add a function sleep(time_ms) and supporting kernel code that will mark the task as delayed and save the context necessary for the resuming the task later. The task is then delayed until a kernel determines that a period of time_ms has expired. Once the time has expired, the task that called sleep(time_ms) will be marked as ready so that the scheduler can resume the task later.

Add a function wait(semaphore) and supporting kernel code that decrements the semaphore count and returns if a resource is available or, if not available, marks the task as blocked, and records the task in the semaphore process queue.

Add a function post(semaphore) and supporting kernel code that increments the semaphore count. If a process is waiting in the queue, decrement the count and mark the task as ready.

Add a function lock(mutex) and supporting kernel code locks the mutex and returns if a resource is available or, if not available, marks the task as blocked on a mutex, and records the task in the mutex process queue.

Add a function unlock(mutex) and supporting kernel code unlocks the mutex. Only the thread that locked a mutex can unlock it.

Modify the function createThread() to store the task name, set the state to unrun, and initialize the task stack as needed. You must design the method for allocating memory space for the task stacks.

Add a function killThread() that marks the task as killed in the TCB. Memory must be freed. In implementing the above kernel functions, code the function systickIsr() to handle the sleep timing and kernel functions. The code to switch task should reside in the pendSvcIsr() function.

Add a shell process that hosts a command line interface to the PC. The command-line interface should support the following commands (many borrowing from UNIX):

ps: The PID id, process (actually thread) name, sleep time remaining is applicable, blocked resource if applicable, and % of CPU time should be stored at a minimum.

ipcs: At a minimum, the semaphore and mutex usage should be displayed.

kill <PID>:This command allows a task to be killed, by referencing the process ID.

pkill <Process_Name>: This command kills a task with the matching process name.

reboot: The command restarts the processor.

pidof <Process_Name> returns the PID of a task.

run <Process_Name> starts a task running in the background if not already running. Only one instance of a named task is allowed. The stack will be recreated for a task if is was not already running.

preempt ON|OFF turns preemption on or off. The default is preemption on.

sched PRIO|RR selects priority or round-robin scheduling. The default is priority scheduling.
// Kernel functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "mm.h"
#include "kernel.h"
#include "asm.h"
#include "uart0.h"

//-----------------------------------------------------------------------------
// RTOS Defines and Kernel Variables
//-----------------------------------------------------------------------------

// mutex
typedef struct _mutex
{
    bool lock;
    uint8_t queueSize;
    uint8_t processQueue[MAX_MUTEX_QUEUE_SIZE];
    uint8_t lockedBy;
} mutex;
mutex mutexes[MAX_MUTEXES];

// semaphore
typedef struct _semaphore
{
    uint8_t count;
    uint8_t queueSize;
    uint8_t processQueue[MAX_SEMAPHORE_QUEUE_SIZE];
} semaphore;
semaphore semaphores[MAX_SEMAPHORES];

// task states
#define STATE_INVALID           0 // no task
#define STATE_UNRUN             1 // task has never been run
#define STATE_READY             2 // has run, can resume at any time
#define STATE_DELAYED           3 // has run, but now awaiting timer
#define STATE_BLOCKED_SEMAPHORE 4 // has run, but now blocked by semaphore
#define STATE_BLOCKED_MUTEX     5 // has run, but now blocked by mutex
#define STATE_KILLED            6 // task has been killed

// task
uint8_t taskCurrent = 0;          // index of last dispatched task
uint8_t taskCount = 0;            // total number of valid tasks

// control
bool priorityScheduler = true;    // priority (true) or round-robin (false)
bool priorityInheritance = false; // priority inheritance for mutexes
bool preemption = false;          // preemption (true) or cooperative (false)

// tcb
#define NUM_PRIORITIES   8
struct _tcb
{
    uint8_t state;                 // see STATE_ values above
    void *pid;                     // used to uniquely identify thread (add of task fn)
    void *sp;                      // current stack pointer
    uint8_t priority;              // 0=highest
    uint8_t currentPriority;       // 0=highest (needed for pi)
    uint32_t ticks;                // ticks until sleep complete
    uint64_t srd;                  // MPU subregion disable bits
    char name[16];                 // name of task used in ps command
    uint8_t mutex;                 // index of the mutex in use or blocking the thread
    uint8_t semaphore;             // index of the semaphore that is blocking the thread
} tcb[MAX_TASKS];

/* from kernel.h:
// function pointer
typedef void (*_fn)();

// mutex
#define MAX_MUTEXES 1
#define MAX_MUTEX_QUEUE_SIZE 2
#define resource 0

// semaphore
#define MAX_SEMAPHORES 3
#define MAX_SEMAPHORE_QUEUE_SIZE 2
#define keyPressed 0
#define keyReleased 1
#define flashReq 2

// tasks
#define MAX_TASKS 12
*/

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

bool initMutex(uint8_t mutex)
{
    bool ok = (mutex < MAX_MUTEXES);
    if (ok)
    {
        mutexes[mutex].lock = false;
        mutexes[mutex].lockedBy = 0;
    }
    return ok;
}

bool initSemaphore(uint8_t semaphore, uint8_t count)
{
    bool ok = (semaphore < MAX_SEMAPHORES);
    {
        semaphores[semaphore].count = count;
    }
    return ok;
}

// REQUIRED: initialize systick for 1ms system timer
void initRtos(void)
{
    uint8_t i;
    // no tasks running
    taskCount = 0;
    // clear out tcb records
    for (i = 0; i < MAX_TASKS; i++)
    {
        tcb[i].state = STATE_INVALID;
        tcb[i].pid = 0;
    }
}

// REQUIRED: Implement prioritization to NUM_PRIORITIES
// loop through tcb and return index of next task to run
uint8_t rtosScheduler(void)
{
    if (priorityScheduler)
    {
        // priority based scheduling 
        uint8_t highestPriority = 8; // higher than max
        uint8_t selectedTask = 0xFF; // invalid
        // find highest priority ready task
        int i;
        for (i = 0; i < MAX_TASKS; i++)
        {
            if ((tcb[i].state == STATE_READY || tcb[i].state == STATE_UNRUN) && (tcb[i].priority < highestPriority))
            {
                highestPriority = tcb[i].priority;
                selectedTask = i;
            }
        }
        taskCurrent = selectedTask;
        return selectedTask;
    }
    else
    {
        // round robin scheduling
        bool ok = false;
        uint8_t task = taskCurrent; // start searching from next task
        while (!ok)
        {
            task++;
            if (task >= taskCount) task = 0;
            ok = (tcb[task].state == STATE_READY || tcb[task].state == STATE_UNRUN);
        }
        taskCurrent = task;
        return task;
    }
}

// REQUIRED: modify this function to start the operating system
// by calling scheduler, set srd bits, setting PSP, ASP bit, call fn with fn add in R0
// fn set TMPL bit, and PC <= fn
void startRtos(void)
{
    uint8_t task = rtosScheduler();
    // set srd bits
    applySramAccessMask(tcb[task].srd);
    // set PSP
    setPsp(tcb[task].sp);
    // set ASP bit
    setAspOn();
    // TODO: 
    // call fn with fn add in R0
    // fn set TMPL bit
    // and PC <= fn
}

// REQUIRED:
// add task if room in task list
// store the thread name
// allocate stack space and store top of stack in sp and spInit
// set the srd bits based on the memory allocation
bool createThread(_fn fn, const char name[], uint8_t priority, uint32_t stackBytes)
{
    bool ok = false;
    uint8_t i = 0;
    bool found = false;
    if (taskCount < MAX_TASKS)
    {
        // make sure fn not already in list (prevent redundancy)
        while (!found && (i < taskCount))
        {
            found = (tcb[i].pid == fn);
            i++;
        }
        if (!found)
        {
            // find first available tcb record
            i = 0;
            while (tcb[i].state != STATE_INVALID) {i++;}
            tcb[i].state = STATE_UNRUN;
            tcb[i].pid = fn;
            tcb[i].priority = priority;
            // copy name
            uint8_t j;
            for (j = 0; j < 15 && name[j] != 0; j++)
            {
                tcb[i].name[j] = name[j];
            }
            tcb[i].sp = mallocHeap(i, stackBytes); // returns pointer to start of block
            // tcb[i].srd updated in malloc function
            // make hw stack frame for first run
            uint32_t *sp = (uint32_t *) tcb[i].sp;
            sp[0] = 0;                     // r0
            sp[1] = 0;                     // r1
            sp[2] = 0;                     // r2
            sp[3] = 0;                     // r3
            sp[4] = 0;                     // r12
            sp[5] = 0xFFFFFFFD;            // LR (return to thread mode using PSP)
            sp[6] = (uint32_t) fn;         // PC (start address)
            sp[7] = 0x01000000;            // xPSR (Thumb bit set)

            taskCount++;
            ok = true;
        }
    }
    return ok;
}

// REQUIRED: modify this function to kill a thread
// REQUIRED: free memory, remove any pending semaphore waiting,
//           unlock any mutexes, mark state as killed
void killThread(_fn fn)
{
}

// REQUIRED: modify this function to restart a thread, including creating a stack
void restartThread(_fn fn)
{
}

// REQUIRED: modify this function to set a thread priority
void setThreadPriority(_fn fn, uint8_t priority)
{
}

// REQUIRED: modify this function to yield execution back to scheduler using pendsv
// kernel will switch tasks while saveing the context necessary for the resuming later
void yield(void)
{
    // thread is unprivileged, so svc is needed to call pendsv (only available in priv mode)
    // svc pushes the task registers onto the PSP for context saving
    asm("svc 0");
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses using pendsv
void sleep(uint32_t tick)
{
    tcb[taskCurrent].ticks += tick;
    tcb[taskCurrent].state = STATE_DELAYED;
    // switch to another task while sleeping
    yield();
}

// REQUIRED: modify this function to wait a semaphore using pendsv
void wait(int8_t semaphore)
{
    // if semaphore is available, decrement count
    if (semaphores[semaphore].count > 0)
    {
        semaphores[semaphore].count--;
    }
    else
    {
        // otherwise, block the task
        tcb[taskCurrent].state = STATE_BLOCKED_SEMAPHORE;
        tcb[taskCurrent].semaphore = semaphore;
        // add to semaphore queue if room available
        uint8_t qSize = semaphores[semaphore].queueSize;
        if (qSize < MAX_SEMAPHORE_QUEUE_SIZE)
        {
            semaphores[semaphore].processQueue[qSize] = taskCurrent;
            semaphores[semaphore].queueSize++;
        }
    }
}

// REQUIRED: modify this function to signal a semaphore is available using pendsv
void post(int8_t semaphore)
{
    // if queue is not empty, give to next task
    if (semaphores[semaphore].queueSize > 0)
    {
        uint8_t nextTask = semaphores[semaphore].processQueue[0];
        // shift queue
        uint8_t i;
        for (i = 1; i < semaphores[semaphore].queueSize; i++)
        {
            semaphores[semaphore].processQueue[i - 1] = semaphores[semaphore].processQueue[i];
        }
        semaphores[semaphore].queueSize--;
        tcb[nextTask].state = STATE_READY;
    }
    else
    {
        semaphores[semaphore].count++;
    }
}

// REQUIRED: modify this function to lock a mutex using pendsv
void lock(int8_t mutex)
{
    // if mutex is available, lock it
    if (!mutexes[mutex].lock)
    {
        mutexes[mutex].lock = true;
        mutexes[mutex].lockedBy = taskCurrent;
    }
    else
    {
        // otherwise, block the task
        tcb[taskCurrent].state = STATE_BLOCKED_MUTEX;
        tcb[taskCurrent].mutex = mutex;
        // add to mutex queue if room available
        uint8_t qSize = mutexes[mutex].queueSize;
        if (qSize < MAX_MUTEX_QUEUE_SIZE)
        {
            mutexes[mutex].processQueue[qSize] = taskCurrent;
            mutexes[mutex].queueSize++;
        }
    }
}

// REQUIRED: modify this function to unlock a mutex using pendsv
void unlock(int8_t mutex)
{
    // only the locking task can unlock
    if (mutexes[mutex].lockedBy == taskCurrent)
    {
        // if queue is not empty, give to next task
        if (mutexes[mutex].queueSize > 0)
        {
            uint8_t nextTask = mutexes[mutex].processQueue[0];
            // shift queue
            uint8_t i;
            for (i = 1; i < mutexes[mutex].queueSize; i++)
            {
                mutexes[mutex].processQueue[i - 1] = mutexes[mutex].processQueue[i];
            }
            mutexes[mutex].queueSize--;
            // give mutex to next task
            mutexes[mutex].lockedBy = nextTask;
            tcb[nextTask].state = STATE_READY;
        }
        else
        {
            // no one waiting, just unlock
            mutexes[mutex].lock = false;
            mutexes[mutex].lockedBy = 0;
        }
    }
}

// REQUIRED: modify this function to add support for the system timer
// REQUIRED: in preemptive code, add code to request task switch
void systickIsr(void)
{
    // with each tick passing, decrement ticks for delayed tasks to call back
    uint8_t i;
    for (i = 0; i < taskCount; i++)
    {
        if (tcb[i].state == STATE_DELAYED && tcb[i].ticks > 0)
        {
            tcb[i].ticks--;
            if (tcb[i].ticks == 0)
            {
                tcb[i].state = STATE_READY;
            }
        }
    }
}

// REQUIRED: in coop and preemptive, modify this function to add support for task switching
// REQUIRED: process UNRUN and READY tasks differently

// 1. push the registers: r4-r11 in the stack to bring back the context of the task later
// 2. save the PSP in tcb, original sp will be shifted to make room for the registers
// 3. call the scheduler to get the next task
// 4. set the PSP to the new task's sp
// 5. if task is unrun, set up the stack
// 6. if task is ready, retrieve r4-r11 from stack
// 8. set the srd bits for the new task

void pendSvIsr(void)
{
    // get stack pointer, has HW: r0-r3, r12, LR, PC, xPSR
    uint32_t *sp = getPsp();
    // save SW: r4-r11
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    asm("mov %0, r4"  : "=r" (r4));
    asm("mov %0, r5"  : "=r" (r5));
    asm("mov %0, r6"  : "=r" (r6));
    asm("mov %0, r7"  : "=r" (r7));
    asm("mov %0, r8"  : "=r" (r8));
    asm("mov %0, r9"  : "=r" (r9));
    asm("mov %0, r10" : "=r" (r10));
    asm("mov %0, r11" : "=r" (r11));
    // store on stack
    sp -= 8; // make room for r4-r11
    sp[0] = r4;
    sp[1] = r5;
    sp[2] = r6;
    sp[3] = r7;
    sp[4] = r8;
    sp[5] = r9;
    sp[6] = r10;
    sp[7] = r11;
    // save the stack pointer pointing to r4-r11
    tcb[taskCurrent].sp = (void *) sp;
    // update state (not if blocked or delayed)
    if (tcb[taskCurrent].state == STATE_UNRUN || tcb[taskCurrent].state == STATE_READY)
    {
        tcb[taskCurrent].state = STATE_READY;
    }

    // get next task
    uint8_t task = rtosScheduler();
    
    // if its been run before, pop r4-11 values
    if (tcb[task].state == STATE_READY)
    {
        uint32_t *sp = (uint32_t *) tcb[task].sp;
        uint32_t r4 = sp[0];
        uint32_t r5 = sp[1];
        uint32_t r6 = sp[2];
        uint32_t r7 = sp[3];
        uint32_t r8 = sp[4];
        uint32_t r9 = sp[5];
        uint32_t r10 = sp[6];
        uint32_t r11 = sp[7];
        asm("mov r4,  %0" : : "r" (r4));
        asm("mov r5,  %0" : : "r" (r5));
        asm("mov r6,  %0" : : "r" (r6));
        asm("mov r7,  %0" : : "r" (r7));
        asm("mov r8,  %0" : : "r" (r8));
        asm("mov r9,  %0" : : "r" (r9));
        asm("mov r10, %0" : : "r" (r10));
        asm("mov r11, %0" : : "r" (r11));
        tcb[task].sp = (void *) (sp + 8); // adjust sp to remove r4-r11
    }
    // set srd bits
    applySramAccessMask(tcb[task].srd);
    // set PSP
    setPsp(tcb[task].sp);
}

// REQUIRED: modify this function to add support for the service call
// REQUIRED: in preemptive code, add code to handle synchronization primitives
void svCallIsr(void)
{
    uint32_t svcNumber = 0;
    // the immediate in the svc is 2 bytes back from the link register? (PC at svc call time)
    // do i even need sv number ??

    if (svcNumber == 0) // yield
    {
        NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV; // trigger pendsv
    }
}
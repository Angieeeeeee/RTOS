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
#include "faults.h"
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
//struct _tcb
//{
//    uint8_t state;                 // see STATE_ values above
//    void *pid;                     // used to uniquely identify thread (add of task fn)
//    void *sp;                      // current stack pointer
//    uint8_t priority;              // 0=highest
//    uint8_t currentPriority;       // 0=highest (needed for pi)
//    uint32_t ticks;                // ticks until sleep complete
//    uint64_t srd;                  // MPU subregion disable bits
//    char name[16];                 // name of task used in ps command
//    uint8_t mutex;                 // index of the mutex in use or blocking the thread
//    uint8_t semaphore;             // index of the semaphore that is blocking the thread
//} tcb[MAX_TASKS];

struct _tcb tcb[MAX_TASKS];

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
        for (i = 0; i < taskCount; i++)
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

    putsUart0("First task PSP = ");
    putsUart0(inttohex((uint32_t)tcb[task].sp));
    putcUart0('\n');

    // set PSP
    setPsp(tcb[task].sp);
    printStack(tcb[task].sp);

    // set ASP bit
    setAspOn();

    // jump to thread
    _fn entry = (_fn)tcb[task].pid;
    setPrivOff();
    entry();              // jump to task; never returns

    // TODO: ?
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
            taskCurrent = i;
            tcb[i].state = STATE_UNRUN;
            tcb[i].pid = fn;
            tcb[i].priority = priority;
            // copy name
            uint8_t j;
            for (j = 0; j < 15 && name[j] != 0; j++)
            {
                tcb[i].name[j] = name[j];
            }

            uint32_t *sp = (uint32_t *)mallocHeap(stackBytes); // top of block allocated

            // tcb[i].srd applied inside malloc(addSramAccessWindow)
            // make hw stack frame for first run
            *(--sp) = 0x01000000;               // xPSR
            *(--sp) = (uint32_t)fn;             // PC
            *(--sp) = 0;                        // LR
            *(--sp) = 0;                        // R12
            *(--sp) = 0;                        // R3
            *(--sp) = 0;                        // R2
            *(--sp) = 0;                        // R1
            *(--sp) = 0;                        // R0
            printStack(sp);
            tcb[i].sp = (void *) sp; // pointer to spot R0 is in

            taskCount++;
            ok = true;
        }
    }
    return ok;
}

// REQUIRED: modify this function to kill a thread
// REQUIRED: free memory, reMOVe any pending semaphore waiting,
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
    __asm("    SVC #0");
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses using pendsv
void sleep(uint32_t tick)
{
    __asm("    SVC #1");
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
// 5. if task is unrun, use initial stack made in createThread
// 6. if task is ready, retrieve r4-r11 from stack
// 8. set the srd bits for the new task

void pendSvIsr(void)
{
    //putsUart0("inside pendSvIsr \n");
    // get stack pointer, has HW: r0-r3, r12, LR, PC, xPSR
    uint32_t *sp = getPsp();

//    putsUart0("1. hw\n");
//    printStack(sp);

    // push r4-11 under stack
    sp = pushSW(sp);

//    putsUart0("2. sw\n");
//    printStack(sp);

    // update the actual stack
    setPsp(sp);

    // save the updated pointer
    tcb[taskCurrent].sp = (void *) sp;

    // update state (not if blocked or delayed)
    if (tcb[taskCurrent].state == STATE_UNRUN || tcb[taskCurrent].state == STATE_READY)
    {
        tcb[taskCurrent].state = STATE_READY;
    }

    // get next task
    uint8_t task = rtosScheduler();

    // psp for next stack
    sp = (uint32_t *) tcb[task].sp;

    // if its been run before, pop r4-11 values
    if (tcb[task].state == STATE_READY)
    {
//        putsUart0("3. recovered\n");
//        printStack(sp);
        // set r4-11 from the stack
        sp = popSW(sp);
    }
//    putsUart0("4. applied\n");
//    printStack(sp);
    // if unrun the hw stack is made in createThread()
    tcb[task].sp = (void *) sp;
    // set srd bits
    applySramAccessMask(tcb[task].srd);
    // set PSP
    setPsp(tcb[task].sp);

    __asm volatile(
        " MOVW   r0, #0xFFFD     \n"
        " MOVT   r0, #0xFFFF     \n"
        " MOV    LR, r0          \n"
        " BX     LR              \n"
    );
}

// REQUIRED: modify this function to add support for the service call
// REQUIRED: in preemptive code, add code to handle synchronization primitives
void svCallIsr(void)
{
    uint32_t *stacked = getPsp();
    uint32_t stackedPc = stacked[6];
    // svc num is in pc - 2
    uint8_t svcNumber = *((uint8_t *)(stackedPc - 2));

    void *arg = (void *)stacked[0]; // r0

    //putsUart0("svc number found is "); putsUart0(uitoa(svcNumber));

    if (svcNumber == 0) // yield
    {
        NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV; // trigger pendsv
    }
    else if (svcNumber == 1) // sleep
    {
            //tcb[taskCurrent].ticks = (unit32_t) arg;
            //tcb[taskCurrent].state = STATE_DELAYED;
            // save context
            // sth about systick
    }
}


// name pid state sp srd priority
void printTcb(void)
{
    putsUart0("Name: PID State SP SRD Priority\n");
    uint8_t i;
    for (i = 0; i < taskCount; i++)
    {
        putsUart0(tcb[i].name);
        putsUart0(": ");
        putsUart0(uitoa(tcb[i].pid));
        putsUart0(" ");
        switch (tcb[i].state)
        {
            case STATE_INVALID:
                putsUart0("invalid");
                break;
            case STATE_UNRUN:
                putsUart0("unrun");
                break;
            case STATE_READY:
                putsUart0("ready");
                break;
            case STATE_DELAYED:
                putsUart0("delayed");
                break;
            case STATE_BLOCKED_SEMAPHORE:
                putsUart0("blocked by semaphore");
                break;
            case STATE_BLOCKED_MUTEX:
                putsUart0("blocked by mutex");
                break;
            case STATE_KILLED:
                putsUart0("killed");
                break;
        }
        putsUart0(" ");
        putsUart0(uitoa((uint32_t)tcb[i].sp));
        putsUart0(" ");
        putsUart0(uitoa(tcb[i].srd));
        putsUart0(" ");
        putsUart0(uitoa(tcb[i].priority));
        putsUart0("\n");
    }
}

// check whats inside addresses
void printStack(void *sp)
{
    // print sp and 8 words above
    uint32_t *stack = (uint32_t *) sp;
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        putsUart0(inttohex((uint32_t)stack));
        putsUart0(": ");
        putsUart0(inttohex(*stack));
        putsUart0("\n");
        stack++;
    }
}
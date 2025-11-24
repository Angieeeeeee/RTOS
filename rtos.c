// RTOS Framework - Fall 2025
// J Losh

// Student Name: Angelina Abuhilal

// Please do not change any function name in this code or the thread priorities

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL Evaluation Board
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// 6 Pushbuttons and 5 LEDs, UART
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port
//   Configured to 115,200 baud, 8N1
// Memory Protection Unit (MPU):
//   Region to control access to flash, peripherals, and bitbanded areas
//   4 or more regions to allow SRAM access (RW or none for task)

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include "tm4c123gh6pm.h"
#include "clock.h"
#include "gpio.h"
#include "uart0.h"
#include "wait.h"
#include "mm.h"
#include "kernel.h"
#include "faults.h"
#include "tasks.h"
#include "shell.h"

// function to test buttons and leds
void testHW(void)
{
    uint8_t buttons;
    while(true)
    {
        buttons = readPbs();
        if (buttons == 0)
        {
            setPinValue(RED_LED, 1);
            waitMicrosecond(1000);
            setPinValue(RED_LED, 0);
        }
        if (buttons == 1)
        {
            setPinValue(ORANGE_LED, 1);
            waitMicrosecond(1000);
            setPinValue(ORANGE_LED, 0);
        }
        if (buttons == 2)
        {
            setPinValue(YELLOW_LED, 1);
            waitMicrosecond(1000);
            setPinValue(YELLOW_LED, 0);
        }
        if (buttons == 3)
        {
            setPinValue(GREEN_LED, 1);
            waitMicrosecond(1000);
            setPinValue(GREEN_LED, 0);
        }
        if (buttons == 4)
        {
            setPinValue(BLUE_LED, 1);
            waitMicrosecond(1000);
            setPinValue(BLUE_LED, 0);
        }
        if (buttons == 5)
        {
            setPinValue(BLUE_LED, 1);
            setPinValue(GREEN_LED, 1);
            waitMicrosecond(1000);
            setPinValue(BLUE_LED, 0);
            setPinValue(GREEN_LED, 0);
        }
        if (buttons == 6)
        {
            setPinValue(BLUE_LED, 0);
            setPinValue(GREEN_LED, 0);
            setPinValue(YELLOW_LED, 0);
            setPinValue(ORANGE_LED, 0);
            setPinValue(RED_LED, 0);
        }
    }
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(void)
{
    bool ok;

    // Initialize hardware
    initSystemClockTo40Mhz();
    initHw();
    //testHW(); //works
    initUart0();
    initMemoryManager();
    initMpu();
    initRtos();

    // Setup UART0 baud rate
    setUart0BaudRate(115200, 40e6);

    // Initialize mutexes and semaphores
    initMutex(resource);
    initSemaphore(keyPressed, 1);
    initSemaphore(keyReleased, 0);
    initSemaphore(flashReq, 5);

    // Add required idle process at lowest priority
    ok =  createThread(idle, "Idle", 7, 512);
    ok &=  createThread(idle2, "Idle2", 7, 512);
    ok &=  createThread(idle3, "Idle3", 7, 512);
    // Add other processes
//    ok &= createThread(lengthyFn, "LengthyFn", 6, 1024); // lock and unlock
//    ok &= createThread(flash4Hz, "Flash4Hz", 4, 512);    // sleep
//    ok &= createThread(oneshot, "OneShot", 2, 1024);     // wait and sleep
//    ok &= createThread(readKeys, "ReadKeys", 6, 512);    // everything
//    ok &= createThread(debounce, "Debounce", 6, 1024);   // wait, sleep, and post
//    ok &= createThread(important, "Important", 0, 1024); // lock, sleep, unlock
//    ok &= createThread(uncooperative, "Uncoop", 6, 1024);// while (readPbs==8)
//    ok &= createThread(errant, "Errant", 6, 1024);       // write to 0x2000000000 (shouldnt be able to)
//    ok &= createThread(shell, "Shell", 6, 4096);

    printTcb();
    dumpHeap();

    // Start up RTOS
    if (ok)
        startRtos(); // never returns
    else
        while(true);
}

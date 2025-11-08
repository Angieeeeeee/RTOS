// Memory manager functions
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
#include "asm.h"
#include "uart0.h"

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

uint64_t srdBitmask = 0x0000000000000000;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// REQUIRED: add your malloc code here and update the SRD bits for the current thread
void * mallocHeap(uint32_t size_in_bytes)
{
    if (!size_in_bytes || (size_in_bytes > 0x00002000)) return NULL;     // null if size zero or greater than a region

    int blocks = size_in_bytes / BLOCK_SIZE;
    if (size_in_bytes % BLOCK_SIZE > 0) blocks ++; // round up

    // look through table to see if there are that number of consecutive blocks that are free
    int i;
    for (i = 0; i < NUM_BLOCKS; i++)
    {
        if (blockArray[i].alloc) continue; // skip used blocks

        // i = 0->3 Region 0
        // i = 4->11 Region 1
        // i = 12->19 Region 2
        // i = 20->27 Region 3
        int startRegion = ((i - 4) / 8) + 1;
        if (i < 4) startRegion = 0;
        int freeCount = 1;

        // check subsequent blocks
        int j;
        for (j = i + 1; j < NUM_BLOCKS && freeCount < blocks; j++)
        {
            int jregion = ((j - 4) / 8) + 1;
            if (j < 4) jregion = 0;
            // stop if block isnt free or crosses region boundary
            if (blockArray[j].alloc || jregion != startRegion) break;
            freeCount++;
        }

        if (freeCount == blocks)
        {
            // populate BLOCK table
            int k;
            for (k = i; k < i + blocks; k++)
            {
                blockArray[k].alloc = true;
                blockArray[k].owner = pid;
                blockArray[k].size = blocks;
            }
            // make those blocks have SRD bits 1 (RW access)
            addSramAccessWindow(&srdBitmask, (void *)(HEAP_START + (i * BLOCK_SIZE)), blocks * BLOCK_SIZE);
            applySramAccessMask(srdBitmask);
            return (void *)(HEAP_START + (i * BLOCK_SIZE)); // pointer to start address in mem
        }

        i += freeCount - 1; // if blocks not found, skip ahead to past the checked blocks
    }
    return NULL; // failed to find space
}

// REQUIRED: add your free code here and update the SRD bits for the current thread
void freeHeap(void *address_from_malloc)
{
    int blockIndex = ((uint32_t)p - HEAP_START) / BLOCK_SIZE;

    if (blockIndex < 0 || blockIndex >= NUM_BLOCKS) return; // check if bad pointer, out of heap range
    if (blockArray[blockIndex].owner != pid || !blockArray[blockIndex].alloc) return; // not the owner of the memory or not allocated anyways

    int size = blockArray[blockIndex].size;

    // free blocks owned by pid starting at index within region
    int i;
    for (i = blockIndex; (i - blockIndex) < size ; i++)
    {
        blockArray[i].alloc = false;
        blockArray[i].owner = 0;
        blockArray[i].size = 0;
        // set that block to 0 in srdBitmask
        srdBitmask &= ~((uint64_t)(1 << (i + 4))); // makes 0 no RW access for unpriv
    }
    applySramAccessMask(srdBitmask);
}

// REQUIRED: add code to initialize the memory manager
void initMemoryManager(void)
{

}

// REQUIRED: add your custom MPU functions here (eg to return the srd bits)
// background rule allowing RWX for priv and unpriv
void setBackgroundRule(void)
{
    NVIC_MPU_NUMBER_R = 0;
    NVIC_MPU_BASE_R = 0x00000000;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE | (31 << 1) | (0b11 << 24) | (1 << 28); // 4GB RW for priv and unpriv no X
}

// only read from flash for both priv and unpriv
void allowFlashAccess(void)
{
    NVIC_MPU_NUMBER_R = 5;
    NVIC_MPU_BASE_R = 0x00000000;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE | // Enable Region
                       (17 << 1) |            // Region Size: 256KB - 2^18 (0x00000000 -> 0x00040000)
                       (0b110 << 24);         // R only 
}

// take away access from private peripherals
void allowPeripheralAccess(void)
{
    NVIC_MPU_NUMBER_R = 6;
    NVIC_MPU_BASE_R = 0xE0000000;                   // Base Address of Peripherals 
    NVIC_MPU_ATTR_R |=   NVIC_MPU_ATTR_ENABLE |     // Enable Region
                        (28 << 1) |                 // Region Size: 512MB - 2^29 (0xE0000000 -> 0x100000000)
                        (0b001 << 24) |             // RW only for priv
                        (1 << 28);                  // XN: Execute Never
}

void setupSramAccess(void)
{
    // makes multiple MPU regions 8KiB, with 8 subregions of 1KiB
    // RW access for priv, no acces to unpriv 0b1
    // disable subregions at start
    // size = 2^(SIZE+1) so 8KiB = 13 -> SIZE = 12

    NVIC_MPU_NUMBER_R = 1;
    NVIC_MPU_BASE_R = 0x20000000;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE | (12 << 1) | (0b001 << 24);// | (0xFF << 8);
    NVIC_MPU_ATTR_R &= ~(0xFF << 8); // enable region initially, make it 0

    NVIC_MPU_NUMBER_R = 2;
    NVIC_MPU_BASE_R = 0x20002000;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE | (12 << 1) | (0b001 << 24);// | (0xFF << 8);
    NVIC_MPU_ATTR_R &= ~(0xFF << 8); // enable region initially

    NVIC_MPU_NUMBER_R = 3;
    NVIC_MPU_BASE_R = 0x20004000;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE | (12 << 1) | (0b001 << 24);// | (0xFF << 8);
    NVIC_MPU_ATTR_R &= ~(0xFF << 8); // enable region initially

    NVIC_MPU_NUMBER_R = 4;
    NVIC_MPU_BASE_R = 0x20006000;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE | (12 << 1) | (0b001 << 24);// | (0xFF << 8);
    NVIC_MPU_ATTR_R &= ~(0xFF << 8); // enable region initially
}

uint64_t createSramAccessMask(void)
{
    return 0x0000000000000000;   
    // when SRD = 1 for subregion then the access will fall to background rule (RW for priv and unpriv)
}

// applies the srdBitMask to all SRAM regions
void applySramAccessMask(uint64_t srdBitMask)
{
    NVIC_MPU_NUMBER_R = 1;
    NVIC_MPU_ATTR_R &= ~(0xFF << 8);
    NVIC_MPU_ATTR_R |= (uint32_t) (srdBitMask & 0xFF) << 8;  //R1: bits 0-7

    NVIC_MPU_NUMBER_R = 2;
    NVIC_MPU_ATTR_R &= ~(0xFF << 8);
    NVIC_MPU_ATTR_R |= (uint32_t) ((srdBitMask >> 8) & 0xFF) << 8;

    NVIC_MPU_NUMBER_R = 3;
    NVIC_MPU_ATTR_R &= ~(0xFF << 8);
    NVIC_MPU_ATTR_R |= (uint32_t) ((srdBitMask >> 16) & 0xFF) << 8;

    NVIC_MPU_NUMBER_R = 4;
    NVIC_MPU_ATTR_R &= ~(0xFF << 8);
    NVIC_MPU_ATTR_R |= (uint32_t) ((srdBitMask >> 24) & 0xFF) << 8;
}

// adds access to the requested SRAM address range
void addSramAccessWindow(uint64_t *srdBitMask, uint32_t *baseAdd, uint32_t size_in_bytes)
{
    if (size_in_bytes % 1024 != 0)
    {
        putsUart0("sram access window: size is wrong\n");
      return;
    }
    if ((uint32_t)baseAdd < 0x20001000 || (uint32_t)baseAdd + size_in_bytes > 0x20008000)
    {
        putsUart0("sram access window: incorrect range\n");
        return;
    }

    uint32_t start = ((uint32_t)baseAdd - 0x20000000) >> 10;                 // start subregion (find offset and divide)
    uint32_t end   = ((uint32_t)baseAdd - 0x20000000 + size_in_bytes) >> 10; // end subregion

    int i;
    for (i = start; i < end; i++)
    {
        *srdBitMask |= (uint64_t) 1 << i; // turns bit on at that subregion, gets RW access
    }
}


// REQUIRED: initialize MPU here
void initMpu(void)
{
    setBackgroundRule();    // RW for all, X for none
    allowFlashAccess();     // only R for all
    allowPeripheralAccess();// take away RW of priv peripheral from unpriv
    setupSramAccess();      // take away RW from unpriv

    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_ENABLE | NVIC_MPU_CTRL_PRIVDEFEN;

    setPsp((uint32_t *) 0x20008000);
    setAspOn();
/*
// trick to jump to the bottom of the heap
// fill all regions then free them, the pointer will be at the end of the heap
    uint32_t* p = malloc_heap(4000);      //fill r1
    if (!p) putsUart0("malloc failed\n");

    uint32_t* s = malloc_heap(8000);      //fill r2
    if (!s) putsUart0("malloc failed\n");

    uint32_t* q = malloc_heap(8000);      // fill r3
    if (!q) putsUart0("malloc failed\n");

    uint32_t* k = malloc_heap(7000);      // fill r4
    if (!k) putsUart0("malloc failed\n");

    uint32_t* a = malloc_heap(1000);      // very last block
    if (!a) putsUart0("malloc failed\n");

    free_heap(p);
    free_heap(s);
    free_heap(q);
    free_heap(k);
*/
}

/* NOTES TO SELF
 *
 * MPUBASE REGISTER offset 0xD9C
 *  [31:5]  ADDR    Bits 31:N (N = Log2(Region size in bytes)) the region base address. (N-1):5 are reserved.
 *     [4]  VALID   0 -> The MPUNUMBER not changed, ignore REGION
 *                  1 -> The MPUNUMBER is updated in REGION
 *   [2:0]  REGION  W -> contains value to be written in MPUNUMBER
 *                  R -> returns current region number to MPUNUMBER register
 *
 * MPUATTR REGISTER offset 0xDA0            p193
 *     [28] XN      0 -> instruction fetches are enabled
 *                  1 -> instruction fetches are disabled
 *  [26:24] AP      BITS  PRIV  UPRIV
 *                  000   0     0
 *                  001   RW    0
 *                  010   RW    R
 *                  011   RW    RW
 *                  101   R     0
 *                  110   R     R
 *                  111   R     R
 *  [21:19] TEX     |
 *     [18] S       |
 *     [17] C       |
 *     [16] B       |see table below
 *   [15:8] SRD     [7:0] each bit represents a subregion. 1 -> disable
 *    [5:1] SIZE    (Region size in bytes) = 2^(SIZE+1) ex. 32bytes would be 4 in size
 *      [0] ENABLE  0 -> region disabled
 *                  1 -> region enabled
 * ==========================================================================
 *               HEAP VISUALIZATION
 * ==========================================================================
 *  0x2000 8000  |----------------|
 *               |                |
 *               | 8kB - Region 4 |  8 subregions of 1024B each  - bits 32-39
 *  0x2000 6000  |----------------|
 *               |                |
 *               | 8kB - Region 3 |  8 subregions of 1024B each  - bits 24-31
 *  0x2000 4000  |----------------|
 *               |                |
 *               | 8kB - Region 2 |  8 subregions of 1024B each  - bits 16-23
 *  0x2000 2000  |----------------|
 *               | 8kB - Region 1 |
 *               | [4kB - OS]     |  8 subregions of 1024B each  - bits 8-15  [4kB of which (bits 8-11) are reserved for the OS]
 *  0x2000 0000  |----------------|
 *
 *
 *  0x1000 0000  |        ^       |
 *               | 4GB - Backgrnd |  8 subregions of 1024B each  - bits 0-7  RW for all
 *  0x0000 0000  |----------------|
 */
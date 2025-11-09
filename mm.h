// Memory manager functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef MM_H_
#define MM_H_

// keeping track if blocks were assigned and to which pid
typedef struct _BLOCK
{
    bool alloc;      // 1 allocated, 0 not allocated
    uint32_t owner;  // pid that owns it
    uint32_t size;
} BLOCK;

#define NUM_BLOCKS  (HEAP_SIZE / BLOCK_SIZE) //32 blocks

#define HEAP_START  0x20001000 // note: 0x20000000 -> 0x20001000 is for OS
#define HEAP_END    0x20008000
#define HEAP_SIZE   0x7000
#define BLOCK_SIZE  1024
#define NUM_BLOCKS  (HEAP_SIZE / BLOCK_SIZE) // heap is 32 but 28 usable

BLOCK blockArray[NUM_BLOCKS];

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void * mallocHeap(_fn fn, uint32_t size_in_bytes);
void freeHeap(_fn fn, void *address_from_malloc);
void initMemoryManager(void);
void setBackgroundRule(void);
void allowFlashAccess(void);
void allowPeripheralAccess(void);
void setupSramAccess(void);
uint64_t createSramAccessMask(void);
void applySramAccessMask(uint64_t srdBitMask);
void addSramAccessWindow(uint64_t *srdBitMask, uint32_t *baseAdd, uint32_t size_in_bytes);
void initMpu(void);

#endif

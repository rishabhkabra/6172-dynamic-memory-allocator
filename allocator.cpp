/**
 * Copyright (c) 2012 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <pthread.h>
#include <iostream>
#include "./allocator_interface.h"
#include "./memlib.h"
#include "./benchmarks/cpuinfo.h"

// All blocks must have a specified minimum alignment.
#define ALIGNMENT 8

// Rounds up to the nearest multiple of ALIGNMENT.
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

// The smallest aligned size that will hold a size_t value.
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

namespace my {

// A header that will precede every allocated memory block 
struct MemoryBlock {
  void * threadInfo; // a pointer to the shared information of the thread that owns this block
  uint32_t size; // size of the entire memory block including the header and the footer
  bool isFree; // flag indicating whether this memory block is in use or had been freed
  MemoryBlock * nextFreeBlock; // pointer to the next free block in the binned free list that this belongs to.
  MemoryBlock * previousFreeBlock; // pointer to the previous free block in the binned free list that this belongs to.
};

// Thread-specific variables that need to be shared with other threads
struct ThreadSharedInfo {
  pthread_mutex_t localLock;
  MemoryBlock * unbinnedBlocks;
};

// A footer that will follow ever allocated memory block
typedef uint32_t MemoryBlockFooter;

// The book-keeping overhead (header + footer) on a freed memory block
#define FREE_BLOCK_OVERHEAD (sizeof(MemoryBlock) + sizeof(MemoryBlockFooter))

// The book-keeping overhead (header + footer) on an allocated memory block
#define ALLOCATED_BLOCK_OVERHEAD (FREE_BLOCK_OVERHEAD - 2 * sizeof(MemoryBlock *))

// The minimum total block size (including overhead) of any memory block that can allocated
#define MINIMUM_ALLOCATED_BLOCK_SIZE ALIGN(FREE_BLOCK_OVERHEAD)

#define BIN_INDEX_THRESHOLD 1024
#define NUM_OF_BINS 150

// A minimum threshold of gained free space for which a memory block will be truncated before it is allocated
#define FREE_BLOCK_SPLIT_THRESHOLD 8

// The initial amount of memory that is made available to a thread's local heap when a thread is initialized
#define INITIAL_ALLOCATION_PER_THREAD 64

// Formula which, given a MemoryBlock pointer, returns the internal space address (of the MemoryBlock) that should be visible to the user
#define MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mbptr) ((void *) ((char *)(mbptr) + sizeof(MemoryBlock) - 2 * sizeof(MemoryBlock *)))

// Formula which, given a MemoryBlock pointer, returns a pointer to the MemoryBlock's own footer
#define MB_ADDRESS_TO_OWN_FOOTER_ADDRESS(mbptr) (MemoryBlockFooter *) ((char *) (mbptr) + (mbptr)->size - sizeof(MemoryBlockFooter))

// Formula which, given a MemoryBlock pointer, returns a pointer to the preceding MemoryBlock's footer
#define MB_ADDRESS_TO_PREVIOUS_FOOTER_ADDRESS(mbptr) (MemoryBlockFooter *)((char *) (mbptr) - sizeof(MemoryBlockFooter))

// Formula which, given a pointer to the beginning of an internal allocated space, returns the corresponding MemoryBlock pointer
#define INTERNAL_SPACE_ADDRESS_TO_MB_ADDRESS(ptr) (MemoryBlock *) ((char *) (ptr) + 2 * sizeof(MemoryBlock *) - sizeof(MemoryBlock))

void * memoryStart;
void * endOfHeap;
pthread_mutex_t globalLock;
pthread_mutexattr_t globalLockAttr;

__thread MemoryBlock * bins[NUM_OF_BINS];
__thread ThreadSharedInfo currentThreadInfo;
__thread bool isInitialized = false;

// Macro to acquire the global lock
#define GLOBAL_LOCK pthread_mutex_lock(&globalLock)

// Macro to release the global lock
#define GLOBAL_UNLOCK pthread_mutex_unlock(&globalLock)

const uint64_t deBruijn = 0x022fdd63cc95386d;
const unsigned int convert[64] = {
  0, 1, 2, 53, 3, 7, 54, 27,
  4, 38, 41, 8, 34, 55, 48, 28,
  62, 5, 39, 46, 44, 42, 22, 9,
  24, 35, 59, 56, 49, 18, 29, 11,
  63, 52, 6, 26, 37, 40, 33, 47,
  61, 45, 43, 21, 23, 58, 17, 10,
  51, 25, 36, 32, 60, 20, 57, 16,
  50, 31, 19, 15, 30, 14, 13, 12 };

int lgFloor(uint32_t n) {
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n = (n + 1) >> 1;
  return convert[(n * deBruijn) >> 58];
}

// check - This checks our invariants that the size_t header before every
// block points to either the beginning of the next block, or the end of the
// heap.
int allocator::check() {
  // Check that bins contain only free blocks
  MemoryBlock * locMB;
  for (int i = 0; i < NUM_OF_BINS; i++) {
    locMB = bins[i];
    while (locMB) {
      if (!(locMB->isFree)) {
        printf("Bin %d contains a non-free memory block\n", i);
        return -1;
      }
      locMB = locMB->nextFreeBlock;
    }
  }

  // Check that memory blocks in bins have correctly set previous and next pointers
  for (int i = 0; i < NUM_OF_BINS; i++) {
    locMB = bins[i];
    if (locMB && locMB->previousFreeBlock != 0) {
      printf("Bin %d points to a block whose previousFreeBlock is not 0\n", i);
      return -1;
    }
    while (locMB) {
      if (locMB->nextFreeBlock && locMB->nextFreeBlock->previousFreeBlock != locMB) {
        printf("Bin %d contains a memory block whose previousFreeBlock does not point to the preceding element of the binned list\n", i);
        return -1;
      }
      locMB = locMB->nextFreeBlock;
    }
  }

  locMB = currentThreadInfo.unbinnedBlocks;
  if (locMB && locMB->previousFreeBlock != 0) {
    printf("unbinnedBlocks points to a block whose previousFreeBlock is not 0\n");
    return -1;
  }
  while(locMB) {
      if (locMB->nextFreeBlock && locMB->nextFreeBlock->previousFreeBlock != locMB) {
        printf("unbinnedBlocks contains a memory block whose previousFreeBlock does not point to the preceding element of the binned list\n");
        return -1;
      }
      locMB = locMB->nextFreeBlock;
  }

  // Check that bins do not contain any duplicate blocks. This test is extremely slow. Use with caution.
  /*
  MemoryBlock * locMB2;
  for (int i = 0; i < NUM_OF_BINS; i++) {
    locMB = bins[i];
    while (locMB) {
      locMB2 = locMB->nextFreeBlock;
      int j = i;
      while (j < NUM_OF_BINS) {
        while (locMB2) {
          if (locMB == locMB2) {
            printf("Bin %d contains a memory block that is also present in bin %d\n", i, j);
            return -1;
          }
          locMB2 = locMB2->nextFreeBlock;
        }
        j++;
        locMB2 = (j < NUM_OF_BINS) ? bins[j] : 0;
      }
      locMB = locMB->nextFreeBlock;
    }
  }
  */

  // Check that all memory blocks in managed space have correctly set footers and threadInfo
  MemoryBlockFooter * footer;
  for (locMB = (MemoryBlock *) memoryStart; locMB && locMB !=endOfHeap; locMB = (MemoryBlock *) ((char *) locMB + locMB->size))
  {
    footer = MB_ADDRESS_TO_OWN_FOOTER_ADDRESS(locMB);
    if (locMB->size != *footer) {
      printf("Memory space contains a block at %p that does not have a correctly assigned footer\n", locMB);
      return -1;
    }
  }
  return 0;
}

// init - Initialize the malloc package.  Called once before any other
// calls are made.  Since this is a very simple implementation, we just
// return success.
int allocator::init() {
  pthread_mutexattr_init(&globalLockAttr);
  pthread_mutexattr_settype(&globalLockAttr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&globalLock, &globalLockAttr);
  GLOBAL_LOCK;
  endOfHeap = mem_heap_lo();
  memoryStart = endOfHeap;
  isInitialized = false;
  GLOBAL_UNLOCK;
  return 0;
}

// Helper method that calculates what bin a memory block should be assigned to as a function of its size
static inline int getBinIndex(uint32_t size) {
  assert (size > 0);
  if (size < BIN_INDEX_THRESHOLD) {
    return size / 8;
  }
  int returnIndex = lgFloor(size) + 118;
  assert(floor(log2(size)) + 118 == returnIndex);
  return ((returnIndex >= NUM_OF_BINS) ? (NUM_OF_BINS - 1) : returnIndex);
}

// Helper method that prints all free blocks present in bins (used for debugging)
static inline void printStateOfBins() {
  std::cout<<"\nUnbinned: ";
  MemoryBlock * locMB = currentThreadInfo.unbinnedBlocks;
  while (locMB) {
    std::cout<<"{"<<locMB->size<<"}";
    locMB = locMB->nextFreeBlock;
  }
  for (int i = 0; i < NUM_OF_BINS; i++) {
    locMB = bins[i];
    if (!locMB) {
      continue;
    }
    std::cout<<"\nBin["<<i<<"]: ";
    while (locMB) {
      if (locMB->isFree) {
        std::cout<<"{"<<locMB->size<<"}";
      } else {
        std::cout<<"["<<locMB->size<<"]";
      }
      locMB = locMB->nextFreeBlock;
    }
  }
}

// Helper method that prints all memory blocks, free or allocated, in the managed heap (used for debugging)
static inline void printStateOfMemory() {
  MemoryBlock * mb = (MemoryBlock *) memoryStart;
  std::cout<<"\n";
  while (mb && mb != endOfHeap) {
    if (mb->isFree) {
      std::cout<<"{"<<mb->size<<"}";
    } else {
      std::cout<<"["<<mb->size<<"]";
    }
    mb = (MemoryBlock *) ((char *) mb + mb->size);
  }
}

// Helper method that assigns a freed memory block to a bin
static inline void assignBlockToBinnedList(MemoryBlock * mb) {
  assert (mb != 0);
  assert (mb->isFree);
  int index = getBinIndex(mb->size);
  mb->nextFreeBlock = bins[index];
  if (bins[index]) {
    assert (bins[index]->previousFreeBlock == 0);
    bins[index]->previousFreeBlock = mb;
  }
  mb->previousFreeBlock = 0;
  bins[index] = mb;
}

// Helper method that assigns a freed memory block to the unbinned list of the thread the block belongs to, 
// used when a block is freed on a different thread than the one it was assigned on
static inline void assignBlockToThreadSpecificUnbinnedList(MemoryBlock * mb) {
  assert(mb);
  assert(mb->isFree);
  assert(mb->threadInfo);
  ThreadSharedInfo * mbThreadInfo = (ThreadSharedInfo *) mb->threadInfo;
  pthread_mutex_lock(&(mbThreadInfo->localLock));
  mb->nextFreeBlock = mbThreadInfo->unbinnedBlocks;
  if (mbThreadInfo->unbinnedBlocks) {
    assert(mbThreadInfo->unbinnedBlocks->previousFreeBlock == 0);
    mbThreadInfo->unbinnedBlocks->previousFreeBlock = mb;
  }
  mb->previousFreeBlock = 0;
  mbThreadInfo->unbinnedBlocks = mb;
  pthread_mutex_unlock(&(mbThreadInfo->localLock));
}

// Helper method that removes a free memory block from a given binned list or unbinned list, used to unlink blocks
// when they need to be used
static inline void removeBlockFromLinkedList (MemoryBlock * mb, MemoryBlock * &listHead) {
  assert (mb != 0);
  if (mb->previousFreeBlock) {
    mb->previousFreeBlock->nextFreeBlock = mb->nextFreeBlock;
  } else {
    listHead = mb->nextFreeBlock;
  }
  if (mb->nextFreeBlock) {
    mb->nextFreeBlock->previousFreeBlock = mb->previousFreeBlock;
  }
}

// Helper method that sets a block's footer by assigning it the block's size
static inline void assignBlockFooter (MemoryBlock * mb) {
  MemoryBlockFooter * footer = MB_ADDRESS_TO_OWN_FOOTER_ADDRESS(mb);
  *footer = mb->size;
}

// Helped method that truncates a memory block and takes care of the resulting extra free block
static inline void truncateMemoryBlock (MemoryBlock * mb, size_t new_size) {
  assert(mb);
  if (mb->size > new_size + FREE_BLOCK_OVERHEAD + FREE_BLOCK_SPLIT_THRESHOLD) {
    assert(mb->threadInfo);
    MemoryBlock * nextBlock = (MemoryBlock *)((char *)mb + new_size);
    nextBlock->size = mb->size - new_size;
    nextBlock->isFree = true;
    nextBlock->threadInfo = mb->threadInfo;
    assignBlockFooter(nextBlock);
    assignBlockToThreadSpecificUnbinnedList(nextBlock);
    mb->size = new_size;
    assignBlockFooter(mb);
  }
}

// Helper method that assigns all memory blocks present in the unbinned list to suitable binned lists.
// Also coalesces contiguous free blocks.
static inline void binAllUnbinnedBlocks() {
  if (!currentThreadInfo.unbinnedBlocks) {
    return;
  }
  pthread_mutex_lock(&(currentThreadInfo.localLock));
  MemoryBlock * mb = currentThreadInfo.unbinnedBlocks;
  MemoryBlock * nextMB, * prevMB;
  size_t totalFree;

  while (mb) {
    assert(mb->isFree);
    assert(mb->threadInfo == (void *) &currentThreadInfo);
    mb->isFree = false; // Necessary as a sentinal for coalescing so blocks from the unbinned list don't coalesce with one another
    mb = mb->nextFreeBlock;
  }

  mb = currentThreadInfo.unbinnedBlocks;
  while (mb) {
    // Coalesce with free blocks on the right
    nextMB = (MemoryBlock *) ((char *) mb + mb->size);
    totalFree = 0;
    while(nextMB != endOfHeap && nextMB->threadInfo == mb->threadInfo && nextMB->isFree) {
      totalFree += nextMB->size;
      removeBlockFromLinkedList(nextMB, bins[getBinIndex(nextMB->size)]);
      nextMB = (MemoryBlock *) ((char *) nextMB + nextMB->size);
    }
    mb->size += totalFree;
    assignBlockFooter(mb);
    nextMB = mb->nextFreeBlock;

    // Coalesce with free blocks on the left
    if ((void *) mb > memoryStart) {
      assert((void *) mb >= (void *)((char *) memoryStart + ALLOCATED_BLOCK_OVERHEAD));
      MemoryBlockFooter * footer = MB_ADDRESS_TO_PREVIOUS_FOOTER_ADDRESS(mb);
      prevMB = (MemoryBlock *) ((char *) mb - *footer);
      totalFree = mb->size;
      while ((void *) prevMB >= memoryStart && prevMB->threadInfo == mb->threadInfo && prevMB->isFree) {
        totalFree += prevMB->size;
        removeBlockFromLinkedList(prevMB, bins[getBinIndex(prevMB->size)]);
        mb = prevMB;
        if ((void *) prevMB == memoryStart) {
          break;
        }
        footer = MB_ADDRESS_TO_PREVIOUS_FOOTER_ADDRESS(prevMB);
        prevMB = (MemoryBlock *) ((char *) prevMB - *footer);
      }
      mb->size = totalFree;
      assignBlockFooter(mb);
    }

    mb->isFree = true;

    // Assign to a suitable bin
    assignBlockToBinnedList(mb);
    currentThreadInfo.unbinnedBlocks = nextMB;
    mb = nextMB;
  }
  currentThreadInfo.unbinnedBlocks = 0;
  pthread_mutex_unlock(&(currentThreadInfo.localLock));
}

// Helper method to initialize the state variables of a thread the first time it is run
static inline void threadInit() {
  for (int i = 0; i < NUM_OF_BINS; i++) {
    bins[i] = 0;
  }
  currentThreadInfo.unbinnedBlocks = 0;
  pthread_mutex_init(&(currentThreadInfo.localLock), NULL);
  MemoryBlock * mb;
  GLOBAL_LOCK;
  void *p = mem_sbrk(INITIAL_ALLOCATION_PER_THREAD);
  if (p == (void *) -1) {
    GLOBAL_UNLOCK;
    return;
  }
  mb = (MemoryBlock *) endOfHeap;
  endOfHeap += INITIAL_ALLOCATION_PER_THREAD;
  GLOBAL_UNLOCK;
  mb->size = INITIAL_ALLOCATION_PER_THREAD;
  mb->threadInfo = (void *) &currentThreadInfo;
  mb->isFree = true;
  assignBlockFooter(mb);
  assignBlockToBinnedList(mb);
  isInitialized = true;
}

  //  malloc - Allocate a block of the requested size.
  //  Ensures block size is a multiple of the alignment.
void * allocator::malloc(size_t size) {
  if (!isInitialized) {
    threadInit();
  }
  void * currentLoc;
  size_t alignedSize = ALIGN(size + ALLOCATED_BLOCK_OVERHEAD);
  alignedSize = (alignedSize > MINIMUM_ALLOCATED_BLOCK_SIZE)? alignedSize : MINIMUM_ALLOCATED_BLOCK_SIZE;
  MemoryBlock * currentLocMB;
  int i = getBinIndex(alignedSize);
  binAllUnbinnedBlocks();

  // Look through existing free blocks in binned lists to see if any of them can be recycled
  while (i < NUM_OF_BINS) {
    currentLoc = bins[i];
    currentLocMB = (MemoryBlock *) bins[i];
    while (currentLoc) {
      if (currentLocMB->size >= alignedSize) {
        // Found a match
        truncateMemoryBlock(currentLocMB, alignedSize);
        removeBlockFromLinkedList(currentLocMB, bins[i]);
        currentLocMB->isFree = false;
        assert(currentLocMB->threadInfo == (void *) &currentThreadInfo);
        return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(currentLoc);
      }
      currentLocMB = currentLocMB->nextFreeBlock;
      currentLoc = (void *) currentLocMB;
    }
    i++;
  }

  // Did not find a free block that can be recycled. Must ask mem_sbrk for memory.
  GLOBAL_LOCK;
  void *p = mem_sbrk(alignedSize);
  if (p == (void *) -1) {
    GLOBAL_UNLOCK;
    return NULL;
  }
  currentLoc = endOfHeap;
  endOfHeap += alignedSize;
  GLOBAL_UNLOCK;
  currentLocMB = (MemoryBlock *) currentLoc;
  currentLocMB->size = alignedSize;
  currentLocMB->threadInfo = (void *) &currentThreadInfo;
  currentLocMB->isFree = false;
  assignBlockFooter(currentLocMB);
  return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(currentLoc);
}

// free - Simply bins the block that needs to be freed if this thread owns it; otherwise, 
// assigns it to the owner thread's unbinned list
void allocator::free(void *ptr) {
  MemoryBlock * mb;
  mb = INTERNAL_SPACE_ADDRESS_TO_MB_ADDRESS(ptr);
  assert(!mb->isFree);
  mb->isFree = true;
  if (mb->threadInfo == &currentThreadInfo) {
    assignBlockToBinnedList(mb);
  } else {
    assignBlockToThreadSpecificUnbinnedList(mb);
  }
  return;
}

// realloc - Implemented using special cases to save the need for copying memory contents or calling both malloc and free
void * allocator::realloc(void *ptr, size_t size) {
  
  MemoryBlock * mb = INTERNAL_SPACE_ADDRESS_TO_MB_ADDRESS(ptr);
  size_t alignedSize = ALIGN(size + ALLOCATED_BLOCK_OVERHEAD);
  alignedSize = (alignedSize > MINIMUM_ALLOCATED_BLOCK_SIZE)? alignedSize : MINIMUM_ALLOCATED_BLOCK_SIZE;
  
  // Case when new size is less than the existing size of the block and the same block can be returned as is
  if (alignedSize < mb->size) {
    truncateMemoryBlock(mb, alignedSize);
    return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mb);
  }

  // Case when new size is greater than existing size..
  if (alignedSize > mb->size) {
    MemoryBlock * nextMB = (MemoryBlock *) ((char *) mb + mb->size);
    // .. but the block to the right in memory is also free and can be used to satisfy the reallocation
    if (nextMB != endOfHeap && nextMB->threadInfo == mb->threadInfo && nextMB->isFree && (mb->size + nextMB->size) >= alignedSize) {
      MemoryBlock * t = currentThreadInfo.unbinnedBlocks;
      MemoryBlock ** listHead = &bins[getBinIndex(nextMB->size)];
      while (t) {
        if (nextMB == t) {
          listHead = &(currentThreadInfo.unbinnedBlocks);
          break;
        }
        t = t->nextFreeBlock;
      }
      removeBlockFromLinkedList(nextMB, *listHead);
      mb->size = mb->size + nextMB->size;
      assignBlockFooter(mb);
      truncateMemoryBlock(mb, alignedSize);
      return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mb);
    }
    else {
      GLOBAL_LOCK;
      // Case when the block we need to reallocate is located at the end of the memory heap, 
      // thereby allowing us to call mem_sbrk on  only the required difference in size
      if ((char *) mb + mb->size == (char *) endOfHeap) {
        size_t neededAllocation = alignedSize - mb->size;
        void *p = mem_sbrk(neededAllocation);
        if (p == (void *) -1) {
          GLOBAL_UNLOCK;
          return NULL;
        }
        endOfHeap += neededAllocation;
        GLOBAL_UNLOCK;
        mb->size = alignedSize;
        assignBlockFooter(mb);
        return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mb);
      }

      // Case when no special cases work and the only way to reallocate is to call malloc followed by free
      GLOBAL_UNLOCK;
      void * newptr = malloc(size);
      if (!newptr) {
        return NULL;
      }
      size_t copy_size = mb->size - ALLOCATED_BLOCK_OVERHEAD; // internal size of the original memory block
      copy_size = (size < copy_size)? size : copy_size; // if the new size is less that the original internal size, we MUST NOT copy more than new size bytes to the new block
      std::memcpy(newptr, ptr, copy_size);
      free(ptr);
      return newptr;
    }
  }

  // Case when new size is just the same as the original size of the block
  return ptr; 
}

// call mem_reset_brk.
void allocator::reset_brk() {
  mem_reset_brk();
}

// call mem_heap_lo
void * allocator::heap_lo() {
  return mem_heap_lo();
}

// call mem_heap_hi
void * allocator::heap_hi() {
  return mem_heap_hi();
}
};

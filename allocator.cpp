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
#include <iostream> // remove later
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
// check - This checks our invariant that the size_t header before every
// block points to either the beginning of the next block, or the end of the
// heap.

struct ThreadSharedInfo {
  pthread_mutex_t localLock;
  void * unbinnedBlocks;
};

struct MemoryBlock {
  ThreadSharedInfo * threadInfo;
  MemoryBlock * nextFreeBlock; // pointer to the next free block in the binned free list that this belongs to.
  MemoryBlock * previousFreeBlock; // pointer to the previous free block in the binned free list that this belongs to.
  uint32_t size; // size is the entire block size, including the header and the footer
  bool isFree;;
};

typedef uint32_t MemoryBlockFooter;

#define TOTAL_BLOCK_OVERHEAD (sizeof(MemoryBlock) + sizeof(MemoryBlockFooter))
#define BIN_INDEX_THRESHOLD 1024
#define NUM_OF_BINS 150
#define FREE_BLOCK_SPLIT_THRESHOLD 8

#define MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mbptr) ((void *) ((char *)(mbptr) + sizeof(MemoryBlock))) // given a MemoryBlock pointer mbptr, returns the internal space address that should be visible to the user
#define MB_ADDRESS_TO_OWN_FOOTER_ADDRESS(mbptr) (MemoryBlockFooter *) ((char *) (mbptr) + (mbptr)->size - sizeof(MemoryBlockFooter))
#define MB_ADDRESS_TO_PREVIOUS_FOOTER_ADDRESS(mbptr) (MemoryBlockFooter *)((char *) (mbptr) - sizeof(MemoryBlockFooter))
#define INTERNAL_SPACE_ADDRESS_TO_MB_ADDRESS(ptr) (MemoryBlock *) ((char *) (ptr) - sizeof(MemoryBlock))

void * memoryStart; //is always mem_heap_lo
void * endOfHeap;
pthread_mutex_t globalLock;
pthread_mutexattr_t globalLockAttr;
__thread MemoryBlock * bins[NUM_OF_BINS];
__thread ThreadSharedInfo sharedInfo;
__thread bool isInitialized = false;


#define GLOBAL_LOCK pthread_mutex_lock(&globalLock)
#define GLOBAL_UNLOCK pthread_mutex_unlock(&globalLock)

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
  
  /*
  // Check that bins do not contain any duplicate blocks. This test is extremely slow. Use with caution.
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
  
  // Check that all memory blocks in managed space have correctly set footers
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
  GLOBAL_UNLOCK;
  return 0;
}

static inline void threadInit() {
  //  std::cout<<"\nInitializing thread.";
  for (int i = 0; i < NUM_OF_BINS; i++) {
    //    std::cout<<"\n\tbins["<<i<<"] is located at "<<&bins[i];
    bins[i] = 0;
  }
  sharedInfo.unbinnedBlocks = 0;
  pthread_mutex_init(&(sharedInfo.localLock), NULL);
  //  std::cout<<"\nThread local mutex located at "<<&localLock;
  //  std::cout<<"\ntlsKey located at "<<&tlsKey;
  isInitialized = true;
}

static inline int getBinIndex(uint32_t size) {
  assert (size > 0);
  if (size < BIN_INDEX_THRESHOLD) {
    return size / 8;
  }
  int returnIndex = floor(log2(size)) + 118;
  return ((returnIndex >= NUM_OF_BINS) ? (NUM_OF_BINS - 1) : returnIndex);
}

static inline void printStateOfBins() {
  for (int i = 0; i < NUM_OF_BINS; i++) {
    MemoryBlock * locMB = bins[i];
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

static inline void removeBlockFromBinnedList (MemoryBlock * mb, int i) {
  assert (mb != 0);
  if (mb->previousFreeBlock) {
    mb->previousFreeBlock->nextFreeBlock = mb->nextFreeBlock;
  } else {
    bins[i] = mb->nextFreeBlock;
  }
  if (mb->nextFreeBlock) {
    mb->nextFreeBlock->previousFreeBlock = mb->previousFreeBlock;
  }
}

static inline void assignBlockFooter (MemoryBlock * mb) {
  MemoryBlockFooter * footer = MB_ADDRESS_TO_OWN_FOOTER_ADDRESS(mb);
  *footer = mb->size;
}

static inline void truncateMemoryBlock (MemoryBlock * mb, size_t new_size) {
  if (mb->size > new_size + TOTAL_BLOCK_OVERHEAD + FREE_BLOCK_SPLIT_THRESHOLD) {
    MemoryBlock * nextBlock = (MemoryBlock *)((char *)mb + new_size);
    nextBlock->size = mb->size - new_size;
    nextBlock->isFree = true;
    assignBlockFooter(nextBlock);
    assignBlockToBinnedList(nextBlock);
    mb->size = new_size;
    assignBlockFooter(mb);
  }
}

static inline void binAllUnbinnedBlocks(){
}

  //  malloc - Allocate a block by incrementing the brk pointer.
  //  Always allocate a block whose size is a multiple of the alignment.
void * allocator::malloc(size_t size) {
  if (!isInitialized) {
    threadInit();
  }
  void * currentLoc;
  size_t alignedSize = ALIGN(size + TOTAL_BLOCK_OVERHEAD);
  MemoryBlock * currentLocMB;
  int i = getBinIndex(alignedSize);
  binAllUnbinnedBlocks();
  //std::cout<<"\n\nAsked for allocation of size "<<size;
  //std::cout<<"\nNeed "<<alignedSize<<" to accommodate header.";
  while (i < NUM_OF_BINS) {
    currentLoc = bins[i]; //currentLoc is set to the first element in the binned free list
    currentLocMB = (MemoryBlock *) bins[i];
    //std::cout<<"\nChecking bin "<<i<<" whose first free block is "<<bins[i];
    while (currentLoc && currentLoc != endOfHeap) { // TODO: remove 2nd condition later
      if (currentLocMB->size >= alignedSize) {
        truncateMemoryBlock(currentLocMB, alignedSize);
        removeBlockFromBinnedList(currentLocMB, i);
        currentLocMB->nextFreeBlock = 0;
        currentLocMB->previousFreeBlock = 0;
        currentLocMB->isFree = false;
        return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(currentLoc);
      }
      currentLocMB = currentLocMB->nextFreeBlock;
      currentLoc = (void *) currentLocMB;
    }
    i++;
  }
  //std::cout<<"\nNeed to ask for extra memory to allocate.";
  //size_t increase = (alignedSize < 496)? 512: alignedSize;
  GLOBAL_LOCK;
  void *p = mem_sbrk(alignedSize); // increase
  if (p == (void *) -1) {
    GLOBAL_UNLOCK;
    return NULL;
  }
  currentLoc = endOfHeap;
  endOfHeap += alignedSize; // increase
  GLOBAL_UNLOCK;
  currentLocMB = (MemoryBlock *) currentLoc;
  currentLocMB->nextFreeBlock = 0;
  currentLocMB->previousFreeBlock = 0;
  currentLocMB->size = alignedSize;
  currentLocMB->isFree = false;
  assignBlockFooter(currentLocMB);
  return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(currentLoc);
}

void allocator::free(void *ptr) {
  return;
  GLOBAL_LOCK;
  //std::cout<<"\n\nAsked to free space at "<<ptr;
  MemoryBlock * mb;
  mb = INTERNAL_SPACE_ADDRESS_TO_MB_ADDRESS(ptr);
  assert(mb->nextFreeBlock == 0 && mb->previousFreeBlock == 0);
  mb->isFree = true;
  
  // Coalesce with free blocks on the right
  MemoryBlock * nextMB = (MemoryBlock *) ((char *) mb + mb->size);
  size_t totalFree = 0;
  while(nextMB != endOfHeap && nextMB->isFree) {
    totalFree += nextMB->size;
    removeBlockFromBinnedList(nextMB, getBinIndex(nextMB->size));
    // is it also necessary to reset nextMB's footer?
    nextMB = (MemoryBlock *) ((char *) nextMB + nextMB->size);
  }
  mb->size += totalFree;
  assignBlockFooter(mb);
  
  // Coalesce with free blocks on the left
  if ((void *) mb > memoryStart) {
    assert((void *) mb >= (void *)((char *) memoryStart + TOTAL_BLOCK_OVERHEAD));
    MemoryBlockFooter * footer = MB_ADDRESS_TO_PREVIOUS_FOOTER_ADDRESS(mb);
    MemoryBlock * prevMB = (MemoryBlock *) ((char *) mb - *footer);
    totalFree = mb->size;
    //bool entered = false;
    while (prevMB && (void *) prevMB >= memoryStart && prevMB->isFree) { // could remove first boolean predicate
      //entered = true;
      totalFree += prevMB->size;
      removeBlockFromBinnedList(prevMB, getBinIndex(prevMB->size));
      mb = prevMB;
      if ((void *) prevMB == memoryStart) {
        break;
      }
      footer = MB_ADDRESS_TO_PREVIOUS_FOOTER_ADDRESS(prevMB);
      prevMB = (MemoryBlock *) ((char *) prevMB - *footer);
    }
    /*
      if (entered) {
      std::cout<<"\n\nSuccessful left coalesce. Size of block before was: "<<mb->size<<". State of memory before coalesce: ";
      printStateOfMemory();
      }
    */
    mb->size = totalFree;
    assignBlockFooter(mb);
    /*
      if (entered) {
      std::cout<<"\nNew size of block: "<<mb->size<<". New state of memory: ";
      printStateOfMemory();
      }
    */
  }
  assignBlockToBinnedList(mb);
  GLOBAL_UNLOCK;
}

  // realloc - Implemented simply in terms of malloc and free
void * allocator::realloc(void *ptr, size_t size) {
  // std::cout<<"\n\nAsked to reallocate block at "<<ptr;
  MemoryBlock * mb = INTERNAL_SPACE_ADDRESS_TO_MB_ADDRESS(ptr);
  // std::cout<<" that had an internal size of "<<mb->size - TOTAL_BLOCK_OVERHEAD<<", an aligned size (incl. overhead) of "<<mb->size<<", and a new requested size of "<<size;
  size_t alignedSize = ALIGN(size + TOTAL_BLOCK_OVERHEAD);
  // std::cout<<"\nOriginal state of memory: ";
  // printStateOfMemory();
  
  if (alignedSize < mb->size) { // new size is less than the existing size of the block
    truncateMemoryBlock(mb, alignedSize);
    // std::cout<<"\nReallocated by truncating to aligned size. \nNew state of memory: ";
    // printStateOfMemory();
    return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mb);
  }
  if (alignedSize > mb->size) {
    MemoryBlock * nextMB = (MemoryBlock *) ((char *) mb + mb->size);
    if (nextMB != endOfHeap && nextMB->isFree && (mb->size + nextMB->size) >= alignedSize) {
      removeBlockFromBinnedList(nextMB, getBinIndex(nextMB->size));
      mb->size = mb->size + nextMB->size;
      assignBlockFooter(mb);
      truncateMemoryBlock(mb, alignedSize);
      // std::cout<<"\nReallocated by using adjacent free block on right. \nNew state of memory: ";
      // printStateOfMemory();
      return MB_ADDRESS_TO_INTERNAL_SPACE_ADDRESS(mb);
    }
    else {
      void * newptr = malloc(size);
      if (!newptr) {
        return NULL;
      }
      size_t copy_size = mb->size - TOTAL_BLOCK_OVERHEAD; // internal size of the original memory block
      copy_size = (size < copy_size)? size : copy_size; // if the new size is less that the original internal size, we MUST NOT copy more than new size bytes to the new block
      std::memcpy(newptr, ptr, copy_size);
      free(ptr);
      // std::cout<<"\nReallocated by calling malloc and free. Had to copy "<<copy_size<<" bytes using memcpy. \nNew state of memory: ";
      // printStateOfMemory();
      return newptr;
    }
  }
  // std::cout<<"\nReturning original pointer as new alignedSize == original aligned size of memory block.";
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

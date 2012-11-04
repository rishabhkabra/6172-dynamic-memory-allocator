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
#include <iostream> // remove later
#include "./allocator_interface.h"
#include "./memlib.h"

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

struct MemoryBlock {
  MemoryBlock * nextFreeBlock; // pointer to the next free block in the binned free list that this belongs to.
  MemoryBlock * previousFreeBlock; // pointer to the previous free block in the binned free list that this belongs to.
  uint32_t size;
  bool isFree;;
};

typedef uint32_t MemoryBlockFooter;

#define TOTAL_BLOCK_OVERHEAD (sizeof(MemoryBlock) + sizeof(MemoryBlockFooter))
#define FREE_BLOCK_SPLIT_THRESHOLD 0

void * memoryStart; //is always mem_heap_lo
void * endOfHeap;
#define NUM_OF_BINS 32
MemoryBlock * bins[NUM_OF_BINS];

  int allocator::check() {
    char *p;
    char *lo = (char*)mem_heap_lo();
    char *hi = (char*)mem_heap_hi() + 1;
    size_t size = 0;

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
      footer = (MemoryBlockFooter *) ((char *) locMB + locMB->size - sizeof(MemoryBlockFooter));
      if (locMB->size != *footer) {
        printf("Memory space contains a block at %p that does not have a correctly assigned footer\n", locMB);
        return -1;
      }
    }
    // p = lo;
    // while (lo <= p && p < hi) {
    //   size = ALIGN(*(size_t*)p + SIZE_T_SIZE);
    //   p += size;
    // }

    // if (p != hi) {
    //   printf("Bad headers did not end at heap_hi!\n");
    //   printf("heap_lo: %p, heap_hi: %p, size: %lu, p: %p\n", lo, hi, size, p);
    //   return -1;
    // }

    return 0;
  }

  // init - Initialize the malloc package.  Called once before any other
  // calls are made.  Since this is a very simple implementation, we just
  // return success.
  int allocator::init() {
    endOfHeap = mem_heap_lo();
    memoryStart = endOfHeap;
    for (int i = 0; i < NUM_OF_BINS; i++) {
      bins[i] = 0;
    }
    return 0;
  }

static inline int getBinIndex(uint32_t size) {
  assert (size > 0);
  return (floor(log2(size)) >= NUM_OF_BINS) ? (NUM_OF_BINS - 1) : floor(log2(size));
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
  MemoryBlockFooter * footer = (MemoryBlockFooter *) ((char *) mb + mb->size - sizeof(MemoryBlockFooter));
  *footer = mb->size;
}

  //  malloc - Allocate a block by incrementing the brk pointer.
  //  Always allocate a block whose size is a multiple of the alignment.
void * allocator::malloc(size_t size) {
  
  void * currentLoc, * memoryLocToReturn = 0;
  int alignedSize = ALIGN(size + TOTAL_BLOCK_OVERHEAD);
  MemoryBlock * currentLocMB;
  int i = getBinIndex(alignedSize);
  //std::cout<<"\n\nAsked for allocation of size "<<size;
  //std::cout<<"\nNeed "<<alignedSize<<" to accommodate header.";
  while (i < NUM_OF_BINS) {
    currentLoc = bins[i]; //currentLoc is set to the first element in the binned free list
    currentLocMB = (MemoryBlock *) bins[i];
    //std::cout<<"\nChecking bin "<<i<<" whose first free block is "<<bins[i];
      while (currentLoc && currentLoc != endOfHeap) {
       
        if (currentLocMB->size > alignedSize + TOTAL_BLOCK_OVERHEAD + FREE_BLOCK_SPLIT_THRESHOLD) {
          // we have enough space in currentLocMB to split it instead of assigning the whole block
          MemoryBlock * nextBlock = (MemoryBlock *)((char *)currentLoc + alignedSize);
          nextBlock->size = currentLocMB->size - alignedSize;
          nextBlock->isFree = true;
          assignBlockFooter(nextBlock);
          assignBlockToBinnedList(nextBlock);
          currentLocMB->size = alignedSize;
          assignBlockFooter(currentLocMB);
        }
       
        if (currentLocMB->size >= alignedSize) {
          //std::cout<<"\nCurrent MemoryBlock ("<<currentLocMB<<") has size "<<currentLocMB->size<<" and is a match.";
          memoryLocToReturn = (void *) ((char *)currentLoc + sizeof(MemoryBlock));
          //std::cout<<"\nMemory location to return: "<<memoryLocToReturn;
          removeBlockFromBinnedList(currentLocMB, i);
          currentLocMB->nextFreeBlock = 0; // to indicate that this block is not free anymore
          currentLocMB->previousFreeBlock = 0;
          currentLocMB->isFree = false;
          return memoryLocToReturn;
        }
        currentLocMB = currentLocMB->nextFreeBlock;
        currentLoc = (void *) currentLocMB;
      }
      i++;
    }
    //std::cout<<"\nNeed to ask for extra memory to allocate.";
    //size_t increase = (alignedSize < 496)? 512: alignedSize;
    void *p = mem_sbrk(alignedSize); // increase
    if (p == (void *) -1) {
      return NULL;
    }
    currentLoc = endOfHeap;
    endOfHeap += alignedSize; // increase
    currentLocMB = (MemoryBlock *) currentLoc;
    currentLocMB->nextFreeBlock = 0;
    currentLocMB->previousFreeBlock = 0;
    currentLocMB->size = alignedSize;
    currentLocMB->isFree = false;
    assignBlockFooter(currentLocMB);

    /*
    if (increase > alignedSize) {
      ((MemoryBlock *)((char *) currentLoc + currentLocMB->size))->isFree = true;
      ((MemoryBlock *)((char *) currentLoc + currentLocMB->size))->size = increase - alignedSize;
    }
    */
    memoryLocToReturn = (void *) ((char *)currentLoc + sizeof(MemoryBlock));
    //std::cout<<"\nMemory Location To Return: "<<memoryLocToReturn;
    return memoryLocToReturn;
}


  void allocator::free(void *ptr) {
    //std::cout<<"\n\nAsked to free space at "<<ptr;
    MemoryBlock * mb;
    mb = (MemoryBlock *) ((char *) ptr - sizeof(MemoryBlock));
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
      MemoryBlockFooter * footer = (MemoryBlockFooter *)((char *) mb - sizeof(MemoryBlockFooter));
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
        footer = (MemoryBlockFooter *)((char *) prevMB - sizeof(MemoryBlockFooter));
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
  }

  // realloc - Implemented simply in terms of malloc and free
  void * allocator::realloc(void *ptr, size_t size) {
    void *newptr;
    size_t copy_size;

    // Allocate a new chunk of memory, and fail if that allocation fails.
    newptr = malloc(size);
    if (NULL == newptr)
      return NULL;

    // Get the size of the old block of memory.  Take a peek at malloc(),
    // where we stashed this in the SIZE_T_SIZE bytes directly before the
    // address we returned.  Now we can back up by that many bytes and read
    // the size.
    copy_size = *(size_t*)((uint8_t*)ptr - SIZE_T_SIZE);

    // If the new block is smaller than the old one, we have to stop copying
    // early so that we don't write off the end of the new block of memory.
    if (size < copy_size)
      copy_size = size;

    // This is a standard library call that performs a simple memory copy.
    std::memcpy(newptr, ptr, copy_size);

    // Release the old block.
    free(ptr);

    // Return a pointer to the new block.
    return newptr;
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

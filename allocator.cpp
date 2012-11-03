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
#include <cstdlib>
#include <cstring>
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
  size_t isFree;
  size_t size;
};

void * memoryStart; //is always mem_heap_lo
void * endOfHeap;

  int allocator::check() {
    char *p;
    char *lo = (char*)mem_heap_lo();
    char *hi = (char*)mem_heap_hi() + 1;
    size_t size = 0;

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

    void * currentLoc = memoryStart;
    MemoryBlock * currentLocMB = (MemoryBlock *) memoryStart;
    void * memoryLocToReturn = 0;
    int alignedSize = ALIGN(size + sizeof(MemoryBlock));
    while (currentLoc != endOfHeap) {
      if (!(currentLocMB->isFree == 1 || currentLocMB->isFree == 0)) {
        printf("Bad isFree\n");
        return -1;
      }
      currentLoc = currentLoc + currentLocMB->size;
      currentLocMB = (MemoryBlock *) currentLoc;
    }
    return 0;
  }

  // init - Initialize the malloc package.  Called once before any other
  // calls are made.  Since this is a very simple implementation, we just
  // return success.
  int allocator::init() {
    endOfHeap = mem_heap_lo();
    memoryStart = endOfHeap;
    return 0;
  }

  //  malloc - Allocate a block by incrementing the brk pointer.
  //  Always allocate a block whose size is a multiple of the alignment.
  void * allocator::malloc(size_t size) {
    void * currentLoc = memoryStart;
    MemoryBlock * currentLocMB = (MemoryBlock *) memoryStart;
    void * memoryLocToReturn = 0;
    int alignedSize = ALIGN(size + sizeof(MemoryBlock));
    while (currentLoc != endOfHeap) {
      if (currentLocMB->isFree) {
        if (currentLocMB->size > alignedSize + sizeof(MemoryBlock)) {
          MemoryBlock * nextBlock = (MemoryBlock *)((char *)currentLoc + alignedSize);
          nextBlock->size = currentLocMB->size - alignedSize;
          nextBlock->isFree = true;
          currentLocMB->size = alignedSize;
        }
        if (currentLocMB->size >= alignedSize) {
          currentLocMB->isFree = false;
          memoryLocToReturn = currentLoc;
          break;
        }
      }
      currentLoc = currentLoc + currentLocMB->size;
      currentLocMB = (MemoryBlock *) currentLoc;
    }
    if (!memoryLocToReturn) {
      void *p = mem_sbrk(alignedSize);
      if (p == (void *) -1) {
        return NULL;
      }
      memoryLocToReturn = endOfHeap;
      endOfHeap += alignedSize;
      currentLoc = memoryLocToReturn;
      currentLocMB = (MemoryBlock *) currentLoc;
      currentLocMB->isFree = false;
      currentLocMB->size = alignedSize;
    }
    memoryLocToReturn = (void *) ((char *)memoryLocToReturn + sizeof(MemoryBlock));
    return memoryLocToReturn;
  }

  // free - Freeing a block does nothing.
  void allocator::free(void *ptr) {
    MemoryBlock * mb;
    mb = (MemoryBlock *) ((char *) ptr - sizeof(MemoryBlock));
    mb->isFree = true;
    MemoryBlock * nextMb = (MemoryBlock *) ((char *) mb + mb->size);
    size_t totalFree = 0;
    while(nextMb != endOfHeap && nextMb->isFree) {
      totalFree += nextMb->size;
      nextMb = (MemoryBlock *) ((char *) nextMb + nextMb->size);
    }
    mb->size += totalFree;
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

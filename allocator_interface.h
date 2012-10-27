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

#include <cstdlib>

#ifndef _ALLOCATOR_INTERFACE_H
#define _ALLOCATOR_INTERFACE_H

namespace my {
  class allocator_interface { // NOLINT
  public:
    virtual void reset_brk() = 0;
    virtual void * heap_lo() = 0;
    virtual void * heap_hi() = 0;
  };

  class libc_allocator : public virtual allocator_interface {
  public:
    static int init();
    static void * malloc(size_t size);
    static void * realloc(void *ptr, size_t size);
    static void free(void *ptr);
    static int check();
    void reset_brk();
    void * heap_lo();
    void * heap_hi();
  };

  class allocator : public virtual allocator_interface {
  public:
    static int init();
    static void * malloc(size_t size);
    static void * realloc(void *ptr, size_t size);
    static void free(void *ptr);
    static int check();
    void reset_brk();
    void * heap_lo();
    void * heap_hi();
  };


  class bad_allocator : public virtual allocator_interface {
  public:
    static int init();
    static void * malloc(size_t size);
    static void * realloc(void *ptr, size_t size);
    static void free(void *ptr);
    static int check();
    void reset_brk();
    void * heap_lo();
    void * heap_hi();
  };
};
#endif  // _ALLOCATOR_INTERFACE_H

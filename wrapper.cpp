/*
 * A wrappper for allocator.cpp.
 *
 * Do not modify this file.
 */

#include "wrapper.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <sstream>
#include <pthread.h>
#include "allocator.cpp"

#include <unistd.h>

static pthread_mutex_t my_malloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool is_my_malloc_initialized = false;

#ifdef VALIDATE
static int seq = 0;

#ifdef USE_ONE_LOG
static std::ofstream *file_log = 0;
static std::stringstream *string_log = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#else
__thread static std::ofstream *file_log = 0;
__thread static std::stringstream *string_log = 0;
#endif

std::ofstream& getFileLog() {
#ifdef USE_ONE_LOG
  pthread_mutex_lock(&log_mutex);
  if (file_log == 0) {
    file_log = new std::ofstream("tmp/1.out");
  }
  pthread_mutex_unlock(&log_mutex);
#else
  // log is thread-local, so we don't need to lock.
  if (file_log == 0) {
    char filename[20];
    sprintf(filename, "tmp/%u.out", (unsigned int)pthread_self());
    file_log = new std::ofstream(filename);
  }
#endif
  return *file_log;
}
std::stringstream& getStringLog() {
#ifdef USE_ONE_LOG
  pthread_mutex_lock(&log_mutex);
  if (string_log == 0) {
    string_log = new std::stringstream("");
  }
  pthread_mutex_unlock(&log_mutex);
#else
  // log is thread-local, so we don't need to lock.
  if (string_log == 0) {
    //char filename[20];
    //sprintf(filename, "tmp/%u.out", (unsigned int)pthread_self());
    string_log = new std::stringstream("");
  }

#endif

  return *string_log;
}
#endif
void my_malloc_init()
{
  pthread_mutex_lock(&my_malloc_mutex);
  if (!is_my_malloc_initialized) {

#ifdef MYMALLOC
    std::cout << "Using custom allocator.\n";
#endif

#ifdef VALIDATE
    std::cout << "Running validate version.\n";
#endif

    // Initialize memlib. (Students will not call this in their initialization.)
    mem_init();

    my::allocator::init();
    is_my_malloc_initialized = true;
  }
  pthread_mutex_unlock(&my_malloc_mutex);
}

void my_free(void *ptr)
{

#ifdef VALIDATE
  int i = __sync_fetch_and_add(&seq, 1);
  std::stringstream& log = getStringLog();

#ifdef USE_ONE_LOG
  pthread_mutex_lock(&log_mutex);
#endif /* USE_ONE_LOG */
  log << i << " free " << ptr << "\n";
#ifdef USE_ONE_LOG
  pthread_mutex_unlock(&log_mutex);
#endif /* USE_ONE_LOG */
#endif /* VALIDATE */

  my::allocator::free(ptr);
}

void *my_malloc(size_t size)
{
  if (!is_my_malloc_initialized) {
    my_malloc_init();
  }

  void* ret = my::allocator::malloc(size);

#ifdef VALIDATE
  int i = __sync_fetch_and_add(&seq, 1);
  std::stringstream& log = getStringLog();
#ifdef USE_ONE_LOG
  pthread_mutex_lock(&log_mutex);
#endif /* USE_ONE_LOG */
  log << i << " malloc " << size << " " << ret << "\n";
#ifdef USE_ONE_LOG
  pthread_mutex_unlock(&log_mutex);
#endif /* USE_ONE_LOG */
#endif /* VALIDATE */
  return ret;
}

void *my_realloc(void* ptr, size_t size)
{
#ifdef VALIDATE
  int i = __sync_fetch_and_add(&seq, 1);
  std::stringstream& log = getStringLog();
#ifdef USE_ONE_LOG
  pthread_mutex_lock(&log_mutex);
#endif /* USE_ONE_LOG */
  log << i << " realloc-begin " << ptr << " " << size << "\n";
#ifdef USE_ONE_LOG
  pthread_mutex_unlock(&log_mutex);
#endif /* USE_ONE_LOG */
#endif /* VALIDATE */

  void* ret = my::allocator::realloc(ptr, size);

#ifdef VALIDATE
  i = __sync_fetch_and_add(&seq, 1);
#ifdef USE_ONE_LOG
  pthread_mutex_lock(&log_mutex);
#endif /* USE_ONE_LOG */
  log << i << " realloc-end " << ptr << " " << size << " " << ret << "\n";
#ifdef USE_ONE_LOG
  pthread_mutex_unlock(&log_mutex);
#endif /* USE_ONE_LOG */
#endif /* VALIDATE */
  return ret;
}

//
// Hooks for benchmark programs
//

void end_thread() {
#ifdef VALIDATE
#ifndef USE_ONE_LOG
  //getLog().close();
  std::string logString = getStringLog().str();
  getFileLog() << logString;
  getFileLog().close();
  pthread_mutex_lock(&my_malloc_mutex);
  std::cerr << "Log file: " << (unsigned int) pthread_self() << "\n";
  pthread_mutex_unlock(&my_malloc_mutex);
#else
  getFileLog() << getStringLog().str();
  getFileLog().close();
#endif /* USE_ONE_LOG */
#endif /* VALIDATE */

}

void end_program() {
#ifdef MYMALLOC
  std::cerr << "Heap size: " << mem_heapsize() << "\n";

#ifdef VALIDATE
#ifdef USE_ONE_LOG
  getFileLog() << getStringLog().str();
  getFileLog().close();
  pthread_mutex_lock(&my_malloc_mutex);
  std::cerr << "Log file: 1\n";
  pthread_mutex_unlock(&my_malloc_mutex);
#else
  // End main thread
  end_thread();
#endif /* USE_ONE_LOG */
#endif /* VALIDATE */

#endif /* MYMALLOC */
}


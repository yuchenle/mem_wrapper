#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

unsigned long long allocated_bytes = 0;
unsigned long num_alloc = 0;
unsigned long num_free = 0;
long long byte_in_use = 0; //memory allocated but not freed, < 0 will trigger an error

unsigned long long d_allocated_bytes = 0;

/* HOST MEM*/
static void *(*real_malloc)(size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void (*real_free)(void *) = NULL;

static int alloc_init_pending = 0;

__attribute__((destructor))
static void output() {
  FILE *fp;
  
  fp = fopen("/tmp/mem_Analysis.txt", "a");

  pid_t pid = getpid();

  fprintf(fp, "pid %d, num alloc %lu, num free %lu, total mem operation %lu, total memory allocated %llu\n", pid, num_alloc, num_free, /* NO overflow handling */num_free + num_alloc, allocated_bytes);

  fclose(fp);
}

/* Load original allocation routines at first use */
static void alloc_init(void) {
  alloc_init_pending = 1;
  real_malloc = dlsym(RTLD_NEXT, "malloc");
  real_realloc = dlsym(RTLD_NEXT, "realloc");
  real_calloc = dlsym(RTLD_NEXT, "calloc");
  real_free = dlsym(RTLD_NEXT, "free");

  if (!real_malloc || !real_realloc || !real_calloc || !real_free) {
    fputs("wrapper.so: Unable to hook HOST allocation!\n", stderr);
    fputs(dlerror(), stderr);
    exit(1);
  } else {
    fputs("wrapper.so: Successfully hooked HOST allocation!\n", stderr);
  }

  alloc_init_pending = 0;
}

#define ZALLOC_MAX 1024
static void *zalloc_list[ZALLOC_MAX];
static size_t zalloc_cnt = 0;

/* dlsym needs dynamic memory before we can resolve the real memory
 * allocator routines. To support this, we offer simple mmap-based
 * allocation during alloc_init_pending.
 * We support a max. of ZALLOC_MAX allocations.
 *
 * On the tested Ubuntu 16.04 with glibc-2.23, this happens only once.
 */
void *zalloc_internal(size_t size) {
  fputs("wrapper.so: zalloc_internal called\n", stderr);
  if (zalloc_cnt >= ZALLOC_MAX - 1) {
    fputs("wrapper.so: Out of internal memory\n", stderr);
    return NULL;
  }
  /* Anonymous mapping ensures that pages are zero'd */
  void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  if (MAP_FAILED == ptr) {
    perror("wrapper.so: zalloc_internal mmap failed");
    return NULL;
  }
  zalloc_list[zalloc_cnt++] = ptr; /* keep track for later calls to free */
  return ptr;
}

void free(void *ptr) {
  if (alloc_init_pending) {
    fputs("wrapper.so: free internal\n", stderr);
    /* Ignore 'free' during initialization and ignore potential mem leaks
     * On the tested system, this did not happen
     */
    return;
  }
  if (!real_malloc) {
    alloc_init();
  }
  for (size_t i = 0; i < zalloc_cnt; i++) {
    if (zalloc_list[i] == ptr) {
      /* If dlsym cleans up its dynamic memory allocated with zalloc_internal,
       * we intercept and ignore it, as well as the resulting mem leaks.
       * On the tested system, this did not happen
       */
      return;
    }
  }
  __atomic_add_fetch(&num_free, 1, __ATOMIC_RELAXED);
  real_free(ptr);
}

void *malloc(size_t size) {
  if (alloc_init_pending) {
    fputs("wrapper.so: malloc internal\n", stderr);
    return zalloc_internal(size);
  }
  if (!real_malloc) {
    alloc_init();
  }
  void *result = real_malloc(size);
  __atomic_add_fetch(&num_alloc, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&allocated_bytes, (unsigned long long)size, __ATOMIC_RELAXED);
  return result;
}

void *realloc(void *ptr, size_t size) {
  if (alloc_init_pending) {
    fputs("wrapper.so: realloc internal\n", stderr);
    if (ptr) {
      fputs("wrapper.so: realloc resizing not supported\n", stderr);
      exit(1);
    }
    return zalloc_internal(size);
  }
  if (!real_malloc) {
    alloc_init();
  }
  __atomic_add_fetch(&num_alloc, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&allocated_bytes, (unsigned long long)size, __ATOMIC_RELAXED);
  return real_realloc(ptr, size);
}

void *calloc(size_t nmemb, size_t size) {
  if (alloc_init_pending) {
    fputs("wrapper.so: calloc internal\n", stderr);
    /* Be aware of integer overflow in nmemb*size.
     * Can only be triggered by dlsym */
    return zalloc_internal(nmemb * size);
  }
  if (!real_malloc) {
    alloc_init();
  }
  __atomic_add_fetch(&num_alloc, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&allocated_bytes, (unsigned long long)size, __ATOMIC_RELAXED);
  return real_calloc(nmemb, size);
}
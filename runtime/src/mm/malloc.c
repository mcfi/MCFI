#include <string.h>
#include <errno.h>
#include <mm.h>
#include <io.h>
#include <atomic.h>

#define inline inline __attribute__((always_inline))

void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
void *mremap(void *, size_t, size_t, int, void *);
int madvise(void *, size_t, int);

struct chunk {
  size_t psize, csize;
  struct chunk *next, *prev;
};

struct bin {
  int lock[2];
  struct chunk *head;
  struct chunk *tail;
};

static struct {
  uintptr_t brk;
  size_t *heap;
  uint64_t binmap;
  uintptr_t brk0;
  struct bin bins[64];
  int brk_lock[2];
  int free_lock[2];
} mal;

/* our fake brk region size is 1GB, which is tunable */
#define BRKSIZE 0x40000000UL

#define SIZE_ALIGN (4*sizeof(size_t))
#define SIZE_MASK (-SIZE_ALIGN)
#define OVERHEAD (2*sizeof(size_t))
#define MMAP_THRESHOLD (0x1c00*SIZE_ALIGN)
#define DONTCARE 16
#define RECLAIM 163840

#define CHUNK_SIZE(c) ((c)->csize & -2)
#define CHUNK_PSIZE(c) ((c)->psize & -2)
#define PREV_CHUNK(c) ((struct chunk *)((char *)(c) - CHUNK_PSIZE(c)))
#define NEXT_CHUNK(c) ((struct chunk *)((char *)(c) + CHUNK_SIZE(c)))
#define MEM_TO_CHUNK(p) (struct chunk *)((char *)(p) - OVERHEAD)
#define CHUNK_TO_MEM(c) (void *)((char *)(c) + OVERHEAD)
#define BIN_TO_CHUNK(i) (MEM_TO_CHUNK(&mal.bins[i].head))

#define C_INUSE  ((size_t)1)

#define IS_MMAPPED(c) !((c)->csize & (C_INUSE))


/* Synchronization tools */

static inline void lock(volatile int *lk)
{
}

static inline void unlock(volatile int *lk)
{
}

static inline void lock_bin(int i)
{
  if (!mal.bins[i].head)
    mal.bins[i].head = mal.bins[i].tail = BIN_TO_CHUNK(i);
}

static inline void unlock_bin(int i)
{
}

static int first_set(uint64_t x)
{
  return a_ctz_64(x);
}

static int bin_index(size_t x)
{
  x = x / SIZE_ALIGN - 1;
  if (x <= 32) return x;
  if (x > 0x1c00) return 63;
  return ((union { float v; uint32_t r; }){(int)x}.r>>21) - 496;
}

static int bin_index_up(size_t x)
{
  x = x / SIZE_ALIGN - 1;
  if (x <= 32) return x;
  return ((union { float v; uint32_t r; }){(int)x}.r+0x1fffff>>21) - 496;
}

#if 0
void __dump_heap(int x)
{
  struct chunk *c;
  int i;
  for (c = (void *)mal.heap; CHUNK_SIZE(c); c = NEXT_CHUNK(c))
    fprintf(stderr, "base %p size %zu (%d) flags %d/%d\n",
            c, CHUNK_SIZE(c), bin_index(CHUNK_SIZE(c)),
            c->csize & 15,
            NEXT_CHUNK(c)->psize & 15);
  for (i=0; i<64; i++) {
    if (mal.bins[i].head != BIN_TO_CHUNK(i) && mal.bins[i].head) {
      fprintf(stderr, "bin %d: %p\n", i, mal.bins[i].head);
      if (!(mal.binmap & 1ULL<<i))
        fprintf(stderr, "missing from binmap!\n");
    } else if (mal.binmap & 1ULL<<i)
      fprintf(stderr, "binmap wrongly contains %d!\n", i);
  }
}
#endif

static struct chunk *expand_heap(size_t n)
{
  struct chunk *w;
  uintptr_t new;

  lock(mal.brk_lock);

  if (n > SIZE_MAX - mal.brk - 2*PAGE_SIZE) goto fail;
  new = mal.brk + n + SIZE_ALIGN + PAGE_SIZE - 1 & -PAGE_SIZE;
  n = new - mal.brk;

  if (new > mal.brk0 + BRKSIZE) goto fail;
  //if (__brk(new) != new) goto fail;

  w = MEM_TO_CHUNK(new);
  w->psize = n | C_INUSE;
  w->csize = 0 | C_INUSE;

  w = MEM_TO_CHUNK(mal.brk);
  w->csize = n | C_INUSE;
  mal.brk = new;
	
  unlock(mal.brk_lock);

  return w;
 fail:
  unlock(mal.brk_lock);
  errn = ENOMEM;
  return 0;
}

static int init_malloc(size_t n)
{
  static int init, waiters;
  int state;
  struct chunk *c;

  if (init == 1) return 0;

  /* initialize a fake brk region */
  mal.brk = (uintptr_t)mmap(0, BRKSIZE, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (mal.brk == (uintptr_t)-1) {
    dprintf(STDERR_FILENO, "[init_malloc] mmap failed with %d\n", errn);
    return -1;
  }
  mal.brk0 = mal.brk;

  /*
#ifdef SHARED
  mal.brk = mal.brk + PAGE_SIZE-1 & -PAGE_SIZE;
#endif
  */
  mal.brk = mal.brk + 2*SIZE_ALIGN-1 & -SIZE_ALIGN;

  c = expand_heap(n);

  if (!c) {
    return -1;
  }

  mal.heap = (void *)c;
  c->psize = 0 | C_INUSE;
  free(CHUNK_TO_MEM(c));

  init = 1;

  return 1;
}

static int adjust_size(size_t *n)
{
  /* Result of pointer difference must fit in ptrdiff_t. */
  if (*n-1 > PTRDIFF_MAX - SIZE_ALIGN - PAGE_SIZE) {
    if (*n) {
      errn = ENOMEM;
      return -1;
    } else {
      *n = SIZE_ALIGN;
      return 0;
    }
  }
  *n = (*n + OVERHEAD + SIZE_ALIGN - 1) & SIZE_MASK;
  return 0;
}

static void unbin(struct chunk *c, int i)
{
  if (c->prev == c->next) {
    mal.binmap &= ~(1ULL<<i);
    //a_and_64(&mal.binmap, ~(1ULL<<i));
  }
  c->prev->next = c->next;
  c->next->prev = c->prev;
  c->csize |= C_INUSE;
  NEXT_CHUNK(c)->psize |= C_INUSE;
}

static int alloc_fwd(struct chunk *c)
{
  int i;
  size_t k;
  while (!((k=c->csize) & C_INUSE)) {
    i = bin_index(k);
    lock_bin(i);
    if (c->csize == k) {
      unbin(c, i);
      unlock_bin(i);
      return 1;
    }
    unlock_bin(i);
  }
  return 0;
}

static int alloc_rev(struct chunk *c)
{
  int i;
  size_t k;
  while (!((k=c->psize) & C_INUSE)) {
    i = bin_index(k);
    lock_bin(i);
    if (c->psize == k) {
      unbin(PREV_CHUNK(c), i);
      unlock_bin(i);
      return 1;
    }
    unlock_bin(i);
  }
  return 0;
}


/* pretrim - trims a chunk _prior_ to removing it from its bin.
 * Must be called with i as the ideal bin for size n, j the bin
 * for the _free_ chunk self, and bin j locked. */
static int pretrim(struct chunk *self, size_t n, int i, int j)
{
  size_t n1;
  struct chunk *next, *split;

  /* We cannot pretrim if it would require re-binning. */
  if (j < 40) return 0;
  if (j < i+3) {
    if (j != 63) return 0;
    n1 = CHUNK_SIZE(self);
    if (n1-n <= MMAP_THRESHOLD) return 0;
  } else {
    n1 = CHUNK_SIZE(self);
  }
  if (bin_index(n1-n) != j) return 0;

  next = NEXT_CHUNK(self);
  split = (void *)((char *)self + n);

  split->prev = self->prev;
  split->next = self->next;
  split->prev->next = split;
  split->next->prev = split;
  split->psize = n | C_INUSE;
  split->csize = n1-n;
  next->psize = n1-n;
  self->csize = n | C_INUSE;
  return 1;
}

static void trim(struct chunk *self, size_t n)
{
  size_t n1 = CHUNK_SIZE(self);
  struct chunk *next, *split;

  if (n >= n1 - DONTCARE) return;

  next = NEXT_CHUNK(self);
  split = (void *)((char *)self + n);

  split->psize = n | C_INUSE;
  split->csize = n1-n | C_INUSE;
  next->psize = n1-n | C_INUSE;
  self->csize = n | C_INUSE;

  free(CHUNK_TO_MEM(split));
}

void *malloc(size_t n)
{
  struct chunk *c;
  int i, j;

  if (adjust_size(&n) < 0) return 0;

  if (n > MMAP_THRESHOLD) {
    size_t len = n + OVERHEAD + PAGE_SIZE - 1 & -PAGE_SIZE;
    char *base = mmap(0, len, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (base == (void *)-1) return 0;
    c = (void *)(base + SIZE_ALIGN - OVERHEAD);
    c->csize = len - (SIZE_ALIGN - OVERHEAD);
    c->psize = SIZE_ALIGN - OVERHEAD;
    return CHUNK_TO_MEM(c);
  }

  i = bin_index_up(n);
  for (;;) {
    uint64_t mask = mal.binmap & -(1ULL<<i);
    if (!mask) {
      if (init_malloc(n) > 0) continue;
      c = expand_heap(n);
      if (!c) return 0;
      if (alloc_rev(c)) {
        struct chunk *x = c;
        c = PREV_CHUNK(c);
        NEXT_CHUNK(x)->psize = c->csize =
          x->csize + CHUNK_SIZE(c);
      }
      break;
    }
    j = first_set(mask);
    lock_bin(j);
    c = mal.bins[j].head;
    if (c != BIN_TO_CHUNK(j) && j == bin_index(c->csize)) {
      if (!pretrim(c, n, i, j)) unbin(c, j);
      unlock_bin(j);
      break;
    }
    unlock_bin(j);
  }

  /* Now patch up in case we over-allocated */
  trim(c, n);

  return CHUNK_TO_MEM(c);
}

void *realloc(void *p, size_t n)
{
  struct chunk *self, *next;
  size_t n0, n1;
  void *new;

  if (!p) return malloc(n);

  if (adjust_size(&n) < 0) return 0;

  self = MEM_TO_CHUNK(p);
  n1 = n0 = CHUNK_SIZE(self);

  if (IS_MMAPPED(self)) {
    size_t extra = self->psize;
    char *base = (char *)self - extra;
    size_t oldlen = n0 + extra;
    size_t newlen = n + extra;
    /* Crash on realloc of freed chunk */
    if (extra & 1) a_crash();
    if (newlen < PAGE_SIZE && (new = malloc(n))) {
      memcpy(new, p, n-OVERHEAD);
      free(p);
      return new;
    }
    newlen = (newlen + PAGE_SIZE-1) & -PAGE_SIZE;
    if (oldlen == newlen) return p;
    base = mremap(base, oldlen, newlen, MREMAP_MAYMOVE, 0);
    if (base == (void *)-1)
      return newlen < oldlen ? p : 0;
    self = (void *)(base + extra);
    self->csize = newlen - extra;
    return CHUNK_TO_MEM(self);
  }

  next = NEXT_CHUNK(self);

  /* Crash on corrupted footer (likely from buffer overflow) */
  if (next->psize != self->csize) a_crash();

  /* Merge adjacent chunks if we need more space. This is not
   * a waste of time even if we fail to get enough space, because our
   * subsequent call to free would otherwise have to do the merge. */
  if (n > n1 && alloc_fwd(next)) {
    n1 += CHUNK_SIZE(next);
    next = NEXT_CHUNK(next);
  }
  /* FIXME: find what's wrong here and reenable it..? */
  if (0 && n > n1 && alloc_rev(self)) {
    self = PREV_CHUNK(self);
    n1 += CHUNK_SIZE(self);
  }
  self->csize = n1 | C_INUSE;
  next->psize = n1 | C_INUSE;

  /* If we got enough space, split off the excess and return */
  if (n <= n1) {
    //memmove(CHUNK_TO_MEM(self), p, n0-OVERHEAD);
    trim(self, n);
    return CHUNK_TO_MEM(self);
  }

  /* As a last resort, allocate a new chunk and copy to it. */
  new = malloc(n-OVERHEAD);
  if (!new) return 0;
  memcpy(new, p, n0-OVERHEAD);
  free(CHUNK_TO_MEM(self));
  return new;
}

void free(void *p)
{
  struct chunk *self = MEM_TO_CHUNK(p);
  struct chunk *next;
  size_t final_size, new_size, size;
  int reclaim=0;
  int i;

  if (!p) return;

  if (IS_MMAPPED(self)) {
    size_t extra = self->psize;
    char *base = (char *)self - extra;
    size_t len = CHUNK_SIZE(self) + extra;
    /* Crash on double free */
    if (extra & 1) a_crash();
    munmap(base, len);
    return;
  }

  final_size = new_size = CHUNK_SIZE(self);
  next = NEXT_CHUNK(self);

  /* Crash on corrupted footer (likely from buffer overflow) */
  if (next->psize != self->csize) a_crash();

  for (;;) {
    /* Replace middle of large chunks with fresh zero pages */
    if (reclaim && (self->psize & next->csize & C_INUSE)) {
      uintptr_t a = (uintptr_t)self + SIZE_ALIGN+PAGE_SIZE-1 & -PAGE_SIZE;
      uintptr_t b = (uintptr_t)next - SIZE_ALIGN & -PAGE_SIZE;
      madvise((void *)a, b-a, MADV_DONTNEED);
    }

    if (self->psize & next->csize & C_INUSE) {
      self->csize = final_size | C_INUSE;
      next->psize = final_size | C_INUSE;
      i = bin_index(final_size);
      lock_bin(i);
      lock(mal.free_lock);
      if (self->psize & next->csize & C_INUSE)
        break;
      unlock(mal.free_lock);
      unlock_bin(i);
    }

    if (alloc_rev(self)) {
      self = PREV_CHUNK(self);
      size = CHUNK_SIZE(self);
      final_size += size;
      if (new_size+size > RECLAIM && (new_size+size^size) > size)
        reclaim = 1;
    }

    if (alloc_fwd(next)) {
      size = CHUNK_SIZE(next);
      final_size += size;
      if (new_size+size > RECLAIM && (new_size+size^size) > size)
        reclaim = 1;
      next = NEXT_CHUNK(next);
    }
  }

  self->csize = final_size;
  next->psize = final_size;
  unlock(mal.free_lock);

  self->next = BIN_TO_CHUNK(i);
  self->prev = mal.bins[i].tail;
  self->next->prev = self;
  self->prev->next = self;

  if (!(mal.binmap & 1ULL<<i)) {
    mal.binmap |= 1ULL<<i;
    //a_or_64(&mal.binmap, 1ULL<<i);
  }
  unlock_bin(i);
}

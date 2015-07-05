#include <def.h>
#include <syscall.h>
#include <mm.h>
#include <io.h>
#include <string.h>
#include <tcb.h>
#include <errno.h>
#include "pager.h"
#include <time.h>
#include <cfggen/cfggen.h>

static void* prog_brk = 0;
static void* max_brk = 0;
#define BRK_LEAP 0x800000

static TCB *tcb_list = 0;

/* tracks which thread escapes the untrusted space for how many times */
dict *thread_escape_map = 0;

#ifdef COLLECT_STAT
static unsigned int at_patch_count = 0;
static unsigned int func_addr_activation_count = 0;
static unsigned int func_entry_patch_count = 0;
static unsigned int vmtd_activation_count = 0;
static unsigned int lp_activation_count = 0;
static unsigned int radc_patch_count = 0;
static unsigned int raic_patch_count = 0;
static unsigned int eqc_callgraph_count = 0;
static unsigned int eqc_retgraph_count = 0;
static unsigned long icj_count = 0;
#endif

static int cfggened = FALSE;

extern code_module *modules;
extern str *stringpool;
static dict *patch_compensate = 0;
extern void *table; /* table region defined in main.c */
extern struct Vmmap VM;

extern unsigned int alloc_bid_slot(void);

void set_tcb(unsigned long sb_tcb) {
  if (sb_tcb > FourGB) {
    report_error("[set_tcb] sandbox tcb is out of sandbox\n");
  }

  TCB* tcb = thread_self();
  tcb->tcb_inside_sandbox = (void*)sb_tcb;
  tcb_list = tcb; /* add the main thread to the tcb list */
  dict_add(&thread_escape_map, tcb, 0);
}

void* allocset_tcb(unsigned long sb_tcb) {
  TCB* tcb = alloc_tcb();
  
  if (sb_tcb > FourGB) {
    report_error("[set_tcb] sandbox tcb is out of sandbox\n");
  }
  
  tcb->tcb_inside_sandbox = (void*)sb_tcb;

  dict_add(&thread_escape_map, tcb, 0);

  /* add tcb to the tcb_list */
  tcb->next = tcb_list;
  tcb_list = tcb;
  return tcb;
}

/**
 * For those tcb's marked as remove, remove them from the list and free memory.
 */
static void remove_tcb_marked(void) {
  TCB *tcb = tcb_list;
  TCB *p;
  /* remove the marked nodes except the head */
  while (0 != (p = tcb->next)) {
    if (p->remove) {
      tcb->next = p->next;
      dealloc_tcb(p);
    } else
      tcb = tcb->next;
  }
  /* remove the head if necessary */
  if (tcb_list->remove) {
    tcb = tcb_list;
    tcb_list = tcb->next;
    dealloc_tcb(tcb);
  }
}

void free_tcb(void *user_tcb) {
  TCB *tcb;
  /* remove the remove-marked tcbs.
     We shouldn't directly remove the tcb because most of the time a thread
     removes itself's tcb, and doing so would crash the program because the
     control flow cannot be returned back to the thread */
  remove_tcb_marked();
  tcb = tcb_list;

  if (!tcb) {
    dprintf(STDERR_FILENO, "[free_tcb] tcb_list is empty\n");
    quit(-1);
  }

  if ((unsigned long)user_tcb > FourGB) {
    dprintf(STDERR_FILENO, "[free_tcb] user_tcb is outside of sandbox\n");
    quit(-1);
  }

  while (tcb) {
    if (tcb->tcb_inside_sandbox == user_tcb) {
      tcb->remove = 1;
      dict_del(&thread_escape_map, tcb);
#ifdef COLLECT_STAT
      icj_count += tcb->icj_count;
#endif
      break;
    }
    tcb = tcb->next;
  }
}

static graph *fats_in_code = 0;

void patch_at(unsigned long patchpoint) {
  //dprintf(STDERR_FILENO, "patched at %lx\n", patchpoint);
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    if (patchpoint >= m->base_addr &&
        patchpoint < m->base_addr + m->sz) {
      found = TRUE;
      break;
    }
  }
  assert(found && patchpoint % 8 == 0);
  patchpoint -= m->base_addr;
  keyvalue *kv = dict_find(m->at_func, (void*)patchpoint);
  assert(kv);
#ifndef NO_ONLINE_PATCHING
  vertex *v = dict_find(fats_in_code, kv->value);
  if (v) {
    vertex *tmp, *atsite;
    HASH_ITER(hh, (graph*)(v->value), atsite, tmp) {
      assert (cfggened);
      unsigned long *ptid = (unsigned long*)(table + (unsigned long)atsite->key);
      if (!(*ptid & 1)) {
        *ptid |= 1;
        // Luckily, libc does not take any function address before the
        // the CFG generation.
        //else {
        //  /* add all possible function addresses to patch_compensate */
        //  dict_add(&patch_compensate, table + (unsigned long)atsite->key, 0);
        //}
        //dprintf(STDERR_FILENO, "Patch %x\n", (unsigned long)atsite->key);
#ifdef PROFILING
        {
          struct timeval tv;
          gettimeofday(&tv);
          dprintf(STDERR_FILENO, "[%lu:%lu] Activated Function at 0x%x\n",
                  tv.tv_sec, tv.tv_usec, (unsigned long)atsite->key);
        }
#endif
#ifdef COLLECT_STAT
        ++func_addr_activation_count;
#endif
      }
    }
    g_del_vertex(&fats_in_code, v->key);
  } /* else the address is also taken in data */
#endif
#ifdef COLLECT_STAT
  ++at_patch_count;
#endif

  /* the patch should be performed after the tary id is set valid */
  char *p = (char*)(m->osb_base_addr + patchpoint - 8);
  char patch[8];
  memcpy(patch, p, 3);
  static const char fivebytenop[5] = {0x0f, 0x1f, 0x44, 0x00, 0x00};
  memcpy(patch+3, fivebytenop, 5);
  *(unsigned long*)p = *(unsigned long*)patch;
}

static dict* vmtd = 0;

void patch_entry(unsigned long patchpoint) {
#ifndef NO_ONLINE_PATCHING
  //dprintf(STDERR_FILENO, "patched entry %x\n", patchpoint);
  code_module *m;
  int found = FALSE;

  DL_FOREACH(modules, m) {
    if (patchpoint >= m->base_addr &&
        patchpoint < m->base_addr + m->sz) {
      found = TRUE;
      break;
    }
  }
  assert(found && patchpoint % 8 == 0);
  patchpoint -= m->base_addr;
  assert(cfggened);
  keyvalue *kv_ctor = dict_find(m->ctor, (void*)patchpoint);
  if (kv_ctor) {
    //dprintf(STDERR_FILENO, "ctor: %s\n", kv_ctor->value);
    // kv_ctor->value is the actual constructor's demangled name
    keyvalue *kv_methods = dict_find(m->vtable, kv_ctor->value);
    if (kv_methods) {
      node *n;
      DL_FOREACH((node*)(kv_methods->value), n) {
        // for each virtual method, we set its tary id to be valid
        keyvalue *kv_m = dict_find(vmtd, n->val);
        if (kv_m) {
          keyvalue *v, *tmp;
          HASH_ITER(hh, (dict*)(kv_m->value), v, tmp) {
            unsigned long *ptid = (unsigned long*)(table + (unsigned long)v->key);
            if (!(*ptid & 1)) {
              *ptid |= 1;
#ifdef COLLECT_STAT
              ++vmtd_activation_count;
#endif
#ifdef PROFILING
              {
                struct timeval tv;
                gettimeofday(&tv);
                dprintf(STDERR_FILENO, "[%lu:%lu] Activated Virtual Method at 0x%x\n",
                        tv.tv_sec, tv.tv_usec, (unsigned long)v->key);
              }
#endif
            }
          }
        }
      }
      l_free((node**)&(kv_methods->value));
      /* remove the class */
      dict_del(&(m->vtable), kv_ctor->value);
    }
  }
  keyvalue *kv_lp = dict_find(m->flp, (void*)patchpoint);
  if (kv_lp) {
    keyvalue *v, *tmp;
    HASH_ITER(hh, (dict*)(kv_lp->value), v, tmp) {
      *(char*)(table + m->base_addr + (uintptr_t)v->key) = LPV;
#ifdef COLLECT_STAT
      ++lp_activation_count;
#endif
#ifdef PROFILING
      {
        struct timeval tv;
        gettimeofday(&tv);
        dprintf(STDERR_FILENO, "[%lu:%lu] Activated LandingPad at 0x%x\n",
                tv.tv_sec, tv.tv_usec, (unsigned long)v->key + m->base_addr);
      }
#endif
    }
    g_del_vertex(&(m->flp), kv_lp->key);
  }
#ifdef COLLECT_STAT
  ++func_entry_patch_count;
#endif
  // kv_methods->value is a list of virtual methods
  keyvalue *kv = dict_find(m->func_orig, (void*)patchpoint);
  assert(kv);
  char *p = (char*)(m->osb_base_addr + patchpoint);
  *(unsigned long*)p = (unsigned long)(kv->value);
#endif
}

void patch_call(unsigned long patchpoint) {
#ifndef NO_ONLINE_PATCHING
  //dprintf(STDERR_FILENO, "patched call %lx\n", patchpoint);
  static dict* patched_ra = 0;
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    if (patchpoint >= m->base_addr &&
        patchpoint < m->base_addr + m->sz) {
      found = TRUE;
      break;
    }
  }
  assert(found);
  assert(patchpoint % 8 == 0 ||
         (patchpoint + 3) % 8 == 0||
         (patchpoint + 2) % 8 == 0);

  /* Different CPU cores might cache the same unpatched
   * instructions. So any duplicated run will be a nop.
   */
  if (dict_find(patched_ra, (void*)patchpoint))
    return;

  dict_add(&patched_ra, (void*)patchpoint, (void*)0);

#ifdef COLLECT_STAT
  if (patchpoint % 8 == 0)
    ++radc_patch_count;
  else
    ++raic_patch_count;
#endif
  patchpoint = (patchpoint + 7) / 8 * 8;
  //dprintf(STDERR_FILENO, "%x, %x\n", m->base_addr, patchpoint - m->base_addr);
  keyvalue *patch = dict_find(m->ra_orig, (const void*)(patchpoint - m->base_addr));
  assert(patch);
  //dprintf(STDERR_FILENO, "%x, %x, %lx, %x\n",
  //        m->base_addr, patch->key, patch->value, patch_count);

  if (cfggened) {
    *((unsigned long*)(table + m->base_addr + (unsigned long)patch->key)) |= 1;
  } else {
    dict_add(&patch_compensate, table + m->base_addr + (unsigned long)patch->key, 0);
  }

  /* the patch should be performed after the tary id is set valid */
  unsigned long *p =
    (unsigned long*)(m->osb_base_addr + (unsigned long)patch->key - 8);
  *p = (unsigned long)patch->value;
#ifdef PROFILING
  {
    struct timeval tv;
    gettimeofday(&tv);
    dprintf(STDERR_FILENO, "[%lu:%lu] Activated Return Address at 0x%x\n",
            tv.tv_sec, tv.tv_usec, patchpoint);
  }
#endif
#endif
}

#define ROCK_INVALID -1
#define ROCK_DATA    0
#define ROCK_CODE    1
#define ROCK_OFFSET  2
#define ROCK_VERIFY  4
#define ROCK_COPY    8
#define ROCK_REPLACE 16

/* if [base, len) in code areas, return ROCK_CODE;
   else if in data areas, return ROCK_DATA;
   else return ROCK_INVALID
*/
static size_t count_bits(const unsigned char* bmp, unsigned long base, size_t len) {
  size_t bitsum = 0;
  unsigned long byte;
  for (byte = base; byte < base + len; byte++) {
    bitsum += ((bmp[byte / 8] & (1 << (byte % 8))) != 0) ? 1 : 0;
  }
  return bitsum;
}

static int which_area(const unsigned char* cdbmp, unsigned long base, size_t len) {
  size_t bitsum = 0;
  size_t bitlen = len;

  if (len < 32) {
    bitsum += count_bits(cdbmp, base, len);
  } else {
    if ((base & 7) != 0) {
      unsigned long base_align = base & (~7);
      bitsum += count_bits(cdbmp, base, 8 - (base - base_align));
      len -= (8 - (base - base_align));
      base = base_align + 8;
    }
    if ((len & 63) != 0) {
      size_t trail_len = (len & 63);
      bitsum += count_bits(cdbmp, base + (len - trail_len), trail_len);
    }
    len >>= 6; // len /= 64
    size_t i;
    unsigned long*p = (unsigned long*)&cdbmp[base/8];
    for (i = 0; i < len; i++) {
      bitsum += __builtin_popcountl(*p);
    }
  }
  if (bitsum == bitlen)
    return ROCK_CODE;
  else if (bitsum == 0)
    return ROCK_DATA;
  else
    return ROCK_INVALID;
}

static void set_code(unsigned char *cdbmp, unsigned long base, size_t len) {
  if (len < 16) { // tunable
    unsigned long byte;
    for (byte = base; byte < base + len; byte++) {
      cdbmp[byte / 8] |= (1 << (byte % 8));
    }
  } else {
    if ((base & 7) != 0) {
      unsigned long base_align = base & (~7);
      cdbmp[base_align] |= ((0xff >> (base - base_align)) << (base - base_align));
      len -= (8 - (base - base_align));
      base = base_align + 8;
    }
    if ((len & 7) != 0) {
      size_t trail_len = (len & 7);
      unsigned long trail_base = base + (len - trail_len);
      cdbmp[trail_base/8] |= ((1 << trail_len) - 1);
    }
    size_t i;
    len /= 8;
    base /= 8;
    for (i = 0; i < len; i++) {
      cdbmp[base + i] = 0xff;
    }
  }
}

static void set_data(unsigned char *cdbmp, unsigned long base, size_t len) {
  if (len < 16) { // tunable
    unsigned long byte;
    for (byte = base; byte < base + len; byte++) {
      cdbmp[byte / 8] &= (~(1 << (byte % 8)));
    }
  } else {
    if ((base & 7) != 0) {
      unsigned long base_align = base & (~7);
      cdbmp[base_align] &= (0xff >> (8 - (base - base_align)));
      len -= (8 - (base - base_align));
      base = base_align + 8;
    }
    if ((len & 7) != 0) {
      size_t trail_len = (len & 7);
      unsigned long trail_base = base + (len - trail_len);
      cdbmp[trail_base/8] &= (0xff << trail_len);
    }
    size_t i;
    len /= 8;
    base /= 8;
    for (i = 0; i < len; i++) {
      cdbmp[base + i] = 0;
    }
  }
}

static int range_overlap(uintptr_t r1, size_t len1,
                         uintptr_t r2, size_t len2) {
  if (r1 == r2)
    return TRUE;
  if (r1 < r2 && r1 + len1 > r2)
    return TRUE;
  if (r2 < r1 && r2 + len2 > r1)
    return TRUE;
  return FALSE;
}

static int insecure_overlap_rdonly(uintptr_t start, size_t len, int prot) {
  if (prot & PROT_WRITE) {
    code_module *m;
    DL_FOREACH(modules, m) {
      if (!m->code_heap && (range_overlap(start, len, m->base_addr, m->sz) ||
                            range_overlap(start, len, m->gotplt, m->gotpltsz))) {
        dprintf(STDERR_FILENO, "[insecure_overlap_rdonly] 0x%x, 0x%x, %d, 0x%lx\n",
                start, len, prot, thread_self()->continuation);
        return TRUE;
      }
    }
  }
  return FALSE;
}

code_module* in_code_heap(uintptr_t start, size_t len) {
  code_module* m;
  DL_FOREACH(modules, m) {
    if (m->code_heap && start >= m->base_addr && start + len <= m->base_addr + m->sz)
      return m;
  }
  return 0;
}

void *rock_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
  void *result = MAP_FAILED;
  uintptr_t page = 0;
  size_t pages = RoundToPage(len) >> PAGESHIFT;

  if ((unsigned long)start & ((1<<PAGESHIFT)-1)) {
    return (void*)-EINVAL;
  }

  /* if the program tries to map a fixed out-sandbox address, return failure */
  if ((unsigned long)start > FourGB && (flags & MAP_FIXED)) {
    return (void*)-ENOMEM;
  }

  if (len > FourGB)
    return (void*)-ENOMEM;

  /* see if we are handling fixed mapping in the code_heap */
  if (0 != start && (flags & MAP_FIXED)) {
    /* TODO: handle the case when [start, len) overlaps a code heap */
    code_module *m = in_code_heap((uintptr_t)start, len);
    if (m) {
      if ((prot & PROT_WRITE) && (prot & PROT_EXEC)) {
        dprintf(STDERR_FILENO, "[rock_map] mapping WX code heap %p, %x\n", start, len);
        quit(-1);
      } else {
        if (ROCK_DATA != which_area(m->code_data_bitmap, (uintptr_t)start - m->base_addr, len)) {
          dprintf(STDERR_FILENO, "[rock_map] mapping existing code\n");
          quit(-1);
        }
      }

      int rs = mprotect(start, len, prot);
      if (rs == 0)
        return start;
      else
        return (void*)-ENOMEM;
    }
  }

  if (prot & PROT_EXEC) {
    dprintf(STDERR_FILENO,
            "[rock_mmap] mmap(%p, %lx, %d, %d, %d, %ld) maps executable pages!\n",
            start, len, prot, flags, fd, off);
    quit(-1);
  }
  /* not fixed mapping */
  if (!(flags & MAP_FIXED)) {
    if ((unsigned long)start > FourGB || start == 0)
      page = VmmapFindSpace(&VM, pages);
    else
      page = VmmapFindMapSpaceAboveHint(&VM, (uintptr_t)start, pages);

    /* no memory is available */
    if (!page) {
      return (void*)-ENOMEM;
    }
  } else {
    /* fixed mapping, check whether the mapping would be safe. */
    if (len > FourGB || page + len > FourGB)
      return (void*)-ENOMEM;
    page = (uintptr_t)start >> PAGESHIFT;
  }
  /* check whether the map would mess up the read-only text and .got.plt pages */
  if (insecure_overlap_rdonly((uintptr_t)(page << PAGESHIFT), len, prot)) {
    dprintf(STDERR_FILENO, "[rock_mmap] insecure_overlap_rdonly\n");
    quit(-1);
  }
  result = mmap((void*)(page << PAGESHIFT), len, prot, flags | MAP_FIXED, fd, off);
  if (result == (void*)(page << PAGESHIFT))
    VmmapAddWithOverwrite(&VM, page,
                          pages,
                          prot,
                          prot,
                          VMMAP_ENTRY_ANONYMOUS);
  return result;
}

int rock_mprotect(void *addr, size_t len, int prot) {
  if ((unsigned long) addr > FourGB || len > FourGB || (prot & PROT_EXEC)) {
    dprintf(STDERR_FILENO, "[rock_mprotect] mprotect(%lx, %lx, %d) is insecure!\n",
            (size_t)addr, len, prot);
    quit(-1);
  }
  if (insecure_overlap_rdonly((uintptr_t)addr, len, prot)) {
    dprintf(STDERR_FILENO, "[rock_mprotect] mprotect(%lx, %lx, %d) overlapps rdonly\n");
    quit(-1);
  }
  return mprotect(addr, len, prot);
}

int rock_munmap(void *start, size_t len) {
  /* return munmap(start, len); */
  if ((unsigned long)start > FourGB ||
      (unsigned long)len > FourGB ||
      (unsigned long)start + len > FourGB) {
    dprintf(STDERR_FILENO, "[rock_munmap] munmap(%ld, %lx) is insecure!\n",
            (size_t)start, len);
    quit(-1);
  }

  int rv = munmap(start, len);
  if(!rv) {
    VmmapRemove(&VM, RoundToPage((uintptr_t)start) >> PAGESHIFT,
                RoundToPage(len) >> PAGESHIFT, VMMAP_ENTRY_ANONYMOUS);
  }
  return rv;
}

void *rock_mremap(void *old_addr, size_t old_len, size_t new_len,
                  int flags, void* new_addr) {
  void *ptr = rock_mmap(0, new_len, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if ((long)ptr > 0) {
    memcpy(ptr, old_addr, old_len > new_len ? new_len : old_len);
    rock_munmap(old_addr, old_len);
  }
  return ptr;
}

void* rock_brk(void* newbrk) {
  if (!prog_brk) {
    /* return the initial break, which should be FourKB aligned */
    prog_brk = (void*)__syscall1(SYS_brk, (long)0);
    if ((unsigned long)prog_brk >= FourGB) {
      dprintf(STDERR_FILENO, "[rock_brk] initial program break is outside of sandbox\n");
      quit(-1);
    }
    /* By default, let's allocate BRK_LEAP to max brk. */
    max_brk = (void*)(RoundToPage(prog_brk) + BRK_LEAP);
    VmmapAdd(&VM, RoundToPage(prog_brk) >> PAGESHIFT,
             BRK_LEAP >> PAGESHIFT,
             PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE, VMMAP_ENTRY_ANONYMOUS);
    /* dprintf(STDERR_FILENO, "[rock_brk] initial break = %x\n", (unsigned long)prog_brk); */
  }

  if ((unsigned long)newbrk >= FourGB) {
    dprintf(STDERR_FILENO, "[rock_brk] newbrk is outside of sandbox\n");
    quit(-1);
  }
  prog_brk = (void*)__syscall1(SYS_brk, (long)newbrk);
  if (prog_brk > max_brk) {
    void *old_max_brk = max_brk;
    max_brk = (void*)(RoundToPage(prog_brk) + BRK_LEAP);
    VmmapAdd(&VM, CurPage(old_max_brk) >> PAGESHIFT,
             ((unsigned long)(max_brk - old_max_brk)) >> PAGESHIFT,
             PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE, VMMAP_ENTRY_ANONYMOUS);
  } else if (CurPage(max_brk) - RoundToPage(prog_brk) >= BRK_LEAP) {
    VmmapRemove(&VM, RoundToPage(prog_brk) >> PAGESHIFT,
                (CurPage(max_brk) - RoundToPage(prog_brk)) >> PAGESHIFT,
                VMMAP_ENTRY_ANONYMOUS);
  }
  return prog_brk;
}

char *load_elf(int fd, int is_exe, char **entry);

void *load_native_code(int fd) {
  return load_elf(fd, FALSE, 0);
}

static unsigned long version = 1;

static void print_cfgcc(void *cc) {
  vertex *v, *tmp;
  HASH_ITER(hh, (vertex*)cc, v, tmp) {
    if (_is_marked_ret(v->key))
      dprintf(STDERR_FILENO, "Return: %s\n", _unmark_ptr(v->key));
    else if (_is_marked_ra_dc(v->key))
      dprintf(STDERR_FILENO, "Rad: %s\n", _unmark_ptr(v->key));
    else if (_is_marked_ra_ic(v->key))
      dprintf(STDERR_FILENO, "Rai: %s\n", _unmark_ptr(v->key));
    else if (_is_marked_icj(v->key)) {
      dprintf(STDERR_FILENO, "ICF: %s\n", _unmark_ptr(v->key));
    } else
      dprintf(STDERR_FILENO, "Function: %s\n", v->key);
  }
}

static void update_thesc(void) {
  TCB *tcb = tcb_list;
  while (tcb) {
    if (!tcb->remove) {
      keyvalue *thesc = dict_find(thread_escape_map, tcb);
      assert(thesc);
      thesc->value = (void*)tcb->sandbox_escape;
    }
    tcb = tcb->next;
  }
}

static int safe(void) {
  TCB *tcb = tcb_list;
  int safe = TRUE;

  while (tcb) {
    if (!tcb->remove) {
      keyvalue *thesc = dict_find(thread_escape_map, tcb);
      assert(thesc);
      unsigned long thescs = tcb->sandbox_escape;
      /* neither the thread is in a system call, nor
       * has the thread invoked any system calls */
      if (!tcb->insyscall && thescs == (unsigned long)thesc->value)
        return FALSE;
    }
    tcb = tcb->next;
  }
  return TRUE;
}

static void compute_fic(dict **fats_in_code,
                        dict **fats_in_data,
                        graph *aliases_tc) {
#define ADD(fats, v) do {                               \
    if (dict_find(fats, v->key)) {                      \
      vertex *a, *atmp;                                 \
      HASH_ITER(hh, (graph*)(v->value), a, atmp) {      \
        dict_add(&fats, a->key, 0);                     \
      }                                                 \
    }                                                   \
  } while (0);
  keyvalue *v, *tmp;
  HASH_ITER(hh, aliases_tc, v, tmp) {
    ADD(*fats_in_code, v);
    ADD(*fats_in_data, v);
  }
#undef ADD
  /* for any function whose address is taken in data, remove
     its name from the set where each function's address is taken in code*/
  HASH_ITER(hh, *fats_in_code, v, tmp) {
    if (dict_find(*fats_in_data, v->key)) {
      HASH_DEL(*fats_in_code, v);
      free(v);
    }
  }
}

extern dict *vtabletaken;

static dict *get_all_vmtd(dict *vtable, dict *defined_ctors) {
  dict *all_vmtd = 0;
  keyvalue *kv, *tmp;
  HASH_ITER(hh, vtable, kv, tmp) {
    node *n;
    if (!dict_find(vtabletaken, kv->key) &&
        dict_find(defined_ctors, kv->key)) {
      //dprintf(STDERR_FILENO, "%s\n", kv->key);
      DL_FOREACH((node*)kv->value, n) {
        if (!dict_find(all_vmtd, n->val))
          dict_add(&all_vmtd, n->val, 0);
      }
    }
  }
  return all_vmtd;
}

static void compute_tc_vmtd(dict **vmtd,
                            dict *defined_ctors,
                            graph *aliases_tc) {
  code_module *m;

  DL_FOREACH(modules, m) {
    merge_dicts(vmtd, get_all_vmtd(m->vtable, defined_ctors));
  }

  keyvalue *v, *tmp;
  HASH_ITER(hh, aliases_tc, v, tmp) {
    if (dict_find(*vmtd, v->key)) {
      vertex *a, *atmp;
      HASH_ITER(hh, (graph*)(v->value), a, atmp) {
        dict_add(vmtd, a->key, 0);
      }
    }
  }
}

/* Version Space
 * We use four 7-bit fields to represent the version, excluding
 * 0xfe and 0xf4 for exception landingpads and dynamic code generation.
 * Therefore the version space is (2**7-2)**4 = 252047376. Although it
 * is large enough for any reasonable program, we should be careful about
 * attackers who are possible to exhaust it.
 */

const unsigned int VERSION_SPACE_MAX = 252047376;
static unsigned int version_space = 0;

/* generate the cfg */
int gen_cfg(void) {
#ifdef NOCFI
  /* don't generate the cfg at all */
  return 0;
#endif
  //dprintf(STDERR_FILENO, "[gen_cfg] called, %p\n", table);
  icf *icfs = 0;
  function *functions = 0;
  dict *classes = 0;
  graph *cha = 0;
  dict *fats = 0;
  dict *fats_in_data = 0;
  dict *defined_ctors = 0;
  graph *aliases = 0;
#ifdef COLLECT_STAT
  if (rt_eqc_ids) {
    dict_dtor(&rt_eqc_ids, 0, 0);
    rt_eqc_ids = 0;
  }
  if (ict_eqc_ids) {
    dict_dtor(&ict_eqc_ids, 0, 0);
    ict_eqc_ids = 0;
  }
#endif
  if (fats_in_code) {
    g_dtor(&fats_in_code);
    fats_in_code = 0;
  }

  if (vmtd) {
    g_dtor(&vmtd);
    vmtd = 0;
  }

  merge_mcfi_metainfo(modules, &icfs, &functions, &classes,
                      &cha, &fats, &fats_in_data, &fats_in_code, &aliases, &defined_ctors);

  graph *all_funcs_grouped_by_name = 0;
  graph *callgraph =
    build_callgraph(icfs, functions, classes, cha,
                    fats, aliases, &all_funcs_grouped_by_name);

  icfs_clear(&icfs);
  dict_clear(&classes);
  g_dtor(&cha);
  dict_clear(&fats);
  g_dtor(&aliases);

  graph *aliases_tc = g_transitive_closure(&aliases);
  /* build complete fats_in_data and fats_in_code */
  compute_fic(&fats_in_code, &fats_in_data, aliases_tc);
  g_dtor(&fats_in_data);
  /* handle vmtd aliases of virtual destructors */
  compute_tc_vmtd(&vmtd, defined_ctors, aliases_tc);
  dict_clear(&defined_ctors);
  g_free_transitive_closure(&aliases_tc);

  node *lcg = g_get_lcc(&callgraph);

#ifdef COLLECT_STAT
  unsigned int count;
  node *n;
  DL_COUNT(lcg, n, count);
  eqc_callgraph_count = count;
#endif

  /* based on the callgraph, let's build the return graph on top of it */
  build_retgraph(&callgraph, all_funcs_grouped_by_name, modules);

  g_dtor(&all_funcs_grouped_by_name);
  functions_clear(&functions);

  node *lrt = g_get_lcc(&callgraph);
  //l_print(lrt, print_cfgcc);
  g_dtor(&callgraph);

#ifdef COLLECT_STAT
  DL_COUNT(lrt, n, count);
  eqc_retgraph_count = count;
#endif

  unsigned long id_for_others;
  dict *callids = 0, *retids = 0;
  gen_mcfi_id(&lcg, &lrt, &version, &id_for_others, &callids, &retids);

  ++version_space;

  if (version_space < VERSION_SPACE_MAX) {
    /* We still have more versions to explore */
    if (safe()) /* if it is safe, then we reset the version_space counter */
      version_space = 0;
  } else {
    /* Wait until it is safe. It is good to have an exponential backoff
     * algorithm here */
    while (!safe())
      ;
    version_space = 0; /* reset the version_space counter */
  }

  /* The CFG generation and update strategy is the following:
   * 1. generate the new bary and tary tables for all modules.
   * 2. for each module whose cfggened == FALSE, populate their tary
   *    and bary tables.
   * 3. for each module whose cfggened == TRUE, populate their
   *    tary tables.
   * 4. for each module whose cfggened == TRUE, populate their bary
   *    tables.
   * 5. mark all modules' cfggened field to be one.
   */
  code_module *m = 0;

#ifdef COLLECT_STAT
  ibt_funcs = 0;
  ibt_radcs = 0;
  ibt_raics = 0;
  ict_count = 0;
  rt_count = 0;
#endif

  DL_FOREACH(modules, m) {
    if (!m->cfggened) {
      gen_tary(m, callids, retids, table, &fats_in_code, &vmtd);
      gen_bary(m, callids, retids, table, id_for_others);
#ifdef NO_ONLINE_PATCHING
      populate_landingpads(m, table);
#endif
    }
  }
  
  DL_FOREACH(modules, m) {
    if (m->cfggened)
      gen_tary(m, callids, retids, table, &fats_in_code, &vmtd);
  }

  /* write barrier, if needed */
  
  DL_FOREACH(modules, m) {
    if (m->cfggened)
      gen_bary(m, callids, retids, table, id_for_others);
    else
      m->cfggened = TRUE;
  }

  dict_clear(&callids);
  dict_clear(&retids);

  if (!cfggened) {
    cfggened = TRUE;
    keyvalue *kv, *tmp;
    HASH_ITER(hh, patch_compensate, kv, tmp) {
      size_t *tary_entry = (size_t*)kv->key;
      //dprintf(STDERR_FILENO, "%p\n", addr);
      *tary_entry |= 1;
    }
    dict_clear(&patch_compensate);
  }

  /* update the counters */
  update_thesc();
  return 0;
}

void take_addr_and_gen_cfg(unsigned long func_addr) {
  //dprintf(STDERR_FILENO, "[take_addr_and_gen_cfg] %x\n", func_addr);
  code_module *m;
  int found = FALSE;
  keyvalue *fnl, *fn, *tmp;
  DL_FOREACH(modules, m) {
    //dprintf(STDERR_FILENO, "%x, %x\n", m->base_addr, m->sz);
    if (func_addr >= m->base_addr && func_addr < m->base_addr + m->sz) {
      func_addr -= m->base_addr;
      fnl = dict_find(m->dynfuncs, (void*)func_addr);
      if (fnl) {
        found = TRUE;
        break;
      }
    }
  }
  if (!found) {
    dprintf(STDERR_FILENO, "[take_addr_and_gen_cfg] cannot find the functions\n");
    quit(-1);
  }
  /* add the functions' names to fats */
  HASH_ITER(hh, ((dict*)(fnl->value)), fn, tmp) {
    dict_add(&(m->fats), fn->key, 0);
  }
  /* generate the cfg */
  gen_cfg();
}

void set_gotplt(unsigned long addr, unsigned long v) {
  //dprintf(STDERR_FILENO, "[set_gotplt] (%x, %x)\n", addr, v);
  code_module *m, *am;
  int foundaddr = FALSE;
  int foundv = FALSE;
  unsigned long func_addr = v;
  keyvalue *fnl, *fn;
  int weak = FALSE;
  DL_FOREACH(modules, m) {
    //dprintf(STDERR_FILENO, "gotplt: %x, %x, %x\n", m->gotplt, m->gotpltsz, m->sz);
    if (addr >= m->gotplt && addr < m->gotplt + m->gotpltsz) {
      foundaddr = TRUE;
      am = m;
    }
    if (func_addr >= m->base_addr && func_addr < m->base_addr + m->sz) {
      func_addr -= m->base_addr;
      //dprintf(STDERR_FILENO, "%x\n", func_addr);
      fnl = dict_find(m->dynfuncs, (void*)func_addr);
      if (fnl) {
        foundv = TRUE;
        continue;
      }
      /* let's try weak symbols */
      fnl = dict_find(m->weakfuncs, (void*)func_addr);
      if (fnl) {
        foundv = TRUE;
      }
    }
  }
  if (!foundaddr) {
    dprintf(STDERR_FILENO, "[set_gotplt] illegal address\n");
    quit(-1);
  }
  if (!foundv) {
    dprintf(STDERR_FILENO, "[set_gotplt] illegal value\n");
    quit(-1);
  }

  keyvalue *gpf = dict_find(am->gpfuncs, (void*)(addr - am->gotplt));
  if (!gpf) {
    dprintf(STDERR_FILENO, "[set_gotplt] invalid addr\n");
    quit(-1);
  }
  if (!dict_find((dict*)(fnl->value), gpf->value)) {
    dprintf(STDERR_FILENO, "[set_gotplt] %s not found\n", gpf->value);
    quit(-1);
  }

  /* change the .got.plt entry atomically */
  unsigned long *p =
    (unsigned long*)(am->osb_gotplt + addr - am->gotplt);
  *p = v;
}

void unload_native_code(const char* code_file_name) {
  /* TODO: implement library unloading in four steps:
     1. Mark all TIDs for the library-to-be-deleted as inactive by
        clearing the least significant bit.
     2. Update the CFG for all other modules.
     3. Wait until all threads have executed at least one system call
        or runtime trampoline call. Note that on some other OSes such
        as windows that provide querying thread's context, it suffices
        to know no other thread is in execution of the library.

     // Step 2 and 3 ensure that no thread is executing code in the lib.

     4. Reclaim the library's memory for future use.
  */
}

void rock_clone(void) {
}

void rock_execve(void) {
}

void rock_shmat(void) {
}

void rock_shmdt(void) {
}

extern void *create_parallel_mapping(void *base,
                                     size_t size,
                                     int prot);

void *create_code_heap(void **ph, size_t size, struct verifier_t *verifier) {
  code_module* m = alloc_code_module();
  if (verifier == 0) {
    dprintf(STDERR_FILENO, "[rock_create_code_heap] illegal verifier\n");
    quit(-1);
  }
  //dprintf(STDERR_FILENO, "%p\n", verifier);
  if (!ph || (size_t)ph > FourGB) {
    dprintf(STDERR_FILENO,
            "[rock_create_code_heap] illegal pointer to shadow code heap\n");
    quit(-1);
  }
  uintptr_t base_addr = VmmapFindSpace(&VM, size >> PAGESHIFT);
  if (base_addr == 0) {
    dprintf(STDERR_FILENO, "[create_code_heap] VmmapFindSpace failed\n");
    quit(-1);
  }
  VmmapAdd(&VM, base_addr,
           size >> PAGESHIFT,
           PROT_READ | PROT_EXEC, PROT_READ | PROT_EXEC,
           VMMAP_ENTRY_ANONYMOUS);

  base_addr <<= PAGESHIFT;

  m->osb_base_addr = (uintptr_t)create_parallel_mapping((void*)base_addr,
                                                        size, PROT_NONE);
  m->base_addr = base_addr;
  m->sz = size;
  m->activated = TRUE;
  m->code_heap = TRUE;
  m->verifier = verifier;
  m->code_data_bitmap = mmap(NULL, RoundToPage(size/8), PROT_WRITE,
                             MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (m->code_data_bitmap == (void*)-1) {
    dprintf(STDERR_FILENO, "[rock_create_code_heap] code_data_bitmap allocation failed\n");
    quit(-1);
  }
  m->internal_dbt_bitmap = mmap(NULL, RoundToPage(size/8), PROT_WRITE,
                                MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (m->internal_dbt_bitmap == (void*)-1) {
    dprintf(STDERR_FILENO, "[rock_create_code_heap] internal_dbt_bitmap allocation failed\n");
    quit(-1);
  }

  *ph = m;

  DL_APPEND(modules, m);
  //dprintf(STDERR_FILENO, "[create_code_heap] %p, %p, 0x%lx\n",
  //        (void*)m->base_addr, (void*)m->osb_base_addr, size);
  return (void*)m->base_addr;
}

#define ROCK_FUNC_SYM       0
#define ROCK_ICJ            1
#define ROCK_ICJ_SYM        2
#define ROCK_RAI            3
#define ROCK_ICJ_UNREG      4
#define ROCK_ICJ_SYM_UNREG  5
#define ROCK_RAI_UNREG      6
#define ROCK_FUNC_SYM_UNREG 7
#define ROCK_RET            8

static char *query_function_name(uintptr_t addr) {
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    if (addr >= m->base_addr && addr < m->base_addr + m->sz) {
      found = TRUE;
      break;
    }
  }
  if (found) {
    symbol *s;
    addr -= m->base_addr;
    DL_FOREACH(m->funcsyms, s) {
      if (s->offset == addr)
        return s->name;
    }
  }
  return 0;
}

static void* query_rad(const char* name) {
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    symbol *r;
    DL_FOREACH(m->rad, r) {
      if (name == r->name) {
        found = TRUE;
        return (void*)(r->offset + m->base_addr);
      }
    }
  }
  dprintf(STDERR_FILENO, "%s\n", name);
  assert(found);
  return 0;
}

static function *query_function(const char* name) {
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    function *f;
    DL_FOREACH(m->functions, f) {
      if (name == f->name) {
        found = TRUE;
        return f;
      }
    }
  }
  assert(found);
  return 0;
}

static symbol *query_icfsym(const char *name) {
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    symbol *s;
    DL_FOREACH(m->icfsyms, s) {
      if (name == s->name) {
        found = TRUE;
        return s;
      }
    }
  }
  assert(found);
  return 0;
}

static code_module *get_code_heap(void *h) {
  static code_module* prev_code_module = 0;
  code_module* m = (code_module*)h;
  if (m == prev_code_module)
    return m;
  else {
    DL_FOREACH(modules, m) {
      if (m == (code_module*)h && m->code_heap)
        return m;
    }
  }
  dprintf(STDERR_FILENO, "[get_code_heap] invalid code module used, %p\n", h);
  quit(-1);
  return 0;
}

static dict *icj_target = 0;
static dict *icj_target_ret = 0;
static dict *ret_name = 0;
static dict *ret_offset = 0;

void reg_cfg_metadata(void *h,    /* code heap handle */
                      int type,   /* type of the metadata */
                      void *md,   /* metadata, whose semantics depends on the type */
                      void *extra /* extra info, optional */
                      ) {
  code_module *m = get_code_heap(h);

  switch(type) {
  case ROCK_FUNC_SYM:
    {
      uintptr_t new_addr = (uintptr_t)md;
      uintptr_t addr = (uintptr_t)extra;
      if (new_addr < m->base_addr || new_addr >= m->base_addr + m->sz ||
          new_addr % 8 != 0) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata ROCK_FUNC_SYM ] illegal new function sym %lx registered\n",
                new_addr);
        quit(-1);
      }
      if (addr >= FourGB || addr % 8 != 0) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata ROCK_FUNC_SYM ] illegal function sym %lx registered\n",
                addr);
        quit(-1);
      }
      char *name = query_function_name(addr);
      assert(name);
      unsigned long *p = (unsigned long*)(table + new_addr);
      unsigned long *q = (unsigned long*)(table + addr);
      *p = *q;
      //dprintf(STDERR_FILENO,
      //        "[rock_reg_cfg_metadata ROCK_FUNC_SYM] %x, %x, %lx\n", new_addr, addr, *q);
      symbol *funcsym = alloc_sym();
      funcsym->name = name;
      funcsym->offset = new_addr - m->base_addr;
      DL_APPEND(m->funcsyms, funcsym);
    }
    break;
  case ROCK_RET:
    {
      //dprintf(STDERR_FILENO, "[rock_reg_cfg_metadata ROCK_RET] %p, %p\n", md, extra);
      char *name;
      unsigned int bid_slot;
      uintptr_t addr;
      keyvalue *r = dict_find(ret_name, extra);
      if (!r) {
        name = query_function_name((uintptr_t)extra);
        assert(name);
        function *func = query_function(name);
        assert(func->returns);
        name = func->returns->val;
        dict_add(&ret_name, extra, func->returns->val);

        // Let's reuse a bid_slot
        symbol *sym = query_icfsym(name);
        bid_slot = sym->offset;
        dict_add(&ret_offset, extra, (void*)(size_t)bid_slot);
      } else {
        name = r->value;
        r = dict_find(ret_offset, extra);
        assert(r);
        bid_slot = (unsigned int)r->value;
      }

      addr = (uintptr_t)md;
      if (addr < m->base_addr || addr >= m->base_addr + m->sz) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata ROCK_RET] illegal ret bary offset %lx registered\n",
                addr);
        quit(-1);
      }

      symbol *icfsym = alloc_sym();
      icfsym->name = name;
      icfsym->offset = bid_slot;

      *(unsigned int*)(m->osb_base_addr + (addr - m->base_addr - 4)) = bid_slot;

      DL_APPEND(m->icfsyms, icfsym);
    }
    break;
  case ROCK_ICJ:
    {
      char *id = sp_intern_string(&stringpool, md);
      icf *ic;
      HASH_FIND_PTR(m->icfs, &id, ic);
      if (!ic) {
        //dprintf(STDERR_FILENO, "[rock_reg_cfg_metadata] icf %s, %x\n", md, extra);
        ic = alloc_icf();
        ic->id = sp_intern_string(&stringpool, md);
        ic->ity = NormalCall;
        char *name = query_function_name((uintptr_t)extra);
        assert(name);
        function *f = query_function(name);
        ic->type = f->type;
        HASH_ADD_PTR(m->icfs, id, ic);
        dict_add(&icj_target, id, extra);
        void *ra = query_rad(name);
        dict_add(&icj_target_ret, id, ra);
      }
    }
    break;
  case ROCK_ICJ_SYM:
    {
      uintptr_t addr = (uintptr_t)extra;
      if (addr < m->base_addr || addr >= m->base_addr + m->sz) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata] illegal icj sym %lx registered\n",
                addr);
        quit(-1);
      }
      symbol *icfsym = alloc_sym();
      icfsym->name = sp_intern_string(&stringpool, md);
      keyvalue *cached_bid_slot = dict_find(m->bid_slot_in_codeheap, icfsym->name);
      unsigned int bid_slot;
      if (!cached_bid_slot) {
        bid_slot = alloc_bid_slot();
        cached_bid_slot = dict_add(&(m->bid_slot_in_codeheap),
                                   icfsym->name, (void*)(unsigned long)bid_slot);
        keyvalue *icj = dict_find(icj_target, icfsym->name);
        assert(icj);
        unsigned long *p = (unsigned long*)(table + bid_slot);
        unsigned long *q = (unsigned long*)(table + (uintptr_t)icj->value);
        *p = *q;
      } else {
        bid_slot = (unsigned int)cached_bid_slot->value;
      }
      *(unsigned int*)(m->osb_base_addr + (addr - m->base_addr - 4)) = bid_slot;
      icfsym->offset = bid_slot;
      DL_APPEND(m->icfsyms, icfsym);
      //dprintf(STDERR_FILENO,
      //        "[rock_reg_cfg_metadata] icfsym %s, %x, %p\n",
      //        icfsym->name, icfsym->offset, extra);
    }
    break;
  case ROCK_RAI:
    {
      symbol *rai = alloc_sym();
      rai->name = sp_intern_string(&stringpool, md);
      size_t addr = (size_t)extra;
      if (addr < m->base_addr || addr >= m->base_addr + m->sz ||
          addr % 8 != 0) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata] illegal rai %lx registered\n",
                addr);
        quit(-1);
      }
      rai->offset = addr - m->base_addr;
      DL_APPEND(m->rai, rai);
      //dprintf(STDERR_FILENO, "[rock_reg_cfg_metadata] rai %s, %x, %p\n",
      //        rai->name, rai->offset, extra);
      keyvalue *ra = dict_find(icj_target_ret, rai->name);
      assert(ra);
      unsigned long *p = (unsigned long*)(table + addr);
      unsigned long *q = (unsigned long*)(table + (uintptr_t)ra->value);
#ifndef NOCFI
      *q |= 1;
#endif
      *p = *q;
    }
    break;
  case ROCK_ICJ_SYM_UNREG:
    {
      char *name = sp_intern_string(&stringpool, md);
      symbol *s, *tmp;
      DL_FOREACH_SAFE(m->icfsyms, s, tmp) {
        if (name == s->name) {
          DL_DELETE(m->icfsyms, s);
          dprintf(STDERR_FILENO,
                  "[rock_reg_cfg_metadata] icj sym %s, %x unregistered\n",
                  s->name, s->offset);
          free(s);
        }
      }
    }
    break;
  case ROCK_FUNC_SYM_UNREG:
    {
      uintptr_t addr = (uintptr_t)md;
      if (addr < m->base_addr || addr >= m->base_addr + m->sz ||
          addr % 8 != 0) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata ROCK_FUNC_SYM_UNREG] illegal func %lx unreg\n",
                addr);
        quit(-1);
      }
      unsigned long *p = (unsigned long*)(table + addr);
      *p = 0; // invalidate this rai target
      addr -= m->base_addr;
      symbol *s, *tmp;
      DL_FOREACH_SAFE(m->funcsyms, s, tmp) {
        if (s->offset == addr) {
          DL_DELETE(m->funcsyms, s);
          dprintf(STDERR_FILENO,
                  "[rock_reg_cfg_metadata] func sym %s, %x unregistered\n",
                  s->name, s->offset);
          free(s);
        }
      }
    }
  case ROCK_RAI_UNREG:
    {
      uintptr_t addr = (uintptr_t)md;
      if (addr < m->base_addr || addr >= m->base_addr + m->sz ||
          addr % 8 != 0) {
        dprintf(STDERR_FILENO,
                "[rock_reg_cfg_metadata] illegal rai %lx unreg\n",
                addr);
        quit(-1);
      }
      unsigned long *p = (unsigned long*)(table + addr);
      *p = 0; // invalidate this rai target
      addr -= m->base_addr;
      symbol *s, *tmp;
      DL_FOREACH_SAFE(m->rai, s, tmp) {
        if (s->offset == addr) {
          DL_DELETE(m->rai, s);
          dprintf(STDERR_FILENO,
                  "[rock_reg_cfg_metadata] rai %s, %x unregistered\n",
                  s->name, s->offset);
          free(s);
        }
      }
    }
    break;
  default:
    dprintf(STDERR_FILENO, "[rock_reg_cfg_metadata] unrecognized type %d\n", type);
    quit(-1);
    break;
  }
  //dprintf(STDERR_FILENO, "[reg_cfg_metadata] exited\n");
}

static void wait(void) {
  static unsigned long thread_escapes[256]; // at most 128 threads can be handled
  TCB *tcb = tcb_list;
  int count = 0;
  /* record the thread escapes for all threads that are not trapped in
     system calls */
  while (tcb && count < 256) {
    if (!tcb->remove) {
      if (!tcb->insyscall) {
        thread_escapes[count++] = (unsigned long)tcb;
        thread_escapes[count++] = tcb->sandbox_escape;
      }
    }
    tcb = tcb->next;
  }
  if (count >= 256) {
    dprintf(STDERR_FILENO, "[wait] too many threads\n");
    quit(-1);
  }
  /* check to see if all threads have executed at least one system call */
  int safe = FALSE;
  while (!safe) {
    safe = TRUE;
    int i;
    for (i = 0; i < count; i += 2) {
      tcb = (TCB*)thread_escapes[i];
      if (tcb) {
        if (tcb->insyscall)
          thread_escapes[i] = 0;  // clear this thread
        else if (tcb->sandbox_escape == thread_escapes[i+1]) {
          safe = FALSE;
          break;
        }
      }
    }
  }
}

void delete_code(void *h, /* handle */
                 uintptr_t addr,
                 size_t length) {
  code_module *m = get_code_heap(h);
  if (length >= FourGB || addr < m->base_addr || addr + length > m->base_addr + m->sz) {
    dprintf(STDERR_FILENO, "[rock_delete_code] illegal %x, %x\n", addr, length);
    quit(-1);
  }
  //dprintf(STDERR_FILENO, "[rock_delete_code] %x, %x\n", addr, length);
  // the to-be deleted code region should not be reachable from direct control-transfer
  if ((addr > m->base_addr &&
       ROCK_CODE == which_area(m->code_data_bitmap, addr - m->base_addr - 1, 2)) ||
      (addr + length < m->base_addr + m->sz &&
       ROCK_CODE == which_area(m->code_data_bitmap, addr + length - 1, 2))) {
    dprintf(STDERR_FILENO,
            "[rock_delete_code] to-be deleted code is reachable "
            "from sequential control-flow transfer\n");
    quit(-1);
  }
  // check whether this region is referenced by other regions
  set_data(m->code_data_bitmap, addr - m->base_addr, length);
  addr = addr & (-8);
  length = ((length + 7) & (-8));

  //dprintf(STDERR_FILENO, "[rock_delete_code] %x, %x\n", addr, length);
  unsigned long *p = (unsigned long*)(table+addr);
  length /= 8;
  unsigned i;
  for (i = 0; i < length; i++)
    p[i] = 0;
}

void move_code(void *h,
               uintptr_t target,
               uintptr_t source,
               size_t length) {
  code_module *m = get_code_heap(h);

  if (length >= FourGB ||
      target < m->base_addr || target + length > m->base_addr + m->sz ||
      (target & 7)) {
    dprintf(STDERR_FILENO, "[rock_move_code] illegal target %x\n", target);
    quit(-1);
  }
  if (source < m->base_addr || source + length > m->base_addr + m->sz) {
    dprintf(STDERR_FILENO, "[rock_move_code] illegal source %x\n", source);
    quit(-1);
  }
  //dprintf(STDERR_FILENO, "[rock_move_code] %x, %x, %u\n", target, source, length);
  if (ROCK_CODE != which_area(m->code_data_bitmap, source - m->base_addr, length)) {
    dprintf(STDERR_FILENO, "[rock_move_code not code]\n");
    quit(-1);
  }
  if ((source > m->base_addr &&
      ROCK_CODE == which_area(m->code_data_bitmap, source - m->base_addr - 1, 1)) ||
      (source + length < m->base_addr + m->sz &&
       ROCK_CODE == which_area(m->code_data_bitmap, source + length, 1))) {
    dprintf(STDERR_FILENO, "[rock_move_code not moving all code]\n");
    quit(-1);
  }
  if (ROCK_DATA != which_area(m->code_data_bitmap, target - m->base_addr, length)) {
    dprintf(STDERR_FILENO, "[rock_move_code not data]\n");
    quit(-1);
  }
  set_data(m->code_data_bitmap, source - m->base_addr, length);
  set_code(m->code_data_bitmap, target - m->base_addr, length);

  target = target & (-8);
  source = source & (-8);
  length = ((length + 7)& (-8));

  //dprintf(STDERR_FILENO, "[rock_move_code] %x, %x, %x\n", target, source, length);
  unsigned long *p = (unsigned long*)(table+target);
  unsigned long *q = (unsigned long*)(table+source);
  length /= 8;
  unsigned i;
  for (i = 0; i < length; i++) {
    unsigned long tmp = q[i];
    if (tmp) {
      q[i] = 0;
      p[i] = tmp;
      //dprintf(STDERR_FILENO, "[rock_move_code] %x, %x, %lx\n",
      //        (uintptr_t)target-(uintptr_t)table,
      //        (uintptr_t)source-(uintptr_t)table,
      //        p[i]);
    }
  }
}

/* fork of rock, pretty tricky, now we do not support fork in a multi-threading
   case, which should be better supported by the OS kernel.
 */
static void restore_parallel_mapping(void *base, void *osb_base,
                                     size_t size, int prot) {
  const char *shmname = "/dev/shm/mcfi";
  int fd = -1;
  while (TRUE) {
    fd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, 0744);
    if (fd >= 0)
      break;
    if (fd == -1 && errn == EEXIST)
      continue;
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] shm_open failed with %d\n", errn);
    quit(-1);
  }

  if (0 != ftruncate(fd, size)) {
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] ftruncate failed with %d\n", errn);
    quit(-1);
  }

  assert(size % PAGE_SIZE == 0);
  void *new_base = mmap(base, size, prot, MAP_SHARED | MAP_FIXED,
                        fd, 0);
  if (base != new_base) {
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] mmap base failed with %d\n", errn);
    quit(-1);
  }

  void *new_osb_base = mmap(osb_base, size, PROT_WRITE, MAP_SHARED | MAP_FIXED,
                            fd, 0);
  if (osb_base != new_osb_base) {
    dprintf(STDERR_FILENO,
            "[create_parallel_mapping] mmap osb_base failed with %d\n", errn);
    quit(-1);
  }

  close(fd);
  shm_unlink(shmname);
}

static void save_content(void) {
  code_module *m;
  DL_FOREACH(modules, m) {
    /* copy the contents of the code and .got.plt */
    //dprintf(STDERR_FILENO, "%x, %lx, %x\n", m->base_addr, m->osb_base_addr, m->sz);
    m->code = mmap(0, m->sz, PROT_WRITE | PROT_READ,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (m->code == (void*)-1) {
      dprintf(STDERR_FILENO, "[rock_fork] code allocation failed\n");
      quit(-1);
    }
    memcpy(m->code, (void*)m->base_addr, m->sz);
    if (m->gotpltsz > 0) {
      m->gotpltcontent = mmap(0, m->gotpltsz, PROT_WRITE | PROT_READ,
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
      if (m->gotpltcontent == (void*)-1) {
        dprintf(STDERR_FILENO, "[rock_fork] gotplt allocation failed\n");
        quit(-1);
      }
      memcpy(m->gotpltcontent, (void*)m->gotplt, m->gotpltsz);
    }
  }

  /* note that we should finish all allocation and then start unmapping
   * the pages, otherwise, strange behaviors appear. */
  DL_FOREACH(modules, m) {
    munmap((void*)m->base_addr, m->sz);
    munmap((void*)m->osb_base_addr, m->sz);
    if (m->gotpltsz > 0) {
      munmap((void*)m->gotplt, m->gotpltsz);
      munmap((void*)m->osb_gotplt, m->gotpltsz);
    }
  }
}

static void restore_content(void) {
  code_module *m;
  DL_FOREACH(modules, m) {
    restore_parallel_mapping((void*)m->base_addr, (void*)m->osb_base_addr, m->sz, PROT_EXEC);
    memcpy((void*)m->osb_base_addr, m->code, m->sz);
    munmap(m->code, m->sz);
    m->code = 0;
    if (m->gotpltsz > 0) {
      restore_parallel_mapping((void*)m->gotplt, (void*)m->osb_gotplt, m->gotpltsz, PROT_READ);
      memcpy((void*)m->osb_gotplt, m->gotpltcontent, m->gotpltsz);
      munmap(m->gotpltcontent, m->gotpltsz);
      m->gotpltcontent = 0;
    }
  }
}

int rock_fork(void) {
  //dprintf(STDERR_FILENO, "[rock_fork]\n");
#ifndef NO_ONLINE_PATCHING
  save_content();
#endif
  int rv = __syscall0(SYS_fork);
#ifndef NO_ONLINE_PATCHING
  restore_content();
#endif
  return rv;
}

#ifdef COLLECT_STAT
struct ibe_t {
  unsigned long ibe_count;
  unsigned long ibe_count_wo_activation; // without considering online activation
};

static int sort_by_value(keyvalue *kv1, keyvalue *kv2) {
  unsigned long v1 = (unsigned long)kv1->value;
  unsigned long v2 = (unsigned long)kv2->value;
  if (v1 == v2)
    return 0;
  return (v1 < v2) ? 1 : -1; /* descending order */
}

/* count indirect branch edges */
static struct ibe_t ibe_count(void) {
  struct ibe_t IBEs;
  IBEs.ibe_count = 0;
  IBEs.ibe_count_wo_activation = 0;

  code_module *m;
  dict *ib = 0;
  dict *ibt = 0;
  dict *ibt_wo_activation = 0;
  //graph *ibt_wo_ac = 0;
  unsigned int i;

  DL_FOREACH(modules, m) {
    //dprintf(STDERR_FILENO, "Module: %x, %x\n", m->base_addr, m->sz);
    unsigned long* p = (unsigned long*)(table + m->base_addr);
    size_t sz = m->sz / 8;
    for (i = 0; i < sz; i++) {
      if (p[i] & 1) {
        incr_dict_val(&ibt, (void*)p[i]);
        //dprintf(STDERR_FILENO, "ibta: %lx, %lx\n",
        //        (unsigned long)p + 8*i - (unsigned long)table,
        //        p[i]);
      }
    }
  }

  HASH_SRT(hh, ibt, sort_by_value);
  HASH_SRT(hh, ict_eqc_ids, sort_by_value);
  HASH_SRT(hh, rt_eqc_ids, sort_by_value);

  {
    dprintf(STDERR_FILENO, "=====================================\n");
    dprintf(STDERR_FILENO, "How many addresses are included in each eqc in descending order?\n");
    keyvalue *kv, *tmp;
#ifndef NO_ONLINE_PATCHING
    dprintf(STDERR_FILENO, "\nPICFI functions\n");
    HASH_ITER(hh, ibt, kv, tmp) {
      if (dict_find(ict_eqc_ids, kv->key))
        dprintf(STDERR_FILENO, "%lu ", kv->value);
    }

    dprintf(STDERR_FILENO, "\n\nPICFI return addresses\n");
    HASH_ITER(hh, ibt, kv, tmp) {
      if (dict_find(rt_eqc_ids, kv->key))
        dprintf(STDERR_FILENO, "%lu ", kv->value);
    }
    dprintf(STDERR_FILENO, "\n");
#endif
    dprintf(STDERR_FILENO, "\nMCFI functions count\n");
    HASH_ITER(hh, ict_eqc_ids, kv, tmp) {
      dprintf(STDERR_FILENO, "%lu ", kv->value);
    }

    dprintf(STDERR_FILENO, "\n\nMCFI return addresses\n");
    HASH_ITER(hh, rt_eqc_ids, kv, tmp) {
      dprintf(STDERR_FILENO, "%lu ", kv->value);
    }
    dprintf(STDERR_FILENO, "\n");
    dprintf(STDERR_FILENO, "=====================================\n");
  }
  /*
  {
    vertex *v, *tmp;
    HASH_ITER(hh, ibt_wo_ac, v, tmp) {
      vertex *all, *atmp;
      dprintf(STDERR_FILENO, "\nEQC:+++++++++++++++++++++++++++\n");
      HASH_ITER(hh, (dict*)(v->value), all, atmp) {
        dprintf(STDERR_FILENO, "%x\n", all->key);
      }
    }
  }
  */
  unsigned id = (alloc_bid_slot() - BID_SLOT_START)/ 8;
  unsigned long *p = (unsigned long*)(table + BID_SLOT_START);
  for (i = 0; i < id; i++) {
    if (p[i] & 1) {
      keyvalue *kv = dict_find(ib, (void*)p[i]);
      if (!kv) {
        kv = dict_add(&ib, (void*)p[i], (void*)0);
      }
      unsigned long count = (unsigned long)kv->value;
      count++;
      kv->value = (void*)count;
    }
  }
  keyvalue *ikv, *tkv, *tmp;
  HASH_ITER(hh, ib, ikv, tmp) {
    tkv = dict_find(ibt, ikv->key);
    if (tkv) {
      IBEs.ibe_count += (unsigned long)ikv->value * (unsigned long)tkv->value;
      dict_del(&ibt, tkv->key);
    }
    tkv = dict_find(ict_eqc_ids, ikv->key);
    if (tkv) {
      IBEs.ibe_count_wo_activation += (unsigned long)ikv->value * (unsigned long)tkv->value;
    }
    tkv = dict_find(rt_eqc_ids, ikv->key);
    if (tkv) {
      IBEs.ibe_count_wo_activation += (unsigned long)ikv->value * (unsigned long)tkv->value;
    }
  }
  /* TODO: some IBTs do not have corresponding IBs. All cases seem to
     be return addresses whose preceding indirect call do not have targets.
  HASH_ITER(hh, ibt, tkv, tmp) {
    dprintf(STDERR_FILENO, "%lx, %lx\n", tkv->key, tkv->value);
  }
  */
  return IBEs;
}
#endif

void collect_stat(void) {
#ifdef PROFILING
  {
    struct timeval tv;
    gettimeofday(&tv);
    dprintf(STDERR_FILENO, "[%lu:%lu] Ended!\n", tv.tv_sec, tv.tv_usec);
  }
#endif
#ifdef COLLECT_STAT

  unsigned int lp_count = 0;
  unsigned int eqclp = 0;
  code_module *m;
  symbol *s;
  DL_FOREACH(modules, m) {
    unsigned int lpn = 0;
    DL_COUNT(m->lp, s, lpn);
    lp_count += lpn;
  }
  if (lp_count > 0)
    ++eqclp;

  struct ibe_t ibe = ibe_count();
  ibe.ibe_count += lp_activation_count;
  ibe.ibe_count_wo_activation += lp_count;

  unsigned int pid = __syscall0(SYS_getpid);

  /*
   * TODO: the following equivalence class numbers don't match, figure out later.
   */
  dprintf(STDERR_FILENO, "\n[%u] MCFI Statistics\n", pid);
  /*
  dprintf(STDERR_FILENO, "[%u] Total Equivalence Classes for instructions: %u\n", pid,
          eqc_callgraph_count + eqc_retgraph_count + eqclp);
  dprintf(STDERR_FILENO, "[%u] Equivalence Classes for indirect calls: %u\n", pid,
          eqc_callgraph_count);
  dprintf(STDERR_FILENO, "[%u] Equivalence Classes for returns: %u\n", pid,
          eqc_retgraph_count + eqclp);
  */
  dprintf(STDERR_FILENO, "[%u] Equivalence classes for indirect branch targets: %u\n", pid,
          HASH_COUNT(ict_eqc_ids) + HASH_COUNT(rt_eqc_ids) + eqclp);
  dprintf(STDERR_FILENO, "[%u] Equivalence classes for functions reachable by indirect "
          "branches: %u\n", pid, HASH_COUNT(ict_eqc_ids));
  dprintf(STDERR_FILENO, "[%u] Equivalence classes for return addresses: %u\n", pid,
          HASH_COUNT(rt_eqc_ids));
  dprintf(STDERR_FILENO, "[%u] Equivalence classes for landing pads: %u\n", pid, eqclp);
  dprintf(STDERR_FILENO, "[%u] Total Indirect Branches: %u\n", pid,
          ict_count + rt_count + eqclp);
  dprintf(STDERR_FILENO, "[%u] Indirect calls/jmps: %u\n", pid, ict_count);
  dprintf(STDERR_FILENO, "[%u] Returns: %u\n", pid, rt_count);
  dprintf(STDERR_FILENO, "[%u] Landing pad jmp: %u\n", pid, eqclp);

  dprintf(STDERR_FILENO, "[%u] Total Indirect Branch Targets: %u\n", pid,
          ibt_funcs + ibt_radcs + ibt_raics + lp_count);
  dprintf(STDERR_FILENO, "[%u] Functions Reachable by Indirect Branches: %u\n",
          pid, ibt_funcs);
  dprintf(STDERR_FILENO, "[%u] Return Addresses of Direct Calls: %u\n",
          pid, ibt_radcs);
  dprintf(STDERR_FILENO, "[%u] Return Addresses of Indirect Calls: %u\n",
          pid, ibt_raics);
  dprintf(STDERR_FILENO, "[%u] Landing Pads: %u\n", pid, lp_count);

#ifndef NO_ONLINE_PATCHING
  dprintf(STDERR_FILENO, "[%u] C-Style Functions AddrTaken In Code: %u\n",
          pid, HASH_COUNT(ibt_funcs_taken_in_code));
  dprintf(STDERR_FILENO, "[%u] C-Style Functions AddrTaken In Data: %u\n",
          pid, ibt_funcs - HASH_COUNT(ibt_funcs_taken_in_code));
  dprintf(STDERR_FILENO, "[%u] Total online activated C-style functions: %u\n",
          pid, func_addr_activation_count);
  dprintf(STDERR_FILENO, "[%u] Virtual Methods AddrTaken In Code: %u\n",
          pid, HASH_COUNT(vmtd_taken_in_code));
  dprintf(STDERR_FILENO, "[%u] Total online activated virtual methods: %u\n",
          pid, vmtd_activation_count);
  dprintf(STDERR_FILENO, "[%u] Total online activated landing pads: %u\n",
          pid, lp_activation_count);
  dprintf(STDERR_FILENO, "[%u] Total online activated return addrs: %u\n",
          pid, radc_patch_count + raic_patch_count);
  dprintf(STDERR_FILENO, "[%u] Activated Return Addrs of Direct Calls: %u\n",
          pid, radc_patch_count);
  dprintf(STDERR_FILENO, "[%u] Activated Return Addrs of Indirect Calls: %u\n",
          pid, raic_patch_count);
  dprintf(STDERR_FILENO, "[%u] Amount of Indirect Branch Edges Considering Activation: %lu\n",
          pid, ibe.ibe_count);
  dprintf(STDERR_FILENO, "[%u] Total function address taken site patches: %lu\n",
          pid, at_patch_count);
  dprintf(STDERR_FILENO, "[%u] Total function entry patches: %lu\n",
          pid, func_entry_patch_count);
  dprintf(STDERR_FILENO, "[%u] Total call site patches: %lu\n",
          pid, radc_patch_count + raic_patch_count);
#endif
  dprintf(STDERR_FILENO, "[%u] Amount of Indirect Branch Edges: %lu\n",
          pid, ibe.ibe_count_wo_activation);
  icj_count += thread_self()->icj_count;
  if (icj_count > 0) {
    /* the printf code does not support %lu well, so we use %lx instead */
    dprintf(STDERR_FILENO, "[%u] Count of Indirect Branches: 0x%lx\n",
            pid, icj_count);
  }
  dprintf(STDERR_FILENO, "\n");
#endif
}

static int data(unsigned long flags) {
  return 0 == (flags & ROCK_CODE);
}

static int code(unsigned long flags) {
  return (flags & ROCK_CODE);
}

/* verify the jitted code */
static int verify(const struct verifier_t *v,
                  unsigned char* cur, const unsigned char *end,
                  uint16_t start, uint16_t *end_state, unsigned char** endptr) {
  uint16_t state = start;
  while (cur < end) {
    //dprintf(STDERR_FILENO, "%u\n", state);
    state = dfa_lookup(v, state, *cur++);
    if (state >= v->start)
      continue;
    else if (accepts(v, state)) {
      *end_state = state;
      *endptr = cur;
      if (cur + 1 < end) {
        /* look one state ahead to postpone the recursive call*/
        uint16_t next_state = dfa_lookup(v, state, *cur++);
        if (next_state != 0)
          verify(v, cur, end, next_state, end_state, endptr);
      }
      return 0;
    } else if (accepts_others(v, state)) {
      *end_state = state;
      *endptr = cur;
      return 0;
    } else if (0 == state) {
      return -1;
    }
  }
  return 1;
}

static int verify_jitted_code(code_module *m, unsigned char *data, size_t size, char *tary,
                              long install_addr, int check_terminate) {
  int result = 0;
  uint8_t *ptr = data;
  uint8_t *end = data + size;
  uint8_t *endptr = 0;
  uint16_t state;
  verifier *v = m->verifier;
  unsigned jmp_count = 0;
  /* for each direct call/jump instruction, we need a 4-byte slot to record
     its target address. For a code piece of size, there are (size + 1) / 2
     direct call/jmp instructions at most, since a direct call/jmp instruction
     is at least 2 bytes.
  */
  unsigned *jmp_targets = malloc(2 * (size + 1));
  if (!jmp_targets) {
    dprintf(STDERR_FILENO, "[verify_jitted_code] jmp_targets allocation failed\n");
    quit(-1);
  }

  while (ptr < end) {
    result = verify(v, ptr, end, m->verifier->start, &state, &endptr);
    /*
    uint8_t *i;
    for (i = ptr; i < endptr; i++)
      dprintf(STDERR_FILENO, "0x%02x ", *i);
    dprintf(STDERR_FILENO, "\n");
    */
    if (result != 0) {
      for (; ptr < end; ptr++) {
        dprintf(STDERR_FILENO, "0x%02x ", *ptr);
      }
      //dprintf(STDERR_FILENO, "Error: %lx\n", ptr - data);
      quit(-1);
    }
    //assert(state != 0);
    tary[ptr - data] = DCV;
    if (state > v->max_accept) {
      if (accepts_jmp_rel1(v, state) || accepts_jcc_rel1(v, state)) {
        char offset = *(char*)(endptr - 1);
        long target = install_addr + endptr - data + (long)offset;
        assert((uintptr_t)target >= m->base_addr &&
               (uintptr_t)target <  m->base_addr + m->sz);
        //dprintf(STDERR_FILENO, "j1, %lx, %lx, %d\n", ptr, target, offset);
        // target would be in the sandbox, so the following cast is secure
        jmp_targets[jmp_count++] = (unsigned)target;
      } else if (accepts_jmp_rel4(v, state) || accepts_jcc_rel4(v, state)) {
        int offset = *(int*)(endptr - 4);
        long target = install_addr + endptr - data + (long)offset;
        assert((uintptr_t)target >= m->base_addr &&
               (uintptr_t)target <  m->base_addr + m->sz);
        //dprintf(STDERR_FILENO, "j4, %lx, %lx, %d\n", ptr, target, offset);
        // target would be in the sandbox, so the following cast is secure
        jmp_targets[jmp_count++] = (unsigned)target;
      } else if (accepts_dcall(v, state)) {
        int offset = *(int*)(endptr - 4);
        long target = install_addr + endptr - data + (long)offset;
        assert((uintptr_t)target >= m->base_addr &&
               (uintptr_t)target <  m->base_addr + m->sz);
        //dprintf(STDERR_FILENO, "c4, %lx, %lx, %d\n", ptr, target, offset);
        // target would be in the sandbox, so the following cast is secure
        jmp_targets[jmp_count++] = (unsigned)target;
      } else if (accepts_mcficall(v, state)) {
        if (((uintptr_t)endptr & 0x7) != 0) {
          dprintf(STDERR_FILENO, "[verify] mcficall at %p not 8-byte aligned\n", ptr);
          quit(-1);
        }
      }
    }
    ptr = endptr;
  }
  // if we are installing new code, we should make sure the code ends with a terminator
  if (check_terminate && !terminator(v, state)) {
    dprintf(STDERR_FILENO,
            "[verify jitted code] the code does not end with a safe instruction, %p\n", data);
    quit(-1);
  }
  unsigned i;
  for (i = 0; i < jmp_count; i++) {
    if (jmp_targets[i] < m->base_addr || jmp_targets[i] >= m->base_addr + m->sz) {
      dprintf(STDERR_FILENO, "jmp_targets out-of-sandbox %x\n", jmp_targets[i]);
      quit(-1);
    } else if (jmp_targets[i] < (unsigned)install_addr ||
        jmp_targets[i] >= (unsigned)install_addr + size) {
      if (*(char*)(table + jmp_targets[i]) != (char)DCV) {
        dprintf(STDERR_FILENO, "jmp_targets wrong %x\n", jmp_targets[i]);
        quit(-1);
      }
    } else {
      if (tary[jmp_targets[i] - (unsigned)install_addr] != (char)DCV) {
        dprintf(STDERR_FILENO, "jmp_targets wrong %x\n", jmp_targets[i]);
        quit(-1);
      }
    }
  }
  free(jmp_targets);
  return result;
}

/* test whether the old code and the new code patch have the same internal
   pseudo-inst boundary */
static int same_internal_boundary(char* old_tary, char* tary,
                                  char *safe_old_code, char* safe_code, size_t len) {
  size_t i;
  int result = TRUE;
  for (i = 0; i < len; i++) {
    if (old_tary[i] != tary[i]) {
      result = FALSE;
    }
    if ((int)old_tary[i] == DCV) {
      safe_old_code[i] = DCV;
    }
    if ((int)tary[i] == DCV) {
      safe_code[i] = DCV;
    }
  }
  return result;
}

static void cpuid(void) {
  __asm__ __volatile__("cpuid":::"rax", "rbx", "rcx", "rdx");
}

/* use [src, len) to fill [dst, len) */
void code_heap_fill(void *h, /* code heap handle */
                    void *dst,
                    void *src,
                    size_t len,
                    void *extra) {
  code_module* m = get_code_heap(h);
  unsigned long flags = (unsigned long)extra;
  //dprintf(STDERR_FILENO, "[code_heap_fill] %p, %p, %p, %u, %x\n", h, dst, src, len, extra);
  if (len >= FourGB ||
      (uintptr_t)dst < m->base_addr || (uintptr_t)dst + len > m->base_addr + m->sz) {
    dprintf(STDERR_FILENO, "[code_heap_fill] illegal dst %p, %p, %p, %x, %x\n",
            h, dst, src, len, extra);
    quit(-1);
  }
  void *p = dst - (void*)m->base_addr + (void*)m->osb_base_addr;
  if (data(flags)) {
    /* the entire data should be either in data areas or code areas */
    int area = which_area(m->code_data_bitmap, dst - (void*)m->base_addr, len);
    if (ROCK_INVALID == area) {
      //dprintf(STDERR_FILENO, "[code_heap_fill] %p, %p, %p, %u, %x\n", h, dst, src, len, extra);
      unsigned long *rsp = (unsigned long*)thread_self()->user_ctx.rsp;
      dprintf(STDERR_FILENO, "[code_heap_fill data] crossing boundary of code and data %p, %x\n",
              thread_self()->continuation, *rsp);
      quit(-1);
    } else if (ROCK_CODE == area) {
      size_t old_len = len;
      while ((uintptr_t)dst >= m->base_addr &&
             ((char*)table)[(uintptr_t)dst] != (char)DCV) {
        --dst;
        ++len;
      }
      //dprintf(STDERR_FILENO, "%p, %d\n", dst, ((char*)table)[(uintptr_t)dst]);
      void *src_code = malloc(len);
      assert(src_code);
      // copy the opcode
      memcpy(src_code, dst, len - old_len);
      // copy the patch
      memcpy(src_code + len - old_len, src, old_len);
      // do the patch
      code_heap_fill(h, dst, src_code, len, (void*)(ROCK_CODE | ROCK_REPLACE));
      free(src_code);
      return;
    }

    switch (len) {
    case 1:
      *((char*)p) = *((char*)src);
      break;
    case 4:
      *((int*)p) = *((int*)src);
      break;
    case 8:
      *((long*)p) = *((long*)src);
      break;
    default:
      memcpy(p, src, len);
      break;
    }
  } else {
    //dprintf(STDERR_FILENO, "[code_heap_fill code] %p, %p, %p, %u, %x\n",
    //        h, dst, src, len, extra);
    int area = which_area(m->code_data_bitmap, dst - (void*)m->base_addr, len);
    /* pure code */
    if (flags & ROCK_COPY) {
      assert(ROCK_DATA == area);
      memcpy(p, src, len);
      flags |= ROCK_VERIFY;
      flags &= (~ROCK_REPLACE);
    }

    if (flags & ROCK_VERIFY) {
      assert(((uintptr_t)dst & 7) == 0); // each code piece should start at 8-byte aligned addr
      char *tary = malloc(len);
      memset(tary, 0, len);
      verify_jitted_code(m, (unsigned char*)dst, len, tary, (long)dst, TRUE);
      memcpy(table + (uintptr_t)dst, tary, len);
      free(tary);
      set_code(m->code_data_bitmap, dst - (void*)m->base_addr, len);
      flags &= (~ROCK_REPLACE);
    }

    if (flags & ROCK_REPLACE) {
      //dprintf(STDERR_FILENO, "[cdbmp] %x\n", m->code_data_bitmap[(size_t)dst/8]);
      if (ROCK_CODE != area) {
        //dprintf(STDERR_FILENO, "[code_heap_fill] %p, %p, %p, %u, %x, %d, %x\n",
        //        h, dst, src, len, extra, area, m->code_data_bitmap[(size_t)dst/8]);
        return;
      }
      // Code patches should start at an instruction boundary
      // TODO: mcficall return sites may also be a patch point in general, but since
      //       it is not for v8, we will consider it later.
      if (((char*)table)[(uintptr_t)dst] != (char)DCV) {
        dprintf(STDERR_FILENO,
                "[code_heap_fill replace] the patch does not start at an instruction boudary\n");
        quit(-1);
      }
      char *code = malloc(len*3);
      char *tary = code + len;
      memset(tary, 0, len);
      char *safe_code = code + len*2;
      char *old_tary = (char*)table + (uintptr_t)dst;
      char *safe_old_code = code;
      // copy out the code
      memcpy(code, src, len);
      // copy out the safe code
      memcpy(safe_code, src, len);
      // check to see if the patch is made at the end of the code
      area = which_area(m->code_data_bitmap, dst - (void*)m->base_addr + len, 1);
      if (ROCK_CODE == area)
        verify_jitted_code(m, (unsigned char*)code, len, tary, (long)dst, FALSE);
      else
        verify_jitted_code(m, (unsigned char*)code, len, tary, (long)dst, TRUE);

      // same_internal_boundary also patches every instruction's first byte
      // to be DCV
      if (same_internal_boundary(old_tary, tary, safe_old_code, safe_code, len)) {
        /* if the patch is within [8-byte aligned address, 8), then
           we use an 8-byte write to atomically write it */
        void *p_align = (void*)((unsigned long)p & (~7));
        if (p + len <= p_align + 8) {
          unsigned long v = *(unsigned long*)p_align;
          memcpy((char*)&v + (p - p_align), code, len);
          *(unsigned long*)p_align = v;
        } else {
          /* patch every instruction's first byte to be DCV */
          // Already done by same_internal_boundary
          /* do a cpuid to sync all current instruction streams */
          cpuid();
          /* copy the rest of each instruction */
          memcpy(p, safe_code, len);
          /* do a cpuid to sync all current instruction streams */
          cpuid();
          /* copy the first byte back */
          memcpy(p, code, len);
        }
      } else {
        /* patch every instruction's first byte to be DCV */
        // Already done by same_internal_boundary
        /* clear the tary table */
        memset(table + (uintptr_t)dst, 0x00, len);
        /* wait after a grace period so that no thread is sleeping in the region.*/
        //wait();
        /* make sure that no direct branch targets the old code's internal bytes */
        if (ROCK_DATA != which_area(m->internal_dbt_bitmap,
                                    (uintptr_t)dst - m->base_addr + 1, len)) {
          // TODO: here we should do a reachability analysis so that no direct branch
          // can target the patched code, since it might be possible that the patched
          // code contains branches to other parts of code, which would then branch
          // back to the patched code. See v8's tagged-to-i lithium instructions
          // generated after a lazy-bailout point.
          //dprintf(STDERR_FILENO, "[rock_code_heap_fill] invalid patch\n");
        }
        /* copy the the new code, but leave the first instruction's opcode to DCV
           to prevent any thread entering the patch before it is done. This assumes
           that there should be no direct branch targeting any internal part of the
           patched code, which should have been checked. */
        memcpy((char*)p + 1, code + 1, len - 1);
        /* set the tary table */
        memcpy(table + (uintptr_t)dst, tary, len);
        /* copy the first instruction's opcode */
        *(char*)p = *code;
      }
      free(code);
    }
  }
}

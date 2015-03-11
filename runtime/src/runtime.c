#include <def.h>
#include <syscall.h>
#include <mm.h>
#include <io.h>
#include <string.h>
#include <tcb.h>
#include <errno.h>
#include "pager.h"
#include <cfggen/cfggen.h>

static void* prog_brk = 0;
static void* max_brk = 0;
#define BRK_LEAP 0x800000

static TCB *tcb_list = 0;

/* tracks which thread escapes the untrusted space for how many times */
dict *thread_escape_map = 0;

#ifdef COLLECT_STAT
static unsigned int radc_patch_count = 0;
static unsigned int raic_patch_count = 0;
static unsigned int eqc_callgraph_count = 0;
static unsigned int eqc_retgraph_count = 0;
#endif

static int cfggened = FALSE;

extern code_module *modules;
static dict *patch_compensate = 0;
extern void *table; /* table region defined in main.c */
extern struct Vmmap VM;

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
      break;
    }
    tcb = tcb->next;
  }
}

void rock_patch(unsigned long patchpoint) {
#ifndef NO_ONLINE_PATCHING
  //dprintf(STDERR_FILENO, "patched %lx\n", patchpoint);
  code_module *m;
  int found = FALSE;
  DL_FOREACH(modules, m) {
    if (patchpoint >= m->base_addr &&
        patchpoint < m->base_addr + m->sz) {
      found = TRUE;
      break;
    }
  }
  /*
  assert(found);
  assert(patchpoint % 8 == 0 ||
         (patchpoint + 3) % 8 == 0||
         (patchpoint + 2) % 8 == 0);
  */
#ifdef COLLECT_STAT
  if (patchpoint % 8 == 0)
    ++radc_patch_count;
  else
    ++raic_patch_count;
#endif
  patchpoint = (patchpoint + 7) / 8 * 8;
  //dprintf(STDERR_FILENO, "%x, %x\n", m->base_addr, patchpoint - m->base_addr);
  keyvalue *patch = dict_find(m->ra_orig, (const void*)(patchpoint - m->base_addr));
  //assert(patch);
  //dprintf(STDERR_FILENO, "%x, %x, %lx, %x\n", m->base_addr, patch->key, patch->value, patch_count);

  if (cfggened)
    *((size_t*)(table + m->base_addr + (unsigned long)patch->key)) |= 1;
  else {
    dict_add(&patch_compensate, table + m->base_addr + (size_t)patch->key, 0);
  }

  /* the patch should be performed after the tary id is set valid */
  memcpy((char*)(m->osb_base_addr + (unsigned long)patch->key - 8),
         &(patch->value), 8);
#endif
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
      if (range_overlap(start, len, m->base_addr, m->sz) ||
          range_overlap(start, len, m->gotplt, m->gotpltsz))
        return TRUE;
    }
  }
  return FALSE;
}

void *rock_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
  if (prot & PROT_EXEC) {
    dprintf(STDERR_FILENO,
            "[rock_mmap] mmap(%p, %lx, %d, %d, %d, %ld) maps executable pages!\n",
            start, len, prot, flags, fd, off);
    quit(-1);
  }

  /* return mmap(start, len, prot, flags | MAP_32BIT, fd, off); */
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
  if ((unsigned long) addr > FourGB || len > FourGB ||
      (prot & PROT_EXEC)) {
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
  /*
  //dprintf(STDERR_FILENO, "[load_native_code] %d, %p, %lx\n", fd, load_addr, seg_base);
  size_t elf_size = 0;
  void *elf = load_opened_elf_into_memory(fd, &elf_size);
  code_module *cm = load_mcfi_metadata(elf);
  cm->base_addr = (uintptr_t)load_addr;
  DL_APPEND(modules, cm);
  replace_prog_seg(load_addr, elf);
  munmap(elf, elf_size);
  */
  return load_elf(fd, FALSE, 0);
}

static unsigned long version = 0;

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
        safe = FALSE;
      thesc->value = (void*)thescs;
    }
    tcb = tcb->next;
  }
  return safe;
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
  //dprintf(STDERR_FILENO, "[gen_cfg] called\n");
  icf *icfs = 0;
  function *functions = 0;
  dict *classes = 0;
  graph *cha = 0;
  dict *fats = 0;
  graph *aliases = 0;

  merge_mcfi_metainfo(modules, &icfs, &functions, &classes,
                      &cha, &fats, &aliases);

  graph *all_funcs_grouped_by_name = 0;
  graph *callgraph =
    build_callgraph(icfs, functions, classes, cha,
                    fats, aliases, &all_funcs_grouped_by_name);

  icfs_clear(&icfs);
  dict_clear(&classes);
  g_dtor(&cha);
  dict_clear(&fats);
  g_dtor(&aliases);

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
      gen_tary(m, callids, retids, table);
      gen_bary(m, callids, retids, table, id_for_others);
      populate_landingpads(m, table);
    }
  }
  
  DL_FOREACH(modules, m) {
    if (m->cfggened)
      gen_tary(m, callids, retids, table);
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

  return 0;
}

void take_addr_and_gen_cfg(unsigned long func_addr) {
  dprintf(STDERR_FILENO, "[take_addr_and_gen_cfg] %x\n", func_addr);
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
  memcpy((char*)(am->osb_gotplt) + addr - am->gotplt, &v, 8);
}

void unload_native_code(const char* code_file_name) {
}

void rock_clone(void) {
}

void rock_execve(void) {
}

void rock_shmat(void) {
}

void rock_shmdt(void) {
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

void collect_stat(void) {
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

  dprintf(STDERR_FILENO, "\n============MCFI Statistics============\n");
  dprintf(STDERR_FILENO, "Total Equivalence Classes: %u\n",
          eqc_callgraph_count + eqc_retgraph_count + eqclp);
  dprintf(STDERR_FILENO, "Forward-Edge Equivalence Classes: %u\n",
          eqc_callgraph_count);
  dprintf(STDERR_FILENO, "Back-Edge Equivalence Classes: %u\n",
          eqc_retgraph_count + eqclp);

  dprintf(STDERR_FILENO, "Total Indirect Branches: %u\n",
          ict_count + rt_count);
  dprintf(STDERR_FILENO, "Forward-Edges: %u\n", ict_count);
  dprintf(STDERR_FILENO, "Back-Edges: %u\n", rt_count);

  dprintf(STDERR_FILENO, "Total Indirect Branch Targets: %u\n",
          ibt_funcs + ibt_radcs + ibt_raics + lp_count);
  dprintf(STDERR_FILENO, "Functions Reachable by Indirect Branches: %u\n",
          ibt_funcs);
  dprintf(STDERR_FILENO, "Return Addresses of Direct Calls: %u\n",
          ibt_radcs);
  dprintf(STDERR_FILENO, "Return Addresses of Indirect Calls: %u\n",
          ibt_raics);
  dprintf(STDERR_FILENO, "Landing Pads: %u\n", lp_count);
#ifndef NO_ONLINE_PATCHING
  dprintf(STDERR_FILENO, "Total Patches (or Activated Return Addrs): %u\n",
          radc_patch_count + raic_patch_count);
  dprintf(STDERR_FILENO, "Activated Return Addrs of Direct Calls: %u\n",
          radc_patch_count);
  dprintf(STDERR_FILENO, "Activated Return Addrs of InDirect Calls: %u\n",
          raic_patch_count);
#endif
  dprintf(STDERR_FILENO, "\n");
#endif
}

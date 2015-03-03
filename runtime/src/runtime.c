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

extern struct Vmmap VM;

void set_tcb(unsigned long sb_tcb) {
  if (sb_tcb > FourGB) {
    report_error("[set_tcb] sandbox tcb is out of sandbox\n");
  }

  TCB* tcb = thread_self();
  tcb->tcb_inside_sandbox = (void*)sb_tcb;
  tcb_list = tcb; /* add the main thread to the tcb list */
}

void* allocset_tcb(unsigned long sb_tcb) {
  TCB* tcb = alloc_tcb();
  
  if (sb_tcb > FourGB) {
    report_error("[set_tcb] sandbox tcb is out of sandbox\n");
  }
  
  tcb->tcb_inside_sandbox = (void*)sb_tcb;
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
      break;
    }
    tcb = tcb->next;
  }
}

void rock_patch(unsigned long patchpoint) {
}

void *rock_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
  /* executable page mapping is not allowed
  if (prot & PROT_EXEC) {
    dprintf(STDERR_FILENO,
            "[rock_mmap] mmap(%p, %lx, %d, %d, %d, %ld) maps executable pages!\n",
            addr, len, prot, flags, fd, off);
    quit(-1);
    }*/
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
  result = mmap((void*)(page << PAGESHIFT), len, prot, flags | MAP_FIXED, fd, off);
  if (result == (void*)(page << PAGESHIFT))
    VmmapAddWithOverwrite(&VM, page,
                          pages,
                          prot,
                          prot,
                          VMMAP_ENTRY_ANONYMOUS);
  return result;
}

/**
 * Trusted mprotect that guarantees W xor X. The attackers may set executable
 * pages to be writable, but that drops the executable permissions and will
 * crash the program.
 */
int rock_mprotect(void *addr, size_t len, int prot) {
  if ((unsigned long) addr > FourGB || len > FourGB) {
    dprintf(STDERR_FILENO, "[rock_mprotect] mprotect(%ld, %lx, %d) is insecure!\n",
            (size_t)addr, len, prot);
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

extern code_module *modules;

char *load_opened_elf_into_memory(int fd,
                                  /*out*/size_t *elf_size_rounded_to_page_boundary);
code_module *load_mcfi_metadata(char *elf);

void replace_prog_seg(char *dest, char *src);

int load_native_code(int fd, void *load_addr, size_t seg_base) {
  dprintf(STDERR_FILENO, "[load_native_code] %d, %p, %lx\n", fd, load_addr, seg_base);
  size_t elf_size = 0;
  void *elf = load_opened_elf_into_memory(fd, &elf_size);
  code_module *cm = load_mcfi_metadata(elf);
  cm->base_addr = (uintptr_t)load_addr;
  DL_APPEND(modules, cm);
  replace_prog_seg(load_addr, elf);
  munmap(elf, elf_size);
  return 0;
}

static unsigned long version = 0;
extern void *table; /* table region defined in main.c */

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

/* generate the cfg */
int gen_cfg(void) {
  dprintf(STDERR_FILENO, "[gen_cfg] called\n");
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

  int count;
  node *n;
  DL_COUNT(lcg, n, count);

  dprintf(STDERR_FILENO, "Callgraph EQCs: %d\n", count);
  //quit(0);
  /* based on the callgraph, let's build the return graph on top of it */
  build_retgraph(&callgraph, all_funcs_grouped_by_name, modules);

  g_dtor(&all_funcs_grouped_by_name);
  functions_clear(&functions);

  node *lrt = g_get_lcc(&callgraph);
  //l_print(lrt, print_cfgcc);
  g_dtor(&callgraph);

  DL_COUNT(lrt, n, count);
  dprintf(STDERR_FILENO, "Retgraph EQCs: %d\n", count);
  //quit(0);
  unsigned long id_for_others;
  dict *callids = 0, *retids = 0;
  gen_mcfi_id(&lcg, &lrt, &version, &id_for_others, &callids, &retids);

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

  DL_FOREACH(modules, m) {
    if (!m->cfggened) {
      gen_tary(m, callids, retids, table);
      gen_bary(m, callids, retids, table, id_for_others);
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
  return 0;
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

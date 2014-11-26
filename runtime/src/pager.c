/*
 * Copyright (c) 2011 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 *  Simple/secure ELF loader ( SEL) memory map.
 */

#include "pager.h"
#include <mm.h>
#include <io.h>
#include <string.h>
#include <syscall.h>

#define START_ENTRIES   5   /* text, rodata, data, bss, stack */
#define REMOVE_MARKED_DEBUG 0


/*
 * The memory map structure is a simple array of memory regions which
 * may have different access protections.  We do not yet merge regions
 * with the same access protections together to reduce the region
 * number, but may do so in the future.
 *
 * Regions are described by (relative) starting page number, the
 * number of pages, and the protection that the pages should have.
 */
struct VmmapEntry *VmmapEntryMake(uintptr_t               page_num,
                                  size_t                  npages,
                                  int                     prot,
                                  int                     max_prot,
                                  enum VmmapEntryType vmmap_type) {
  struct VmmapEntry *entry;
#if 0
  Log(4,
      "VmmapEntryMake(0x%"NACL_PRIxPTR",0x%"NACL_PRIxS","
      "0x%x,0x%x,0x%x)\n",
      page_num, npages, prot, max_prot, (int) vmmap_type);
#endif  
  entry = (struct VmmapEntry *) malloc(sizeof *entry);
  if (NULL == entry) {
    return 0;
  }
#if 0  
  Log(4, "entry: 0x%"NACL_PRIxPTR"\n", (uintptr_t) entry);
#endif  
  entry->page_num = page_num;
  entry->npages = npages;
  entry->max_prot = max_prot;
  entry->prot = prot;
  entry->vmmap_type = vmmap_type;
  entry->removed = 0;
  return entry;
}


void  VmmapEntryFree(struct VmmapEntry *entry) {
#if 0  
  Log(4,
      ("VmmapEntryFree(0x%08"NACL_PRIxPTR
       "): (0x%"NACL_PRIxPTR",0x%"NACL_PRIxS","
       "0x%x,0x%x,0x%x)\n"),
      (uintptr_t) entry,
      entry->page_num, entry->npages, entry->max_prot, entry->prot,
      (int) entry->vmmap_type);
#endif
  free(entry);
}


/*
 * Print debug.
 */
void VmentryPrint(void                  *state,
                  struct VmmapEntry *vmep) {
#if 0  
  UNREFERENCED_PARAMETER(state);

  printf("page num 0x%06x\n", (uint32_t)vmep->page_num);
  printf("num pages %d\n", (uint32_t)vmep->npages);
  printf("prot bits %x\n", vmep->prot);
  fflush(stdout);
#endif  
}


void VmmapDebug(struct Vmmap *self,
                char             *msg) {
#if 0  
  puts(msg);
  VmmapVisit(self, VmentryPrint, (void *) 0);
  fflush(stdout);
#endif  
}


int VmmapCtor(struct Vmmap *self) {
  self->size = START_ENTRIES;
  if (SIZE_T_MAX / sizeof *self->vmentry < self->size) {
    return 0;
  }
  self->vmentry = malloc(self->size * sizeof *self->vmentry);
  if (!self->vmentry) {
    return 0;
  }
  memset(self->vmentry, 0, self->size * sizeof *self->vmentry);
  self->nvalid = 0;
  self->is_sorted = 1;
  return 1;
}


void VmmapDtor(struct Vmmap *self) {
  size_t i;

  for (i = 0; i < self->nvalid; ++i) {
    VmmapEntryFree(self->vmentry[i]);
  }
  free(self->vmentry);
  self->vmentry = 0;
}

/*
 * Comparison function for qsort.  Should never encounter a
 * removed/invalid entry.
 */

static int VmmapCmpEntries(void const  *vleft,
                           void const  *vright) {
  struct VmmapEntry const *const *left =
    (struct VmmapEntry const *const *) vleft;
  struct VmmapEntry const *const *right =
    (struct VmmapEntry const *const *) vright;

  return (int) ((*left)->page_num - (*right)->page_num);
}


static void VmmapRemoveMarked(struct Vmmap *self) {
  size_t  i;
  size_t  last;

  if (0 == self->nvalid)
    return;

#if REMOVE_MARKED_DEBUG
  VmmapDebug(self, "Before RemoveMarked");
#endif
  /*
   * Linearly scan with a move-end-to-front strategy to get rid of
   * marked-to-be-removed entries.
   */

  /*
   * Invariant:
   *
   * forall j in [0, self->nvalid): NULL != self->vmentry[j]
   */
  for (last = self->nvalid; last > 0 && self->vmentry[--last]->removed; ) {
    VmmapEntryFree(self->vmentry[last]);
    self->vmentry[last] = NULL;
  }
  if (last == 0 && self->vmentry[0]->removed) {
#if 0    
    Log(LOG_FATAL, "No valid entries in VM map\n");
#endif
    quit(-1);
    return;
  }

  /*
   * Post condition of above loop:
   *
   * forall j in [0, last]: NULL != self->vmentry[j]
   *
   * 0 <= last < self->nvalid && !self->vmentry[last]->removed
   */
  CHECK(last < self->nvalid);
  CHECK(!self->vmentry[last]->removed);
  /*
   * and,
   *
   * forall j in (last, self->nvalid): NULL == self->vmentry[j]
   */

  /*
   * Loop invariant: forall j in [0, i):  !self->vmentry[j]->removed
   */
  for (i = 0; i < last; ++i) {
    if (!self->vmentry[i]->removed) {
      continue;
    }
    /*
     * post condition: self->vmentry[i]->removed
     *
     * swap with entry at self->vmentry[last].
     */

    VmmapEntryFree(self->vmentry[i]);
    self->vmentry[i] = self->vmentry[last];
    self->vmentry[last] = NULL;

    /*
     * Invariants here:
     *
     * forall j in [last, self->nvalid): NULL == self->vmentry[j]
     *
     * forall j in [0, i]: !self->vmentry[j]->removed
     */

    while (--last > i && self->vmentry[last]->removed) {
      VmmapEntryFree(self->vmentry[last]);
      self->vmentry[last] = NULL;
    }
    /*
     * since !self->vmentry[i]->removed, we are guaranteed that
     * !self->vmentry[last]->removed when the while loop terminates.
     *
     * forall j in (last, self->nvalid):
     *  NULL == self->vmentry[j]->removed
     */
  }
  /* i == last */
  /* forall j in [0, last]: !self->vmentry[j]->removed */
  /* forall j in (last, self->nvalid): NULL == self->vmentry[j] */
  self->nvalid = last + 1;

  self->is_sorted = 0;
#if REMOVE_MARKED_DEBUG
  VmmapDebug(self, "After RemoveMarked");
#endif
}


void VmmapMakeSorted(struct Vmmap  *self) {
  if (self->is_sorted)
    return;

  VmmapRemoveMarked(self);

  qsort(self->vmentry,
        self->nvalid,
        sizeof *self->vmentry,
        VmmapCmpEntries);
  self->is_sorted = 1;
#if REMOVE_MARKED_DEBUG
  VmmapDebug(self, "After Sort");
#endif
}

void VmmapAdd(struct Vmmap          *self,
              uintptr_t             page_num,
              size_t                npages,
              int                   prot,
              int                   max_prot,
              enum VmmapEntryType   vmmap_type) {
  struct VmmapEntry *entry;
#if 0
  Log(2,
      ("VmmapAdd(0x%08"NACL_PRIxPTR", 0x%"NACL_PRIxPTR", "
       "0x%"NACL_PRIxS", 0x%x, 0x%x, 0x%x)\n"),
      (uintptr_t) self, page_num, npages, prot, max_prot,
      (int) vmmap_type);
#endif
  
  if (self->nvalid == self->size) {
    size_t                new_size = 2 * self->size;
    struct VmmapEntry     **new_map;

    new_map = realloc(self->vmentry, new_size * sizeof *new_map);
    if (NULL == new_map) {
#if 0      
      Log(LOG_FATAL, "VmmapAdd: could not allocate memory\n");
#endif
      quit(-1);
      return;
    }
    self->vmentry = new_map;
    self->size = new_size;
  }
  /* self->nvalid < self->size */
  entry = VmmapEntryMake(page_num, npages, prot, max_prot, vmmap_type);

  self->vmentry[self->nvalid] = entry;
  self->is_sorted = 0;
  ++self->nvalid;
}

/*
 * Update the virtual memory map.  Deletion is handled by a remove
 * flag, since a NACL_VMMAP_ENTRY_ANONYMOUS vmmap_type just means
 * that the memory is backed by the system paging file.
 */
static void VmmapUpdate(struct Vmmap          *self,
                        uintptr_t             page_num,
                        size_t                npages,
                        int                   prot,
                        int                   max_prot,
                        enum VmmapEntryType   vmmap_type,
                        int                   remove) {
  /* update existing entries or create new entry as needed */
  size_t                i;
  uintptr_t             new_region_end_page = page_num + npages;
  
#if 0
  Log(2,
      ("VmmapUpdate(0x%08"NACL_PRIxPTR", 0x%"NACL_PRIxPTR
       ", 0x%"NACL_PRIxS", 0x%x, 0x%x, 0x%x, 0x%x)\n"),
      (uintptr_t) self, page_num, npages, prot, max_prot,
      (int) vmmap_type, remove);
#endif
  
  VmmapMakeSorted(self);

  CHECK(npages > 0);

  for (i = 0; i < self->nvalid; i++) {
    struct VmmapEntry *ent = self->vmentry[i];
    uintptr_t             ent_end_page = ent->page_num + ent->npages;

    if (ent->page_num < page_num && new_region_end_page < ent_end_page) {
      /*
       * Split existing mapping into two parts, with new mapping in
       * the middle.
       */
      VmmapAdd(self,
               new_region_end_page,
               ent_end_page - new_region_end_page,
               ent->prot,
               ent->max_prot,
               ent->vmmap_type);
      ent->npages = page_num - ent->page_num;
      break;
    } else if (ent->page_num < page_num && page_num < ent_end_page) {
      /* New mapping overlaps end of existing mapping. */
      ent->npages = page_num - ent->page_num;
    } else if (ent->page_num < new_region_end_page &&
               new_region_end_page < ent_end_page) {
      /* New mapping overlaps start of existing mapping. */
      ent->page_num = new_region_end_page;
      ent->npages = ent_end_page - new_region_end_page;
      break;
    } else if (page_num <= ent->page_num &&
               ent_end_page <= new_region_end_page) {
      /* New mapping covers all of the existing mapping. */
      ent->removed = 1;
    } else {
      /* No overlap */
      CHECK(new_region_end_page <= ent->page_num || ent_end_page <= page_num);
    }
  }

  if (!remove) {
    VmmapAdd(self, page_num, npages, prot, max_prot, vmmap_type);
  }

  VmmapRemoveMarked(self);
}

void VmmapAddWithOverwrite(struct Vmmap         *self,
                           uintptr_t            page_num,
                           size_t               npages,
                           int                  prot,
                           int                  max_prot,
                           enum VmmapEntryType  vmmap_type) {
  VmmapUpdate(self,
              page_num,
              npages,
              prot,
              max_prot,
              vmmap_type,
              /* remove= */ 0);
}

void VmmapRemove(struct Vmmap         *self,
                 uintptr_t            page_num,
                 size_t               npages,
                 enum VmmapEntryType  vmmap_type) {
  VmmapUpdate(self,
              page_num,
              npages,
              /* prot= */ 0,
              /* max_prot= */ 0,
              vmmap_type,
              /* remove= */ 1);
}

int VmmapChangeProt(struct Vmmap   *self,
                    uintptr_t      page_num,
                    size_t         npages,
                    int            prot) {
  size_t      i;
  size_t      nvalid;
  uintptr_t   new_region_end_page = page_num + npages;

  /*
   * VmmapCheckExistingMapping should be always called before
   * VmmapChangeProt proceeds to ensure that valid mapping exists
   * as modifications cannot be rolled back.
   */
  if (!VmmapCheckExistingMapping(self, page_num, npages, prot)) {
    return 0;
  }
#if 0
  Log(2,
      ("VmmapChangeProt(0x%08"NACL_PRIxPTR", 0x%"NACL_PRIxPTR
       ", 0x%"NACL_PRIxS", 0x%x)\n"),
      (uintptr_t) self, page_num, npages, prot);
#endif  
  VmmapMakeSorted(self);

  /*
   * This loop & interval boundary tests closely follow those in
   * VmmapUpdate. When updating those, do not forget to update them
   * at both places where appropriate.
   * TODO(phosek): use better data structure which will support intervals
   */

  for (i = 0, nvalid = self->nvalid; i < nvalid && npages > 0; i++) {
    struct VmmapEntry *ent = self->vmentry[i];
    uintptr_t             ent_end_page = ent->page_num + ent->npages;

    if (ent->page_num < page_num && new_region_end_page < ent_end_page) {
      /* Split existing mapping into two parts */
      VmmapAdd(self,
               new_region_end_page,
               ent_end_page - new_region_end_page,
               ent->prot,
               ent->max_prot,
               ent->vmmap_type);
      ent->npages = page_num - ent->page_num;
      /* Add the new mapping into the middle. */
      VmmapAdd(self,
               page_num,
               npages,
               prot,
               ent->max_prot,
               ent->vmmap_type);
      break;
    } else if (ent->page_num < page_num && page_num < ent_end_page) {
      /* New mapping overlaps end of existing mapping. */
      ent->npages = page_num - ent->page_num;
      /* Add the overlapping part of the mapping. */
      VmmapAdd(self,
               page_num,
               ent_end_page - page_num,
               prot,
               ent->max_prot,
               ent->vmmap_type);
      /* The remaining part (if any) will be added in other iteration. */
      page_num = ent_end_page;
      npages = new_region_end_page - ent_end_page;
    } else if (ent->page_num < new_region_end_page &&
               new_region_end_page < ent_end_page) {
      /* New mapping overlaps start of existing mapping, split it. */
      VmmapAdd(self,
               page_num,
               npages,
               prot,
               ent->max_prot,
               ent->vmmap_type);
      ent->page_num = new_region_end_page;
      ent->npages = ent_end_page - new_region_end_page;
      break;
    } else if (page_num <= ent->page_num &&
               ent_end_page <= new_region_end_page) {
      /* New mapping covers all of the existing mapping. */
      page_num = ent_end_page;
      npages = new_region_end_page - ent_end_page;
      ent->prot = prot;
    } else {
      /* No overlap */
      CHECK(new_region_end_page <= ent->page_num || ent_end_page <= page_num);
    }
  }
  return 1;
}

int VmmapCheckExistingMapping(struct Vmmap  *self,
                              uintptr_t     page_num,
                              size_t        npages,
                              int           prot) {
  size_t      i;
  uintptr_t   region_end_page = page_num + npages;
#if 0
  Log(2,
      ("VmmapCheckExistingMapping(0x%08"NACL_PRIxPTR", 0x%"NACL_PRIxPTR
       ", 0x%"NACL_PRIxS", 0x%x)\n"),
      (uintptr_t) self, page_num, npages, prot);
#endif
  if (0 == self->nvalid) {
    return 0;
  }
  VmmapMakeSorted(self);

  for (i = 0; i < self->nvalid; ++i) {
    struct VmmapEntry   *ent = self->vmentry[i];
    uintptr_t               ent_end_page = ent->page_num + ent->npages;

    if (ent->page_num <= page_num && region_end_page <= ent_end_page) {
      /* The mapping is inside existing entry. */
      return 0 == (prot & (~ent->max_prot));
    } else if (ent->page_num <= page_num && page_num < ent_end_page) {
      /* The mapping overlaps the entry. */
      if (0 != (prot & (~ent->max_prot))) {
        return 0;
      }
      page_num = ent_end_page;
      npages = region_end_page - ent_end_page;
    } else if (page_num < ent->page_num) {
      /* The mapping without backing store. */
      return 0;
    }
  }
  return 0;
}

static int VmmapContainCmpEntries(void const *vkey,
                                  void const *vent) {
  struct VmmapEntry const *const *key =
    (struct VmmapEntry const *const *) vkey;
  struct VmmapEntry const *const *ent =
    (struct VmmapEntry const *const *) vent;
#if 0
  Log(5, "key->page_num   = 0x%05"NACL_PRIxPTR"\n", (*key)->page_num);

  Log(5, "entry->page_num = 0x%05"NACL_PRIxPTR"\n", (*ent)->page_num);
  Log(5, "entry->npages   = 0x%"NACL_PRIxS"\n", (*ent)->npages);
#endif
  if ((*key)->page_num < (*ent)->page_num) return -1;
  if ((*key)->page_num < (*ent)->page_num + (*ent)->npages) return 0;
  return 1;
}

struct VmmapEntry const *VmmapFindPage(struct Vmmap *self,
                                       uintptr_t    pnum) {
  struct VmmapEntry key;
  struct VmmapEntry *kptr;
  struct VmmapEntry *const *result_ptr;

  VmmapMakeSorted(self);
  key.page_num = pnum;
  kptr = &key;
  result_ptr = ((struct VmmapEntry *const *)
                bsearch(&kptr,
                        self->vmentry,
                        self->nvalid,
                        sizeof self->vmentry[0],
                        VmmapContainCmpEntries));
  return result_ptr ? *result_ptr : NULL;
}


struct VmmapIter *VmmapFindPageIter(struct Vmmap      *self,
                                    uintptr_t         pnum,
                                    struct VmmapIter  *space) {
  struct VmmapEntry key;
  struct VmmapEntry *kptr;
  struct VmmapEntry **result_ptr;

  VmmapMakeSorted(self);
  key.page_num = pnum;
  kptr = &key;
  result_ptr = ((struct VmmapEntry **)
                bsearch(&kptr,
                        self->vmentry,
                        self->nvalid,
                        sizeof self->vmentry[0],
                        VmmapContainCmpEntries));
  space->vmmap = self;
  if (NULL == result_ptr) {
    space->entry_ix = self->nvalid;
  } else {
    space->entry_ix = result_ptr - self->vmentry;
  }
  return space;
}


int VmmapIterAtEnd(struct VmmapIter *nvip) {
  return nvip->entry_ix >= nvip->vmmap->nvalid;
}


/*
 * IterStar only permissible if not AtEnd
 */
struct VmmapEntry *VmmapIterStar(struct VmmapIter *nvip) {
  return nvip->vmmap->vmentry[nvip->entry_ix];
}


void VmmapIterIncr(struct VmmapIter *nvip) {
  ++nvip->entry_ix;
}


/*
 * Iterator becomes invalid after Erase.  We could have a version that
 * keep the iterator valid by copying forward, but it is unclear
 * whether that is needed.
 */
void VmmapIterErase(struct VmmapIter *nvip) {
  struct Vmmap  *nvp;

  nvp = nvip->vmmap;
  free(nvp->vmentry[nvip->entry_ix]);
  nvp->vmentry[nvip->entry_ix] = nvp->vmentry[--nvp->nvalid];
  nvp->is_sorted = 0;
}


void  VmmapVisit(struct Vmmap *self,
                 void             (*fn)(void *state,
                                        struct VmmapEntry *entry),
                 void             *state) {
  size_t i;
  size_t nentries;

  VmmapMakeSorted(self);
  for (i = 0, nentries = self->nvalid; i < nentries; ++i) {
    (*fn)(state, self->vmentry[i]);
  }
}


/*
 * Linear search, from high addresses down.
 */
uintptr_t VmmapFindSpace(struct Vmmap *self,
                         size_t       num_pages) {
  size_t                i;
  struct VmmapEntry *vmep;
  uintptr_t             end_page;
  uintptr_t             start_page;

  if (0 == self->nvalid)
    return 0;
  VmmapMakeSorted(self);
  for (i = self->nvalid; --i > 0; ) {
    vmep = self->vmentry[i-1];
    end_page = vmep->page_num + vmep->npages;  /* end page from previous */
    start_page = self->vmentry[i]->page_num;  /* start page from current */
    if (start_page - end_page >= num_pages) {
      return start_page - num_pages;
    }
  }
  return 0;
  /*
   * in user addresses, page 0 is always trampoline, and user
   * addresses are contained in system addresses, so returning a
   * system page number of 0 can serve as error indicator: it is at
   * worst the trampoline page, and likely to be below it.
   */
}


/*
 * Linear search, from high addresses down.  For mmap, so the starting
 * address of the region found must be NACL_MAP_PAGESIZE aligned.
 *
 * For general mmap it is better to use as high an address as
 * possible, since the stack size for the main thread is currently
 * fixed, and the heap is the only thing that grows.
 */
uintptr_t VmmapFindMapSpace(struct Vmmap *self,
                            size_t       num_pages) {
  size_t                i;
  struct VmmapEntry *vmep;
  uintptr_t             end_page;
  uintptr_t             start_page;

  if (0 == self->nvalid)
    return 0;
  VmmapMakeSorted(self);

  for (i = self->nvalid; --i > 0; ) {
    vmep = self->vmentry[i-1];
    end_page = vmep->page_num + vmep->npages;  /* end page from previous */

    start_page = self->vmentry[i]->page_num;  /* start page from current */

    if (start_page - end_page >= num_pages) {
      return start_page - num_pages;
    }
  }
  return 0;
  /*
   * in user addresses, page 0 is always trampoline, and user
   * addresses are contained in system addresses, so returning a
   * system page number of 0 can serve as error indicator: it is at
   * worst the trampoline page, and likely to be below it.
   */
}


/*
 * Linear search, from uaddr up.
 */
uintptr_t VmmapFindMapSpaceAboveHint(struct Vmmap *self,
                                     uintptr_t    uaddr,
                                     size_t       num_pages) {
  size_t                nvalid;
  size_t                i;
  struct VmmapEntry *vmep;
  uintptr_t             usr_page;
  uintptr_t             start_page;
  uintptr_t             end_page;

  VmmapMakeSorted(self);

  usr_page = uaddr >> PAGESHIFT;

  nvalid = self->nvalid;

  for (i = 1; i < nvalid; ++i) {
    vmep = self->vmentry[i-1];
    end_page = vmep->page_num + vmep->npages;

    start_page = self->vmentry[i]->page_num;

    if (end_page <= usr_page && usr_page < start_page) {
      end_page = usr_page;
    }
    if (usr_page <= end_page && (start_page - end_page) >= num_pages) {
      /* found a gap at or after uaddr that's big enough */
      return end_page;
    }
  }
  return 0;
}

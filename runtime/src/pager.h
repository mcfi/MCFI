/*
 * Copyright (c) 2011 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 *  Simple/secure ELF loader ( SEL) memory map.
 */

#ifndef PAGER_H_
#define PAGER_H_

#include <def.h>

#define PAGESHIFT 12

/*
 * Interface is based on setting properties and query properties by
 * page numbers (addr >> PAGESHIFT) and the number of pages
 * affected (for setting properties).
 *
 * Initially, the address space is empty, with all memory
 * inaccessible.  As the program is loaded, pages are marked
 * accessible -- text pages are non-writable, data and stack are
 * writable.  At runtime, shared memory buffers are allocated by
 * looking at the first memory hole that fits, starting down from the
 * stack.
 *
 * The simple data structure that we use is a sorted array of valid
 * memory regions.
 */

/*
 * VmmapEntryType exists for Windows support. NACL_VMMAP_ENTRY_ANONYMOUS
 * indicates anonymous memory mappings that were created with VirtualAlloc()
 * and must therefore be unmapped using VirtualFree(). NACL_VMMAP_ENTRY_MAPPED
 * indicates mappings that were created with MapViewOfFileEx() and must
 * therefore be unmapped using UnmapViewOfFile().
 */
enum VmmapEntryType {
  VMMAP_ENTRY_ANONYMOUS = 1,
  VMMAP_ENTRY_MAPPED
};

struct VmmapEntry {
  uintptr_t               page_num;   /* base virtual addr >> PAGESHIFT */
  size_t                  npages;     /* number of pages */
  int                     prot;       /* mprotect attribute */
  int                     max_prot;   /* maximum protection */
  enum VmmapEntryType vmmap_type;     /* memory entry type */
  int                     removed;    /* flag set in VmmapUpdate */
};

struct Vmmap {
  struct VmmapEntry **vmentry;       /* must not overlap */
  size_t                nvalid, size;
  int                   is_sorted;
};

void VmmapDebug(struct Vmmap  *self,
                char              *msg);

/*
 * iterators methods are final for now.
 */
struct VmmapIter {
  struct Vmmap      *vmmap;
  size_t                 entry_ix;
};

int                   VmmapIterAtEnd(struct VmmapIter *nvip);
struct VmmapEntry *VmmapIterStar(struct VmmapIter *nvip);
void                  VmmapIterIncr(struct VmmapIter *nvip);
void                  VmmapIterErase(struct VmmapIter *nvip);

int   VmmapCtor(struct Vmmap  *self);

void  VmmapDtor(struct Vmmap  *self);

/*
 * VmmapAdd does not check whether the newly mapped region overlaps
 * with any existing ones. This function is intended for sandbox startup
 * only when non-overlapping mappings are being added.
 */
void  VmmapAdd(struct Vmmap         *self,
               uintptr_t            page_num,
               size_t               npages,
               int                  prot,
               int                  max_prot,
               enum VmmapEntryType  vmmap_type);

/*
 * VmmapAddWithOverwrite checks the existing mappings and resizes
 * them if necessary to fit in the newly mapped region.
 */
void  VmmapAddWithOverwrite(struct Vmmap          *self,
                            uintptr_t                 page_num,
                            size_t                    npages,
                            int                       prot,
                            int                       max_prot,
                            enum VmmapEntryType   vmmap_type);

/*
 * VmmapRemove modifies the specified region and updates the existing
 * mappings if necessary.
 */
void  VmmapRemove(struct Vmmap          *self,
                  uintptr_t                 page_num,
                  size_t                    npages,
                  enum VmmapEntryType   vmmap_type);

/*
 * VmmapChangeProt updates the protection bits for the specified region.
 */
int VmmapChangeProt(struct Vmmap  *self,
                    uintptr_t         page_num,
                    size_t            npages,
                    int               prot);

/*
 * VmmapCheckMapping checks whether there is an existing mapping with
 * maximum protection equivalent or higher to the given one.
 */
int VmmapCheckExistingMapping(struct Vmmap  *self,
                              uintptr_t         page_num,
                              size_t            npages,
                              int               prot);

/*
 * VmmapFindPage and VmmapFindPageIter only works if pnum is
 * in the Vmmap.  If not, NULL and an AtEnd iterator is returned.
 */
struct VmmapEntry const *VmmapFindPage(struct Vmmap *self,
                                       uintptr_t        pnum);

struct VmmapIter *VmmapFindPageIter(struct Vmmap      *self,
                                    uintptr_t             pnum,
                                    struct VmmapIter  *space);

/*
 * Visitor pattern, call fn on every entry.
 */
void  VmmapVisit(struct Vmmap   *self,
                 void               (*fn)(void                  *state,
                                          struct VmmapEntry *entry),
                 void               *state);

/*
 * Returns page number starting at which there is a hole of at least
 * num_pages in size.  Linear search from high addresses on down.
 */
uintptr_t VmmapFindSpace(struct Vmmap *self,
                         size_t           num_pages);

/*
 * Just lke VmmapFindSpace, except usage is intended for
 * HostDescMap, so the starting address of the region found must
 * be NACL_MAP_PAGESIZE aligned.
 */
uintptr_t VmmapFindMapSpace(struct Vmmap *self,
                            size_t           num_pages);

uintptr_t VmmapFindMapSpaceAboveHint(struct Vmmap *self,
                                     uintptr_t        uaddr,
                                     size_t           num_pages);

void VmmapMakeSorted(struct Vmmap  *self);

#define CurPage(x) ((x >> PAGESHIFT) << PAGESHIFT)
#define RoundToPage(x) ((x + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE)

#endif

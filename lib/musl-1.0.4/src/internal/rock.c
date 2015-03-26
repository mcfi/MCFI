#include "trampolines.h"
#include <rock.h>

void *rock_create_code_heap(void **ph, unsigned long sz) {
  return trampoline_create_code_heap((unsigned long)ph, sz);
}

void rock_code_heap_fill(void *h, void *dst, const void *src, unsigned long len,
                         const void *extra) {
  trampoline_code_heap_fill(h, dst, src, len, extra);
}

void rock_reg_cfg_metadata(const void *h, int type, const void *md, const void *extra) {
  trampoline_reg_cfg_metadata(h, type, md, extra);
}

void rock_delete_code(const void *h,
                      const void *addr, unsigned long length) {
  trampoline_delete_code(h, addr, length);
}

void rock_move_code(const void *h, const void *target, const void *source, unsigned long length) {
  trampoline_move_code(h, target, source, length);
}

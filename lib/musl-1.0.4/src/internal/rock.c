#include "trampolines.h"
#include <rock.h>

void *rock_create_code_heap(void **ph, unsigned long sz) {
  return trampoline_create_code_heap((unsigned long)ph, sz);
}

void rock_reg_cfg_metadata(const void *h, int type, const void *md, const void *extra) {
  trampoline_reg_cfg_metadata(h, type, md, extra);
}

void rock_delete_code(const void *h,
                      const void *addr, unsigned long length) {
  trampoline_delete_code(h, addr, length);
}

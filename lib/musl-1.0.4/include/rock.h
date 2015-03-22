#ifndef __ROCK_H
#define __ROCK_H

#define ROCK_FUNCTION      0
#define ROCK_FUNCTION_SYM  1
#define ROCK_ICJ           2
#define ROCK_ICJ_SYM       3
#define ROCK_RAI           4
#define ROCK_ICJ_UNREG     5
#define ROCK_ICJ_SYM_UNREG 6
#define ROCK_RAI_UNREG     7

#ifdef __cplusplus
extern "C" {
#endif
  void rock_reg_cfg_metadata(const void *h, int type, const void *md, const void *extra);
  void *rock_create_code_heap(void **ph, unsigned long size);
  void rock_add_cfg_edge_combo(const void *h, const char *name,
                               const void *bary, const void *rai);
  void rock_gen_cfg(void);
#ifdef __cplusplus
}
#endif
#endif

/* procedures for general shadow code heap manipulation
     cover
     deletion
*/

#include <def.h>
#include <tcb.h>

char *PSA = 0;
char *IBT = 0;
char *DBT = 0;

struct code_region_t {
  const char *content;
  size_t len;
};

char* code_heap = 0;

int create_code_heap(size_t sz) {
}

/* code installation and modification are both handled
   by dyncode_cover, which covers the code heap area with new content.
*/
int dyncode_cover(const char *content, /* mixture of code + data */
                  char *area,          /* which code heap area is to cover */
                  size_t len,          /* how many bytes */
                  const char *bitmap   /* differentiates code from data */) {
}

int dyncode_delete(const char *area, /* mixture of code + data */
                   size_t len        /* how many bytes */) {
}

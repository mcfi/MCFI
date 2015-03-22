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

/* code installation
*/
int dyncode_install(const char *content, /* mixture of code + data */
                    char *area,          /* which code heap area is to install */
                    size_t len,          /* how many bytes */
                    const char *bitmap   /* differentiates code from data */) {
  return 0;
}

/* code modification
 */
int dyncode_modify(const char *content, /* mixture of code + data */
                   char *area,          /* which code heap area is to replace */
                   size_t len,          /* how many bytes */
                   const char *bitmap   /* differentiates code from data */) {
  return 0;
}

int dyncode_delete(const char *area, /* mixture of code + data */
                   size_t len        /* how many bytes */) {
  return 0;
}

#include "stringpool.h"

str *stringpool = NULL;

int main() {
  sp_add_cpy(&stringpool, "string", 0);
  sp_add_cpy(&stringpool, "pool", 0);
  sp_add_cpy(&stringpool, "ohyear", 0);
  sp_print(&stringpool);
  sp_dtor(&stringpool);
}

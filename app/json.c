#include <stdio.h>
#include "json.h"

void json_print(Value *val) {
  if (val->type == Integer) {
    printf("%lld", val->val.integer);
  } else if (val->type == String) {
    printf("\"%s\"", val->val.string);
  }
}

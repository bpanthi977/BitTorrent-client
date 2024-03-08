#include <stdio.h>
#include "json.h"

void json_print(Value *val) {
  if (val->type == Integer) {
    printf("%lld", val->val.integer);
  } else if (val->type == String) {
    printf("\"%s\"", val->val.string);
  } else if (val->type == List) {
    LinkedList* list = val->val.list;
    printf("[");
    while (list != NULL) {
      json_print(list->val);
      if (list->next != NULL) printf(",");
      list = list->next;
    }
    printf("]");
  }
}

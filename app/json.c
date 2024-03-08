#include <stdio.h>
#include <stdlib.h>
#include "app.h"

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
  } else if (val->type == Dict) {
    printf("{");
    LinkedList* list = val->val.list;
    while (list != NULL) {
      if (list->val->type != Keyval) {
        fprintf(stderr, "ERROR: Expected Keyval in a dictionary. Got: %d", list->val->type);
        exit(1);
      }
      KeyVal *kv = list->val->val.kv;
      printf("\"%s\":", kv->key);
      json_print(kv->val);
      if (list->next != NULL) printf(",");
      list = list->next;
    }
    printf("}");
  }
}

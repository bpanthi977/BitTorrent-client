#include <stdio.h>
#include <stdlib.h>
#include "app.h"

void pindent(int indent) {
  if (indent)
    printf("%*s", indent, "");
}

void pprint_str(String* string) {
  putc('"', stdout);
  char *str = string->str;
  for (int i=0; i< string->length; i++) {
    if (*str >= 32 && *str <=126) {
      putc(*str, stdout);
    } else {
      putc('X', stdout);
    }
    str++;
  }
  putc('"', stdout);
}

void pprint_hex_digit(int hex) {
  if (hex < 10) {
    putc(hex + '0', stdout);
  } else {
    switch (hex) {
      case 10: putc('a', stdout); break;
      case 11: putc('b', stdout); break;
      case 12: putc('c', stdout); break;
      case 13: putc('d', stdout); break;
      case 14: putc('e', stdout); break;
      case 15: putc('f', stdout); break;
    }
  }
}

void pprint_hex(void *_str, int len) {
  uint8_t *str = (uint8_t *)_str;

  for (int i = 0; i < len; i++) {
    const int hex1 = *str >> 4;
    const int hex2 = *str & 0x0F;
    pprint_hex_digit(hex1);
    pprint_hex_digit(hex2);
    str++;
  }
}

void json_pprint_(Value *val, bool pretty, int indent, bool no_first_indent) {
  int first_indent =  no_first_indent ? 0 : indent;
  int next_indent = pretty ? indent + 2 : 0;

  if (val->type == TInteger) {
    pindent(first_indent);
    printf("%lld", val->val.integer);

  } else if (val->type == TString) {
    pindent(first_indent);
    pprint_str(val->val.string);

  } else if (val->type == TList) {
    LinkedList* list = val->val.list;
    pindent(first_indent);
    printf("[");
    if (pretty) printf("\n");

    while (list != NULL) {
      json_pprint_(list->val, pretty, next_indent, false);
      if (list->next != NULL) {
        printf(",");
      }
      if (pretty) printf("\n");
      list = list->next;
    }
    pindent(indent);
    printf("]");

  } else if (val->type == TDict) {
    pindent(first_indent);
    printf("{");
    if (pretty) printf("\n");

    LinkedList* list = val->val.list;
    while (list != NULL) {
      if (list->val->type != TKeyVal) {
        fprintf(stderr, "ERROR: Expected Keyval in a dictionary. Got: %d", list->val->type);
        exit(1);
      }
      KeyVal *kv = list->val->val.kv;
      pindent(next_indent);
      pprint_str(kv->key);
      printf(":");
      json_pprint_(kv->val, pretty, next_indent, true);
      if (list->next != NULL) {
        printf(",");
      }
      if (pretty) printf("\n");
      list = list->next;
    }
    pindent(indent);
    printf("}");
  }
}

void json_pprint(Value *val) {
  json_pprint_(val, true, 0, false);
}

void json_print(Value *val) {
  json_pprint_(val, false, 0, false);
}

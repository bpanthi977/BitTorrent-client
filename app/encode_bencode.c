#include "app.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void encode_str(char *str, Cursor *cur) {
  int length = strlen(str);
  int written_chars = sprintf(cur->str, "%d:%s", length, str);
  cur->str += written_chars;
}

void encode_number(int64_t number, Cursor *cur) {
  int written_chars = sprintf(cur->str, "i%llde", number);
  cur->str += written_chars;
}

void encode_list(LinkedList* list, Cursor *cur) {
  *cur->str = 'l';
  cur->str++;
  while (list != NULL) {
    encode_bencode(list->val, cur);
    list = list->next;
  }
  *cur->str = 'e';
  cur->str++;
}

void encode_dict(LinkedList *dict, Cursor *cur) {
  *cur->str = 'd';
  cur->str++;
  while (dict != NULL) {
    if (!assert_type(dict->val, Keyval, "[encode_dict] Expected Keyval entries in dict")) {
      exit(1);
    }
    KeyVal *kv = dict->val->val.kv;
    encode_str(kv->key, cur);
    encode_bencode(kv->val, cur);
    dict = dict->next;
  }
  *cur->str = 'e';
  cur->str++;
}

void encode_bencode(Value *val, Cursor *cur) {
  switch (val->type) {
  case String:
    encode_str(val->val.string, cur);
    break;
  case Integer:
    encode_number(val->val.integer, cur);
    break;
  case List:
    encode_list(val->val.list, cur);
    break;
  case Dict:
    encode_dict(val->val.list, cur);
    break;
  default:
    fprintf(
            stderr,
            "[encode_bencode] Coun't encode input. Value type is unexpected: %d\n",
            val->type);
    exit(1);
  }
}

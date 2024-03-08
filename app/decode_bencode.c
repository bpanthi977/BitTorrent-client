#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "decode_bencode.h"

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

char* decode_string(Cursor *cur) {
  int length = atoi(cur->str);
  const char* colon_index = strchr(cur->str, ':');
  if (colon_index != NULL) {
    const char* start = colon_index + 1;
    char* decoded_str = (char*)malloc(length + 1);
    strncpy(decoded_str, start, length);
    decoded_str[length] = '\0';
    int read = (start - cur->str + length) - 1;
    cur->str += read;
    return decoded_str;
  } else {
    fprintf(stderr, "Invalid encoded value: %s\n", cur->str);
    exit(1);
  }
}

int64_t decode_integer(Cursor *cur) {
  int64_t res = 0;
  bool negative = false;
  while (*(++cur->str) != 'e') {
    if (*cur->str == '-') {
      negative = true;
    } else {
      res = res * 10 + (*cur->str - '0');
    }
  }
  return res * (negative ? -1 : 1);
}

LinkedList* cons(Value *val, LinkedList* list) {
  LinkedList *cell = malloc(sizeof(LinkedList));
  cell->val = val;
  cell->next = list;
  return cell;
}

LinkedList *decode_list(Cursor *cur) {
  LinkedList *head = NULL;
  LinkedList *tail = NULL;

  while (*(++cur->str) != 'e') {
    Value *val = decode_bencode(cur);
    if (tail == NULL) { // first item of list
      head = cons(val, NULL);
      tail = head;
    } else {
      LinkedList *new_cell = cons(val, NULL);
      tail->next = new_cell;
      tail = new_cell;
    }
  }

  return head;
}

Value *read_keyval(Cursor *cur) {
  char *key = decode_string(cur);
  cur->str++;
  Value *val = decode_bencode(cur);
  KeyVal *kv = malloc(sizeof(KeyVal));
  kv->key = key;
  kv->val = val;
  Value *ret = malloc(sizeof(Value));
  ret->type = Keyval;
  ret->val.kv = kv;
  return ret;
}

LinkedList *decode_dict(Cursor *cur) {
  LinkedList *head = NULL;
  LinkedList *tail = NULL;

  while (*(++cur->str) != 'e') {
    Value *kv = read_keyval(cur);
    if (tail == NULL) { // first item of list
      head = cons(kv, NULL);
      tail = head;
    } else {
      LinkedList *new_cell = cons(kv, NULL);
      tail->next = new_cell;
      tail = new_cell;
    }
  }
  return head;
}

Value *decode_bencode(Cursor *cur) {
  Value* ret = malloc(sizeof(Value));
  if (is_digit(cur->str[0])) {
    ret->type = String;
    ret->val.string = decode_string(cur);

  } else if (cur->str[0] == 'i') {
    ret->type = Integer;
    ret->val.integer = decode_integer(cur);

  } else if (cur->str[0] == 'l') {
    ret->type = List;
    ret->val.list = decode_list(cur);

  } else if (cur->str[0] == 'd') {
    ret->type = Dict;
    ret->val.list = decode_dict(cur);

  } else {
    fprintf(stderr, "Only strings are supported at the moment\n");
    exit(1);
  }

  return ret;
}

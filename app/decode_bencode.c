#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "app.h"

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

String *decode_string(Cursor *cur) {
  int length = atoi(cur->str);
  const char* colon_index = strchr(cur->str, ':');
  if (colon_index != NULL) {
    const char* start = colon_index + 1;
    char* decoded_str = (char*)malloc(length + 1);
    for (int i = 0; i < length; i++) {
      *(decoded_str+i) = *(start+i);
    }
    decoded_str[length] = '\0';
    int read = (start - cur->str + length) - 1;
    cur->str += read;

    String *string = malloc(sizeof(String));
    string->length = length;
    string->str = decoded_str;
    return string;
  } else {
    fprintf(stderr, "Couldn't decode string. Invalid value: %s\n", cur->str);
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
  String *key = decode_string(cur);
  cur->str++;
  Value *val = decode_bencode(cur);
  KeyVal *kv = malloc(sizeof(KeyVal));
  kv->key = key;
  kv->val = val;
  Value *ret = malloc(sizeof(Value));
  ret->type = TKeyVal;
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

bool string_equal(String* s1, char* s2) {
  char *ss1 = s1->str;
  int count = 0;
  while (*s2 != '\0') {
    if (count == s1->length)
      return false;

    if (*ss1 != *s2)
      return false;

    ss1++;
    s2++;
    count++;
  }
  return true;
}

Value *gethash(Value *dict, char *key) {
  if (!assert_type(dict, TDict, "[gethash] Expected dict. Got %d\n")) {
    exit(1);
  }

  LinkedList *_entry = dict->val.list;
  while (_entry != NULL) {
    Value *entry = _entry->val;
    if (entry->type != TKeyVal) {
      fprintf(stderr, "[BUG] dictionary entry is not a Keyval. Got %d\n", entry->type);
      exit(1);
    }
    KeyVal *kv = entry->val.kv;
    if (string_equal(kv->key, key)) {
      return kv->val;
    }

    _entry = _entry->next;
  }
  return NULL;
}

Value *decode_bencode(Cursor *cur) {
  Value* ret = malloc(sizeof(Value));
  if (is_digit(cur->str[0])) {
    ret->type = TString;
    ret->val.string = decode_string(cur);

  } else if (cur->str[0] == 'i') {
    ret->type = TInteger;
    ret->val.integer = decode_integer(cur);

  } else if (cur->str[0] == 'l') {
    ret->type = TList;
    ret->val.list = decode_list(cur);

  } else if (cur->str[0] == 'd') {
    ret->type = TDict;
    ret->val.list = decode_dict(cur);

  } else {
    fprintf(stderr, "Only strings are supported at the moment\n");
    exit(1);
  }

  return ret;
}

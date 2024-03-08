#include <stdint.h>

#ifndef DECODE_BENCODE
enum Type {
  String = 1,
  Integer = 2,
  List = 3
};

struct _LinkedList;

typedef union {
  int64_t integer;
  char* string;
  struct _LinkedList* list;
} Thing;

typedef struct {
  enum Type type;
  Thing val;
} Value;

struct _LinkedList {
  Value* val;
  struct _LinkedList *next;
};

typedef struct _LinkedList LinkedList;

typedef struct {
  char* str;
} Cursor;

Value* decode_bencode(Cursor *cur);
#endif

#define DECODE_BENCODE

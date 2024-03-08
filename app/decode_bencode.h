#include <stdint.h>

#ifndef DECODE_BENCODE
enum Type {
  String = 1,
  Integer = 2,
  List = 3,
  Keyval = 4,
  Dict = 5
};

struct _LinkedList;
struct _Value;

typedef struct {
  char *key;
  struct _Value *val;
} KeyVal;

typedef union {
  int64_t integer;
  char* string;
  struct _LinkedList *list;
  KeyVal * kv;
} Thing;

struct _Value {
  enum Type type;
  Thing val;
} _Value;

typedef struct _Value Value;

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

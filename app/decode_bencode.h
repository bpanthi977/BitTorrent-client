#include <stdint.h>

#ifndef DECODE_BENCODE
enum Type {
  String = 1,
  Integer = 2,
};

typedef union {
  int64_t integer;
  char* string;
} Thing;

typedef struct {
  enum Type type;
  Thing val;
} Value;

typedef struct {
  char* str;
} Cursor;

Value* decode_bencode(Cursor *cur);
#endif

#define DECODE_BENCODE

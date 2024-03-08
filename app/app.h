#include <stdint.h>
#include <stdbool.h>

#ifndef APP_INCLUDES
enum Type {
  TString = 1,
  TInteger = 2,
  TList = 3,
  TKeyVal = 4,
  TDict = 5
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

// decode_bencode.c
Value *decode_bencode(Cursor *cur);
Value *gethash(Value *dict, char *key);

// assert_type.c
bool assert_type(Value *val, enum Type type, char *msg);
// file.c
char *read_file_to_string(const char *path);

// json.c
void json_print(Value *val);
void json_pprint(Value *val);
void pprint_str(char* str);
void pprint_hex(uint8_t *str, int len);


// encode_bencode.c
void encode_bencode(Value *val, Cursor *cur);

// sha1.c
void SHA1(char *hash_out, const char *str, uint32_t len);

#endif

#define APP_INCLUDES 1

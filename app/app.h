#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

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
  int length;
  char *str;
} String;

typedef struct {
  String *key;
  struct _Value *val;
} KeyVal;

typedef union {
  int64_t integer;
  String *string;
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
Value *gethash_safe(Value *dict, char *key, enum Type type);


// assert_type.c
bool assert_type(Value *val, enum Type type, char *msg);
// file.c
String *read_file_to_string(const char *path);
Value *read_torrent_file(const char* path);


// json.c
void json_print(Value *val);
void json_pprint(Value *val);
void pprint_str(String* string);
void pprint_hex(void *str, int len);


// encode_bencode.c
void encode_bencode(Value *val, Cursor *cur);

// sha1.c
void SHA1(char *hash_out, const char *str, uint32_t len);

// torrent.c
enum PeerStage {
  S_INIT = 0,
  S_CONNECTED,
  S_HANDSHAKE,
  S_INTERESTED,
  S_UNCHOKE,
  S_DONE,
};

typedef struct Peer {
  int sock;
  enum PeerStage stage;
  char recvbuffer[32 * 1024];
  int recv_bytes;
  int processed_bytes;
  char peer_id[20];
} Peer;

String info_hash(Value* torrent);
Value *fetch_peers(Value *torrent);
void connect_peer(Peer *p, struct sockaddr_in addr);
void do_handshake(Peer *p, String infohash);



// utils.c
void url_encode(String *string, Cursor *cur);
void append_string(String *string, Cursor *cur);
void append_str(char* str, Cursor *cur);
struct sockaddr_in parse_ip_port(char* ip_port);
struct sockaddr_in* parse_peer_addresses(String *peers);


#endif

#define APP_INCLUDES 1

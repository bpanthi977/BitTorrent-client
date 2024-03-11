#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <stdio.h>

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
  S_CONNECTING,
  S_CONNECTED,
  S_WAIT_HANDSHAKE,
  S_HANDSHAKED,
  S_ACTIVE,
  S_ERROR,
  S_DONE,
};

struct _Piece;
typedef struct Peer {
  int peer_idx;
  int sock;
  enum PeerStage stage;
  uint8_t *bitmap;
  uint8_t bitmap_size;

  uint8_t *recvbuffer;
  int buffer_size;
  int recv_bytes;
  int processed_bytes;
  char peer_id[20];

  bool interested;
  bool unchoked;
  // When stage = S_ACTIVE
  struct _Piece *piece;
} Peer;

enum MSG_TYPE {
  MSG_INCOMPLETE = -3,
  MSG_NULL = -2,
  MSG_KEEPALIVE = -1,
  MSG_CHOKE = 0,
  MSG_UNCHOKE = 1,
  MSG_INTERESTED = 2,
  MSG_NOT_INTERESTED = 3,
  MSG_HAVE = 4,
  MSG_BITFIELD = 5,
  MSG_REQUEST = 6,
  MSG_PIECE = 7,
  MSG_CANCEL = 8,
};

typedef struct Message {
  uint32_t length;
  enum MSG_TYPE type;
  void *payload;
} Message;


enum PIECE_STATE {
  PS_INIT = 0,
  PS_DOWNLOADING,
  PS_DOWNLOADED,
  PS_FLUSHED
};

typedef struct _Piece {
  uint32_t piece_idx;
  enum PIECE_STATE state;
  Peer *current_peer;
  String hash;

  // After a piece is activated
  uint64_t piece_length;
  uint32_t block_size;
  uint32_t total_blocks;
  uint32_t last_block_size;

  // After a piece is activate and while is downloading or downloaded
  uint8_t* buffer;
  uint8_t* asked_blocks;
  uint8_t* recieved_blocks;
  uint32_t recieved_count;
  uint32_t outstanding_requests_count;
} Piece;

typedef struct Torrent {
  Piece *pieces;
  int n_pieces;
  int active_pieces;
  int downloaded_pieces;
  FILE *output_file;

  // Init
  String infohash;
  uint64_t piece_length;
  uint64_t file_length;
} Torrent;

uint64_t torrent_total_length(Value *info);
String info_hash(Value* torrent);
bool connect_peer(Peer *p, struct sockaddr_in addr);
int start_communication_loop(Peer *peers, int n_peers, Torrent *o);
Torrent create_torrent(Value *torrent);
void free_torrent(Torrent *o);
Peer create_peer(int peer_idx, int n_pieces, size_t buffer_size);
void free_peer(Peer *p);

// tracker.c
int fetch_peers(Value *torrent, struct sockaddr_in **peers);

// utils.c
void url_encode(String *string, Cursor *cur);
void append_string(String *string, Cursor *cur);
void append_str(char* str, Cursor *cur);
struct sockaddr_in parse_ip_port(char* ip_port);
int parse_peer_addresses(String *peers, struct sockaddr_in **peer_addrs);
uint32_t read_uint32(void *buffer, int offset);
int ceil_division(int divident, int divisor);
void pprint_sockaddr(struct sockaddr_in addr);
bool aref_bit(uint8_t *bitmap, int n_bytes, int index);
void setf_bit(uint8_t *bitmap, int n_bytes, int index, bool value);


#ifndef htonll
#define htonll(x) ((1==htonl(1)) ? (x) : (((uint64_t)htonl((x) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
#endif

#ifndef ntohll
#define ntohll(x) ((1==ntohl(1)) ? (x) : (((uint64_t)ntohl((x) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))
#endif

#endif

#define APP_INCLUDES 1

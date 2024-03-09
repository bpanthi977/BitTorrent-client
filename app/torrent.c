#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>
#include <sys/_types/_null.h>
#include "app.h"

#define DEBUG false

String info_hash(Value* torrent) {
  String hash_string = { 0 };
  Value *info = gethash(torrent, "info");
  if (!assert_type(info, TDict, "Torrent info is not a Dict")) return hash_string;

  String *pieces = gethash_safe(info, "pieces", TString)->val.string;
  char *buffer = malloc(pieces->length + 200);
  Cursor cur = {.str = buffer};
  encode_bencode(info, &cur);

  char *hash = malloc(20);

  SHA1(hash, buffer, cur.str - buffer);
  free(buffer);

  hash_string.length = 20;
  hash_string.str = hash;
  return hash_string;
}

static size_t cb_curl_write_to_string(void *data, size_t size, size_t blocks, void *callback_data) {
  size_t realsize = size * blocks;
  String *str = (String *)callback_data;

  char *ptr = realloc(str->str, str->length + realsize + 1);
  if(!ptr)
    return 0;  /* out of memory! */

  str->str = ptr;
  memcpy(&(str->str[str->length]), data, realsize);
  str->length += realsize;
  str->str[str->length] = 0;

  return realsize;
}

uint64_t torrent_total_length(Value *info) {
  Value *length = gethash(info, "length");
  if (length == NULL) {
    uint64_t total_length = 0;
    LinkedList *files = gethash_safe(info, "files", TList)->val.list;
    while (files != NULL) {
      total_length += gethash_safe(files->val, "length", TInteger)->val.integer;
      files = files->next;
    }
    return total_length;
  } else {
    return length->val.integer;
  }
}

Value *fetch_peers(Value* torrent) {
  // Send HTTP GET request at <torrent.announce> with query params:
  // info_hash = info_hash(torrent)
  // peer_id = char[20]
  // port = 6881
  // uploaded = 0
  // downloaded = 0
  // left = torrent.info.length
  // compact = 1

  CURL *curl = curl_easy_init();
  String *announce = gethash_safe(torrent, "announce", TString)->val.string;
  Value *info = gethash_safe(torrent, "info", TDict);
  uint64_t length = torrent_total_length(info); // max 20 characters
  String hash = info_hash(torrent);

  char *url = malloc(announce->length + 400);
  Cursor cur = {.str = url};
  append_string(announce, &cur);
  append_str("?info_hash=", &cur); url_encode(&hash, &cur);
  append_str("&peer_id=00112233445566778899", &cur);
  append_str("&port=6881", &cur);
  append_str("&uploaded=0", &cur);
  append_str("&downloaded=0", &cur);
  append_str("&left=", &cur);
  cur.str += sprintf(cur.str, "%lld", length);
  append_str("&compact=1", &cur);
  *cur.str = '\0';

  curl_easy_setopt(curl, CURLOPT_URL, url);

  String response = {.str = NULL, .length = 0};
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb_curl_write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

  CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    fprintf(stderr, "Failed to fetch peers from tracker: %s\n", announce->str);
    exit(1);
  }

  curl_easy_cleanup(curl);

  Cursor cur2 = {.str = response.str };
  Value *res = decode_bencode(&cur2);

  free(hash.str);
  free(url);

  return res;
}

void send_handshake(String *infohash, int fd) {
  if (DEBUG) printf("Sending handshake \n");

  // Prepare handshake message
  char buffer[1024];
  Cursor cur = { .str = buffer };
  // 1 byte = 19 (decimal)
  buffer[0] = 19; cur.str++;
  // 19 bytes string BitTorrent protocol
  append_str("BitTorrent protocol", &cur);
  // 8 zero bytes
  memset(cur.str, 0, 8); cur.str += 8;
  // 20 bytes infohash
  append_string(infohash, &cur);
  // 20 bytes peer id
  append_str("00112233445566778899", &cur);

  // Send
  send(fd, buffer, cur.str - buffer, 0);
}

void send_msg(Peer *p, Message msg) {
  if (DEBUG) printf("  Sending message {.length: %d, .type: %d}\n", msg.length, msg.type);

  if (msg.type == MSG_KEEPALIVE) { //
    send(p->sock, "\0\0\0\0", 4, 0);
  } else if (msg.length == 0) {
    uint8_t buffer[5] = {0};
    buffer[3] = 1;
    buffer[4] = msg.type;
    send(p->sock, buffer, 5, 0);
  } else {
    uint8_t buffer[5] = {0};
    *(uint64_t *)buffer = msg.length + 1;
    buffer[4] = msg.type;
    send(p->sock, buffer, 5, 0);
    size_t sent = send(p->sock, msg.payload, msg.length, 0);

    if (sent != msg.length) {
      fprintf(stderr, "[TODO] Couln't send whole message. Tried: %d, Sent: %zu\n",  msg.length, sent);
      exit(1);
    }
  }
}

void connect_peer(Peer *p, struct sockaddr_in addr) {
  if (DEBUG) printf("Connecting to a peer\n");

  if (p->stage >= S_CONNECTED) {
    fprintf(stderr, "[connect_peer] [BUG] Peer is already conncted. Stage: %d\n",  p->stage);
    exit(1);
  }

  int fd = socket(PF_INET, SOCK_STREAM, 0);

  int sin_len = sizeof(struct sockaddr_in);
  if (connect(fd, (struct sockaddr *)&addr, sin_len) == -1) {
    fprintf(stderr, "Error connecting to socket. errno: %d\n",  errno);
    exit(1);
  }

  p->sock = fd;
  p->stage = S_CONNECTED;
}

int peer_recv(Peer *p) {
  size_t bytes = recv(p->sock, p->recvbuffer + p->recv_bytes, p->buffer_size - p->recv_bytes, 0);
  if (DEBUG) printf("  Recieved %zu bytes\n", bytes);

  if (bytes == -1) {
    fprintf(stderr, "Error occured while recv: %d\n", errno);
    exit(1);
  } else if (bytes == 0) {
    fprintf(stderr, "Peer disconnected without sending\n");
    exit(1);
  }
  p->recv_bytes += bytes;
  return bytes;
}

void do_handshake(Peer *p, String infohash) {
  send_handshake(&infohash, p->sock);
  peer_recv(p);
  if (p->recv_bytes < 68) {
    fprintf(stderr, "Recieved input is invalid for handshake\n");
    exit(1);
  }
  if (DEBUG) pprint_hex(p->recvbuffer, p->recv_bytes);

  String peer_id = {.str = p->recvbuffer + 1 + 19 + 8 + 20, .length = 20};
  Cursor cur = { .str = p->peer_id };
  append_string(&peer_id, &cur);
  p->processed_bytes += 68;
  if (DEBUG) printf("Handshake complete\n");
}

Message pop_message(Peer *p) {
  if (DEBUG) printf("  Poping messgage. {.processed = %d, .recieved = %d }\n", p->processed_bytes, p->recv_bytes);

  Message msg = {0};

  if (p->recv_bytes == p->processed_bytes) {
    msg.type = MSG_NULL;
    if (DEBUG) printf("No new mssages\n");

    return msg;
  }

  uint32_t msg_len = read_uint32(p->recvbuffer, p->processed_bytes);
  p->processed_bytes += 4;

  if (msg_len == 0) { // Keepalive msg
    if (DEBUG) printf("    Popped KEEPALIVE messgage\n");

    msg.type = MSG_KEEPALIVE;
    return msg;
  } else {
    msg.length = msg_len - 1; // 1 byte for msg_type
  }

  uint8_t msg_type = *(uint8_t *)(p->recvbuffer + p->processed_bytes);
  p->processed_bytes += 1;

  msg.type = (enum MSG_TYPE)msg_type;

  while (p->recv_bytes < p->processed_bytes + msg.length) {
    // Didn't recieve full message
    if (DEBUG) printf("    Full data of message not recieved. Required: %d, Got: %d\n", msg.length, p->recv_bytes - p->processed_bytes);
    peer_recv(p);
  }

  msg.payload = p->recvbuffer + p->processed_bytes;
  p->processed_bytes += msg.length;

  if (DEBUG) printf("    Popped messgage: {.length=%d, .type=%d}\n", msg.length, msg.type);
  return msg;
}

void ensure_unchoked(Peer *p) {
  if (DEBUG) printf("Ensuring unchoked\n");

  while (p->stage < S_UNCHOKED) {
    // Get a message
    Message msg = pop_message(p);
    if (msg.type == MSG_NULL) {
      peer_recv(p);
      msg = pop_message(p);
    }

    if (msg.type == MSG_UNCHOKE) {
      p->stage = S_UNCHOKED;
    } else {
      if (DEBUG) printf("Sending interested message\n");

      Message interested_msg = {.length = 0, .type = MSG_INTERESTED, .payload = NULL};
      send_msg(p, interested_msg);
    }
  }
}

void shift_recvbuffer(Peer *p) {
  if (p->recv_bytes == p->processed_bytes) {
    p->recv_bytes = 0;
    p->processed_bytes = 0;
  }
  if (DEBUG) printf("[recvbuffer] recv_bytes: %d, processed_bytes: %d, buffer_size: %d\n", p->recv_bytes, p->processed_bytes, p->buffer_size);
}

String download_piece(Value *torrent, Peer *p, int piece_idx) {
  // Compute block sizes
  int MAX_OUTSTANDING_PIECES = 1;
  Value *info = gethash_safe(torrent, "info", TDict);
  uint64_t piece_length = gethash_safe(info, "piece length", TInteger)->val.integer;
  uint64_t file_length = gethash_safe(info, "length", TInteger)->val.integer;
  if (piece_length == 0) {
    fprintf(stdout, "piece length is zero. Invalid.");
    exit(1);
  }
  uint32_t total_pieces = ceil_division(file_length, piece_length);
  if (piece_idx == total_pieces - 1) // last piece may be smaller
    piece_length = file_length - piece_idx * piece_length;

  uint32_t BLOCK_SIZE = 16 * 1024; // 16 kiB
  uint32_t total_blocks = ceil_division(piece_length, BLOCK_SIZE);
  uint32_t final_block_size = piece_length - (total_blocks - 1) * BLOCK_SIZE;
  if (final_block_size == 0) final_block_size = BLOCK_SIZE;

  printf("Downloading piece %d of size %llu in %d blocks \n", piece_idx, piece_length, total_blocks);

  // Allocate memory
  uint8_t *buffer = malloc(piece_length);
  uint8_t *asked = malloc(total_blocks);
  uint8_t *recieved = malloc(total_blocks);
  memset(recieved, 0, total_blocks);
  memset(asked, 0, total_blocks);

  // Get the peer ready
  ensure_unchoked(p);
  printf("Unchoked\n");

  uint32_t recieved_count = 0;
  uint32_t outstanding_requests_count = 0;

  while (recieved_count != total_blocks) {
    // 1. Send request for pieces
    if (outstanding_requests_count < MAX_OUTSTANDING_PIECES) {
      for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
        if (outstanding_requests_count >= MAX_OUTSTANDING_PIECES) break;
        if (asked[block_idx] == 0) {
          printf("Asking for block %d\n", block_idx);
          uint32_t payload[3];
          // index, begin, length
          payload[0] = htonl(piece_idx);
          payload[1] = htonl(block_idx * BLOCK_SIZE);
          payload[2] = htonl(block_idx == total_blocks - 1 ? final_block_size: BLOCK_SIZE);
          Message request = { .length = 3 * 4, .type = MSG_REQUEST, .payload = payload};
          send_msg(p, request);

          outstanding_requests_count++;
          asked[block_idx] = 1;
        }
      }
    }

    // 2. Process response
    peer_recv(p);
    Message msg = pop_message(p);
    while (msg.type != MSG_NULL) {
      if (msg.type == MSG_PIECE) {
        uint32_t index = read_uint32(msg.payload, 0);
        uint32_t begin = read_uint32(msg.payload, 4);
        uint32_t block_size = msg.length - 8;
        uint32_t block_idx = begin / BLOCK_SIZE;

        if (index != piece_idx) {
          printf("Recieved data for unwnated piece idx: %u\n", index);
        } else if (begin % BLOCK_SIZE != 0) {
          printf("Recieved data doesn't align with block size: Got %u\n", begin);
        } else if (block_idx >= total_blocks) {
          printf("Recieved data's block index exceeds total blocks: Got %u, Expected: %u\n", block_idx, total_blocks);
        } else if (block_idx == total_blocks - 1 && block_size != final_block_size) {
          printf("Final block size doesn't match: Got %u, Expected: %u\n", block_size, final_block_size);
        } else if (block_idx != total_blocks -1 && block_size != BLOCK_SIZE) {
          printf("Full block size doesn't match: Got %u, Expected: %u\n", block_size, final_block_size);
        } else if (recieved[block_idx]) {
          if (DEBUG) printf("Block %d already recieved. Ignoring\n", block_idx);
        } else {
          // All good
          printf("Recieved block: %d, size: %d\n", block_idx, block_size);
          fflush(stdout);
          recieved_count++;
          outstanding_requests_count--;
          recieved[block_idx] = 1;
          memcpy(buffer + begin, msg.payload + 8, block_size);
        }
      } else if (msg.type >= 0){
        printf("Got message of type %d. Ignoring.\n", msg.type);
      }
      msg = pop_message(p);
    }
    shift_recvbuffer(p);
  }

  printf("Downloaded piece\n");
  free(asked);
  free(recieved);
  String ret = {.str = (char *)buffer, .length = piece_length};

  // Verify piece hash
  String *hash = gethash_safe(info, "pieces", TString)->val.string;
  char *expected_hash = hash->str + piece_idx * 20;

  char actual_hash[20];
  SHA1(actual_hash, ret.str, ret.length);
  if (memcmp(actual_hash, expected_hash, 20) != 0) {
    printf("[BAD] Got Hash: ");
    pprint_hex(actual_hash, 20);
    printf(", Expected");
    pprint_hex(expected_hash, 20);
    printf("\n");

    fprintf(stderr, "Downloaded hash doesn't match actual hash of piece");
    exit(1);
  } else {
    printf("[OK] Hash: ");
    pprint_hex(actual_hash, 20);
    printf("\n");
  }
  return ret;
}

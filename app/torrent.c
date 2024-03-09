#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>
#include "app.h"

String *info_hash(Value* torrent) {
  Value *info = gethash(torrent, "info");
  if (!assert_type(info, TDict, "Torrent info is not a Dict")) return NULL;

  String *pieces = gethash_safe(info, "pieces", TString)->val.string;
  char *buffer = malloc(pieces->length + 200);
  Cursor cur = {.str = buffer};
  encode_bencode(info, &cur);

  char *hash = malloc(20);

  SHA1(hash, buffer, cur.str - buffer);
  free(buffer);

  String *hash_string = malloc(sizeof(String));
  hash_string->length = 20;
  hash_string->str = hash;
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
  int64_t length = gethash_safe(info, "length", TInteger)->val.integer; // max 20 characters
  String *hash = info_hash(torrent);

  char *url = malloc(announce->length + 400);
  Cursor cur = {.str = url};
  append_string(announce, &cur);
  append_str("?info_hash=", &cur); url_encode(hash, &cur);
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

  free(hash->str);
  free(hash);
  free(url);

  return res;
}

void send_handshake(String* infohash, int fd) {
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

void connect_peer(Peer *p, struct sockaddr_in addr) {
  if (p->stage >= S_CONNECTED) {
    fprintf(stderr, "[connect_peer] [BUG] Peer is already conncted. Stage: %d\n",  p->stage);
    exit(1);
  }

  int fd = socket(PF_INET, SOCK_STREAM, 0);

  if (connect(fd, (struct sockaddr *)&addr, addr.sin_len) == -1) {
    fprintf(stderr, "Error connecting to socket. errno: %d\n",  errno);
    exit(1);
  }

  p->sock = fd;
  p->stage = S_CONNECTED;
}

int peer_recv(Peer *p, int max_bytes) {
  size_t bytes = recv(p->sock, p->recvbuffer + p->recv_bytes, max_bytes, 0);
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

void do_handshake(Peer *p, String* infohash) {
  send_handshake(infohash, p->sock);
  peer_recv(p, 1024);
  if (p->recv_bytes < 68) {
    fprintf(stderr, "Recieved input is invalid for handshake\n");
    exit(1);
  }

  String peer_id = {.str = p->recvbuffer + 1 + 19 + 8 + 20, .length = 20};
  Cursor cur = { .str = p->peer_id };
  append_string(&peer_id, &cur);
  p->processed_bytes += 68;
}


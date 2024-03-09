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

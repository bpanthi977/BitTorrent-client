#include "app.h"
#include <stdlib.h>

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

String info_hash(Value* torrent) {
  String hash_string = { 0 };
  Value *info = gethash(torrent, "info");
  if (!assert_type(info, TDict, "Torrent info is not a Dict")) return hash_string;
  String *pieces = gethash_safe(info, "pieces", TString)->val.string;

  char *buffer = malloc(pieces->length + 1024);
  Cursor cur = {.str = buffer};
  encode_bencode(info, &cur);

  char *hash = malloc(20);

  SHA1(hash, buffer, cur.str - buffer);
  free(buffer);

  hash_string.length = 20;
  hash_string.str = hash;
  return hash_string;
}

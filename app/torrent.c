#include <stdio.h>
#include <stdlib.h>
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

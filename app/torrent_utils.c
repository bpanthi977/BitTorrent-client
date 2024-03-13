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

int count_interesting_pieces(Torrent *t, Peer *p) {
  int count = 0;
  for (int piece_idx = 0; piece_idx < t->n_pieces; piece_idx++) {
    Piece *piece = t->pieces + piece_idx;
    if (piece->state == PS_INIT && aref_bit(p->bitmap, p->bitmap_size, piece_idx) == 1) {
      count++;
    }
  }
  return count;
}

int count_available_pieces(Peer *p, int n_pieces) {
  int count = 0;
  for (int piece_idx = 0; piece_idx < n_pieces; piece_idx++) {
    if (aref_bit(p->bitmap, p->bitmap_size, piece_idx) == 1) {
      count++;
    }
  }
  return count;
}

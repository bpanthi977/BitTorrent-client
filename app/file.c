#include <stdio.h>
#include <stdlib.h>
#include "app.h"

String *read_file_to_string(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Can't open file %s\n", path);
    return NULL;
  }

  // Goto file end and get file length
  fseek(file, 0, SEEK_END);
  int length = ftell(file);

  // Allocate memory
  char *buffer = malloc(length + 1);

  // Read
  if (buffer) {
    fseek(file, 0, SEEK_SET);
    fread(buffer, length, 1, file);
    buffer[length] = '\0';
    fclose(file);

    String *string = malloc(sizeof(String));
    string->length = length;
    string->str = buffer;
    return string;
  } else {
    fprintf(stderr, "File is too big. Can't allocat %d bytes\n", length);
    return NULL;
  }
}

Value *read_torrent_file(const char* path) {
  String *buffer = read_file_to_string(path);
  if (buffer == NULL) {
    return NULL;
  }

  char *buffer_start = buffer->str;
  Cursor cur = { .str = buffer->str };
  Value *torrent = decode_bencode(&cur);
  free(buffer->str);
  free(buffer);

  if (!assert_type(torrent, TDict, "Torrent file is not a valid bencode dictionary")) return NULL;

  return torrent;
}

#include <stdio.h>
#include <stdlib.h>

char* read_file_to_string(const char *path) {
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
    return buffer;
  } else {
    fprintf(stderr, "File is too big. Can't allocat %d bytes\n", length);
    return NULL;
  }
}

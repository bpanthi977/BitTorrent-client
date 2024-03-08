#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

char* decode_string(const char* bencoded_value) {
  int length = atoi(bencoded_value);
  const char* colon_index = strchr(bencoded_value, ':');
  if (colon_index != NULL) {
    const char* start = colon_index + 1;
    char* decoded_str = (char*)malloc(length + 1);
    strncpy(decoded_str, start, length);
    decoded_str[length] = '\0';
    return decoded_str;
  } else {
    fprintf(stderr, "Invalid encoded value: %s\n", bencoded_value);
    exit(1);
  }
}

char* decode_bencode(const char* bencoded_value) {
    if (is_digit(bencoded_value[0])) {
      return decode_string(bencoded_value);
    } else {
        fprintf(stderr, "Only strings are supported at the moment\n");
        exit(1);
    }
}

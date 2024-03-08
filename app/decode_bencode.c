#include <stdint.h>
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

int64_t decode_integer(const char* bencoded_value) {
  int64_t res = 0;
  bool negative = false;
  while (*(++bencoded_value) != 'e') {
    if (*bencoded_value == '-') {
      negative = true;
    } else {
      res = res * 10 + (*bencoded_value - '0');
    }
  }
  return res * (negative ? -1 : 1);
}

void decode_bencode(const char* bencoded_value) {
  if (is_digit(bencoded_value[0])) {
    char *str = decode_string(bencoded_value);
    printf("\"%s\"\n", str);
    free(str);
  } else if (bencoded_value[0] == 'i') {
    int64_t integer = decode_integer(bencoded_value);
    printf("%lld\n", integer);
  } else {
    fprintf(stderr, "Only strings are supported at the moment\n");
    exit(1);
  }
}

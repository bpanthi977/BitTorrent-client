#include <stdlib.h>
#include <string.h>
#include "app.h"

bool uri_unreserved_charp(char c) {
  if ('A' <= c && c <= 'Z') return true;
  if ('a' <= c && c <= 'z') return true;
  if ('0' <= c && c <= '9') return true;
  if (c == '-' || c == '_' || c == '.' || c == '~') return true;
  return false;
}

char hex_char(int hex) {
  if (hex < 10) {
    return hex + '0';
  } else {
    return 'a' + hex - 10;
  }
}

void url_encode(String *string, Cursor *cur) {
  char* str = string->str;
  char* buffer = cur->str;
  for (int i=0; i<string->length; i++) {
    char c = *(str++);
    if (uri_unreserved_charp(c)) {
      *(buffer++) = c;
    } else {
      *(buffer++) = '%';
      *(buffer++) = hex_char(((uint8_t) c) >> 4);
      *(buffer++) = hex_char(((uint8_t) c) & 0x0F);
    }
  }
  cur->str = buffer;
}

void append_string(String *string, Cursor *cur) {
  strcpy(cur->str, string->str);
  cur->str += string->length;
}

void append_str(char* str, Cursor *cur) {
  strcpy(cur->str, str);
  cur->str += strlen(str);
}

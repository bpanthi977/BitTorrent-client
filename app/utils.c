#include <errno.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
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
  memcpy(cur->str, string->str, string->length);
  cur->str += string->length;
}

void append_str(char* str, Cursor *cur) {
  strcpy(cur->str, str);
  cur->str += strlen(str);
}

struct sockaddr_in parse_ip_port(char* ip_port) {
  uint32_t ip = 0;
  uint8_t byte = 0;
  while (*ip_port != '\0') {
    if (*ip_port == '.' || *ip_port == ':') {
      ip = ip * 256 + byte;
      byte = 0;
    } else {
      uint8_t digit = *ip_port - '0';
      byte = byte * 10 + digit;
    }
    if (*ip_port == ':') break;
    ip_port++;
  }

  struct sockaddr_in peer_addr = {0};
  peer_addr.sin_family = AF_INET; // ipv4
  peer_addr.sin_addr.s_addr = htonl(ip);
  peer_addr.sin_port = htons(atoi(ip_port + 1)); // since ip_port now points ':' character
  return peer_addr;
}

uint32_t read_uint32(void *buffer, int offset) {
  uint8_t *bytes = (uint8_t *)(buffer + offset);
  uint32_t result = ntohl(*((uint32_t *)bytes));
  return result;
}

int parse_peer_addresses(String *peers, struct sockaddr_in **peer_addrs) {
  // peers is a string of 48 * n bytes;
  // 48  = 32 bits for IP and 16 bits for port (big-endian)
  int n_peers = (peers->length * 8 / 48);
  struct sockaddr_in *addrs = malloc(sizeof(struct sockaddr_in) * n_peers);

  uint8_t *data = (uint8_t *)peers->str;
  for (int i=0; i<n_peers; i++) {
    struct sockaddr_in *addr = addrs + i;
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = *((uint32_t *)data);
    addr->sin_port = *((uint16_t *)(data + 4));

    data+=6;
  }

  *peer_addrs = addrs;
  return n_peers;
}

void pprint_sockaddr(struct sockaddr_in addr) {
  uint8_t *ip = (uint8_t *) &addr.sin_addr.s_addr;
  uint8_t *port = (uint8_t *) &addr.sin_port;
  printf("%d.%d.%d.%d:%d\n", ip[0], ip[1], ip[2], ip[3], port[0] * 256 + port[1]);
}

int ceil_division(int divident, int divisor) {
  return divident / divisor + (divident % divisor == 0 ? 0 : 1);
}

bool aref_bit(uint8_t *bitmap, int n_bytes, int index) {
  int byte_index = index / 8;
  int bit_index = index % 8;
  if (n_bytes <= byte_index) {
    fprintf(stderr, "[BUG] can't dereference %d-th bit in an bit array of %d bytes\n", index, n_bytes);
    return false;
  }
  uint8_t byte = *(bitmap + byte_index);
  return (byte >> (7 - bit_index)) & 0x1;
}

void setf_bit(uint8_t *bitmap, int n_bytes, int index, bool value) {
  int byte_index = index / 8;
  int bit_index = index % 8;
  if (n_bytes <= byte_index) {
    fprintf(stderr, "[BUG] can't dereference %d-th bit in an bit array of %d bytes\n", index, n_bytes);
  }
  uint8_t byte = *(bitmap + byte_index);
  uint8_t new_byte;
  if (value == 1) {
    new_byte = byte | (1 << (7 - bit_index));
  } else {
    new_byte = byte & (0xFF - (1 << (7 - bit_index)));
  }
  *(bitmap + byte_index) = new_byte;
}

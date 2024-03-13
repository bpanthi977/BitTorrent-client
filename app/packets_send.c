#include "app.h"
#include <stdlib.h>
#include <string.h>

#define DEBUG false
#define DEBUG_MSG_BYTES false
#define DEBUG_HANDSHAKE false

void send_handshake(String *infohash, int fd) {
  if (DEBUG_HANDSHAKE) printf("Sending handshake at %d \n\t", fd);

  // Prepare handshake message
  char buffer[1024];
  Cursor cur = { .str = buffer };
  // 1 byte = 19 (decimal)
  buffer[0] = 19; cur.str++;
  // 19 bytes string BitTorrent protocol
  append_str("BitTorrent protocol", &cur);
  // 8 zero bytes
  memset(cur.str, 0, 8); cur.str += 8;
  // 20 bytes infohash
  append_string(infohash, &cur);
  // 20 bytes peer id
  append_str("BPTtorrent0000000000", &cur);

  // Send
  if (DEBUG_HANDSHAKE) {
    pprint_hex(buffer, cur.str - buffer);
    printf("\n");
  }
  send(fd, buffer, cur.str - buffer, 0);
}

void send_msg(Peer *p, Message msg) {
  if (DEBUG_MSG_BYTES) {
    printf("  Sending message {.length: %u, .type: %d}\n\t", msg.length, msg.type);
    pprint_hex(msg.payload, msg.length);
    printf("\n");
  }

  p->last_msg_time = NOW;
  if (msg.type == MSG_KEEPALIVE) { //
    send(p->sock, "\0\0\0\0", 4, 0);
  } else if (msg.length == 0) {
    uint8_t buffer[5] = {0};
    buffer[3] = 1;
    buffer[4] = msg.type;
    send(p->sock, buffer, 5, 0);
  } else {
    uint8_t buffer[5 + msg.length];
    *(uint32_t *)buffer = htonl(msg.length + 1);
    buffer[4] = msg.type;
    memcpy(buffer + 5, msg.payload, msg.length);
    size_t sent = send(p->sock, buffer, msg.length + 5, 0);
    if (sent != msg.length + 5) {
      fprintf(stderr, "[TODO] Couln't send whole message. Tried: %u, Sent: %zu\n",  msg.length + 5, sent);
      exit(1);
    }
  }
}

void send_bitfield(Torrent *t, Peer *p) {
  int bytes = ceil_division(t->n_pieces, 8);
  uint8_t bits[bytes];
  memset(bits, 0, bytes);
  for (int i=0; i<t->n_pieces; i++) {
    int have = t->pieces[i].state == PS_DOWNLOADED;
    setf_bit(bits, bytes, i, have);
  }
  Message msg = { .type = MSG_BITFIELD, .length = bytes, .payload = bits};
  send_msg(p, msg);
}

void send_interested(Peer *peer) {
  if (DEBUG) printf("Sending INTERESTED\n");
  Message interested_msg = {.length = 0, .type = MSG_INTERESTED, .payload = NULL};
  send_msg(peer, interested_msg);
}

void send_unchoke(Peer *peer) {
  if (DEBUG) printf("Sending UNCHOKE\n");
  Message unchoke_mssg = {.length = 0, .type = MSG_UNCHOKE, .payload = NULL};
  send_msg(peer, unchoke_mssg);
}

void send_keepalive(Peer *peer) {
  if (DEBUG) printf("Sending KEEPALIVE\n");
  Message keepalive = { .type = MSG_KEEPALIVE, .length = 0, .payload = NULL};
  send_msg(peer, keepalive);
}

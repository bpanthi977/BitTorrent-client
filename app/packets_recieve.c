#include "app.h"
#include <errno.h>
#include <string.h>
#define DEBUG false
#define DEBUG_MSG false
#define DEBUG_MSG_BYTES false

int peer_recv(Peer *p) {
  size_t bytes = recv(p->sock, p->recvbuffer + p->recv_bytes, p->buffer_size - p->recv_bytes, 0);
  if (bytes == -1) {
    fprintf(stderr, "Error occured while recv: %d %s\n", errno, strerror(errno));
    p->stage = S_ERROR;
    return 0;
  } else if (bytes == 0) {
    fprintf(stderr, "Peer disconnected without sending\n");
    p->stage = S_ERROR;
    return 0;
  }

  if (DEBUG_MSG_BYTES) printf("  Recieved %zu bytes from %d at %d\n", bytes, p->peer_idx, p->sock);
  p->recv_bytes += bytes;
  p->last_msg_time = NOW;
  if (p->piece != NULL) {
    p->piece->speed_bytes_recieved += bytes;
  }
  return bytes;
}

Message pop_message(Peer *p) {
  if (DEBUG_MSG) printf("  Poping messgage. {.processed = %d, .recieved = %d }\n", p->processed_bytes, p->recv_bytes);

  Message msg = {0};
  if (p->recv_bytes == p->processed_bytes) {
    msg.type = MSG_NULL;
    if (DEBUG_MSG) printf("No new mssages\n");

    return msg;
  }

  uint8_t *buffer = p->recvbuffer + p->processed_bytes;
  uint32_t msg_len = read_uint32(buffer, 0);

  if (msg_len == 0) { // Keepalive msg
    if (DEBUG_MSG) printf("    Popped KEEPALIVE messgage\n");

    msg.type = MSG_KEEPALIVE;
    p->processed_bytes += 4;
    return msg;

  } else if (p->recv_bytes - p->processed_bytes < msg_len + 4) {
    if (DEBUG_MSG) printf("    Full data of message not recieved. Required: %u, Got: %d\n", msg_len + 4, p->recv_bytes - p->processed_bytes);
    msg.type = MSG_INCOMPLETE;
    msg.length = 0;
    msg.payload = NULL;
    return msg;

  } else {
    uint8_t msg_type = *(buffer + 4);
    msg.type = (enum MSG_TYPE)msg_type;
    msg.length = msg_len - 1; // don't count 1 byte of msg_type
    msg.payload = buffer + 5;

    if (DEBUG_MSG) printf("\tPopped messgage: {.length=%u, .type=%d}\n", msg.length, msg.type);
    if (DEBUG_MSG_BYTES) {
      printf("\t\t"); pprint_hex(p->recvbuffer + p->processed_bytes, msg_len + 4); printf("\n");
    }

    p->processed_bytes += msg_len + 4;
    return msg;
  }
}

void shift_recvbuffer(Peer *p) {
  if (p->recv_bytes == p->processed_bytes) {
    p->recv_bytes = 0;
    p->processed_bytes = 0;
  } else if (p->recv_bytes > 3 * p->buffer_size / 2) {
    if (DEBUG) {
      printf("Shifting recvbuffer of peer: %d\n", p->peer_idx);
      printf("[recvbuffer] recv_bytes: %d, processed_bytes: %d, buffer_size: %d\n", p->recv_bytes, p->processed_bytes, p->buffer_size);
    }

    if (p->processed_bytes > 0) {
      int bytes_to_shift = p->recv_bytes - p->processed_bytes;
      uint8_t *from = p->recvbuffer + p->processed_bytes;
      uint8_t *to = p->recvbuffer;
      p->recv_bytes -= bytes_to_shift;
      p->processed_bytes -= bytes_to_shift;
      while (bytes_to_shift > 0) {
        *(to++) = *(from++);
        bytes_to_shift--;
      }
      if (DEBUG) {
        printf("shifted to recv_bytes: %d, processed_bytes: %d, buffer_size: %d\n", p->recv_bytes, p->processed_bytes, p->buffer_size);
      }
    }
  } else {
    if (DEBUG) printf("[recvbuffer] recv_bytes: %d, processed_bytes: %d, buffer_size: %d\n", p->recv_bytes, p->processed_bytes, p->buffer_size);
  }
}

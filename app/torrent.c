#include <errno.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "app.h"
#include "packets.h"

#define DEBUG false
#define DEBUG_MSGTYPE false

time_t NOW = 0;
float NOW_MS = 0.0f;

bool connect_peer(Peer *p, struct sockaddr_in addr) {
  if (DEBUG) {
    printf("Connecting to a peer: %d ", p->peer_idx);
    pprint_sockaddr(addr);
    printf("\n");
  }

  if (p->stage >= S_CONNECTED) {
    fprintf(stderr, "[connect_peer] [BUG] Peer is already conncted. Stage: %d\n",  p->stage);
    exit(1);
  }

  int fd = socket(PF_INET, SOCK_STREAM, 0);

  fcntl(fd, F_SETFL, O_NONBLOCK);
  int sin_len = sizeof(struct sockaddr_in);
  int ret = connect(fd, (struct sockaddr *)&addr, sin_len);
  p->sock = fd;
  if (ret == 0) {
    if (DEBUG) printf("Connected to %d\n", p->peer_idx);
    p->stage = S_CONNECTED;
  } else if (ret == -1 && errno == EINPROGRESS) {
    p->stage = S_CONNECTING;
  } else {
    fprintf(stderr, "Error connecting to socket. errno: %d %s\n",  errno, strerror(errno));
    return false;
  }
  return true;
}

bool process_handshake(Peer *p) {
  if (p->recv_bytes < 68) {
    fprintf(stderr, "Recieved input is invalid for handshake\n");
    p->stage = S_ERROR;
    close(p->sock);
    return false;
  }

  memcpy(p->peer_id, p->recvbuffer + 1 + 19 + 8 + 20, 20);
  p->processed_bytes += 68;
  if (DEBUG) printf("Handshake complete\n");
  p->stage = S_HANDSHAKED;
  return true;
}

// Initialize piece for download
bool initalize_piece_for_download(Torrent *t, Peer *p, Piece *piece) {

  // compute block sizes
  int piece_idx = piece->piece_idx;
  uint64_t piece_length = t->piece_length;
  uint64_t file_length = t->file_length;
  if (piece_length == 0) {
    fprintf(stdout, "piece length is zero. Invalid.");
    exit(1);
  }
  uint32_t total_pieces = ceil_division(file_length, piece_length);
  if (piece_idx == total_pieces - 1) // last piece may be smaller
    piece_length = file_length - piece_idx * piece_length;

  uint32_t BLOCK_SIZE = 16 * 1024; // 16 kiB
  uint32_t total_blocks = ceil_division(piece_length, BLOCK_SIZE);
  uint32_t last_block_size = piece_length - (total_blocks - 1) * BLOCK_SIZE;
  if (last_block_size == 0) last_block_size = BLOCK_SIZE;

  piece->piece_length = piece_length;
  piece->block_size = BLOCK_SIZE;
  piece->total_blocks = total_blocks;
  piece->last_block_size = last_block_size;

  // Allocate memory
  uint8_t *buffer = malloc(piece_length);
  uint8_t *asked_blocks = malloc(total_blocks);
  uint8_t *recieved_blocks = malloc(total_blocks);
  memset(recieved_blocks, 0, total_blocks);
  memset(asked_blocks, 0, total_blocks);

  piece->buffer = buffer;
  piece->asked_blocks = asked_blocks;
  piece->recieved_blocks = recieved_blocks;
  piece->recieved_count = 0;
  piece->outstanding_requests_count = 0;

  // Reset stats for speed
  piece->speed_bytes_recieved = 0;
  piece->speed_timestamp_ms = NOW_MS;
  piece->speed_ma = -1;

  printf("Downloading piece %d of size %llu in %d blocks \n", piece_idx, piece_length, total_blocks);
  return true;
}

bool verify_piece(Piece *piece) {
  char actual_hash[20];
  SHA1(actual_hash, (void *)piece->buffer, piece->piece_length);

  if (memcmp(actual_hash, piece->hash.str, 20) != 0) {
    printf("[BAD] Got Hash for piece %d: ", piece->piece_idx);
    pprint_hex(actual_hash, 20);
    printf(", Expected");
    pprint_hex(piece->hash.str, 20);
    printf("\n");
    return false;
  } else {
    printf("[OK] Hash for piece %d: ", piece->piece_idx);
    pprint_hex(actual_hash, 20);
    printf("\n");
    return true;
  }
}

void cleanup_piece_after_download(Piece *piece) {
  free(piece->asked_blocks);
  free(piece->recieved_blocks);
  free(piece->buffer);
}

void request_piece_blocks(Peer *peer, Piece *piece) {
  int MAX_OUTSTANDING_BLOCKS = 10;

  if (piece->outstanding_requests_count >= MAX_OUTSTANDING_BLOCKS) return;
  for (uint32_t block_idx = 0 ; block_idx < piece->total_blocks; block_idx++) {
    if (piece->outstanding_requests_count >= MAX_OUTSTANDING_BLOCKS) break;
    bool unasked = piece->asked_blocks[block_idx] == 0;
    bool downloaded = piece->recieved_blocks[block_idx] == 1;
    if (unasked && !downloaded) {
      if (DEBUG) printf("Asking for block %d\n", block_idx);
      uint32_t payload[3];
      // index, begin, length
      payload[0] = htonl(piece->piece_idx);
      payload[1] = htonl(block_idx * piece->block_size);
      payload[2] = htonl(block_idx == piece->total_blocks - 1 ? piece->last_block_size: piece->block_size);
      Message request = { .length = 3 * 4, .type = MSG_REQUEST, .payload = payload};
      uint8_t secs = time(NULL) & 0xFF;
      if (secs == 0) secs += 1;
      if (DEBUG) printf("Sending REQUEST: Peer %d, Piece_%d[%d] Time: %d\n", peer->peer_idx, piece->piece_idx, block_idx, secs);
      send_msg(peer, request);

      piece->outstanding_requests_count++;
      piece->asked_blocks[block_idx] = secs;
    }
  }
}

void clear_outstanding_requests(Piece *piece) {
  if (piece->state != PS_DOWNLOADING) {
    fprintf(stderr, "[BUG] clear_oustanding_requests called for piece at state: %d\n", piece->state);
    return;
  }

  uint8_t secs = time(NULL) & 0xFF;
  if (secs == 0) secs += 1;
  int count = 0;
  for (int block_idx = 0; block_idx < piece->total_blocks; block_idx++) {
    uint8_t asked = piece->asked_blocks[block_idx];
    if (asked != 0 && asked < secs - 4) {
      piece->asked_blocks[block_idx] = 0;
      piece->outstanding_requests_count--;
      count++;
    }
  }
  if (DEBUG) printf("Cleared %d reqs with time older than %d - 4\n", count, secs);
}

Piece *save_piece_block(Torrent *t, Message msg) {
  // while (recieved_count != total_blocks)

  if (msg.type != MSG_PIECE) {
    fprintf(stderr, "[BUG] Got message of type %d. Ignoring.\n", msg.type);
    exit(1);
  }

  uint32_t index = read_uint32(msg.payload, 0);
  if (index >= t->n_pieces) {
    fprintf(stderr, "Invalid piece_idx (%d) in PIECE response. n_piece = %d\n", index, t->n_pieces);
    return NULL;
  };

  Piece *piece = t->pieces + index;
  uint32_t begin = read_uint32(msg.payload, 4);
  uint32_t block_size = msg.length - 8;
  uint32_t block_idx = begin / piece->block_size;
  if (DEBUG_MSGTYPE) printf(" Got PIECE %d[%d] .size %d\n", index, block_idx, block_size);

  if (piece->state != PS_DOWNLOADING) {
    printf("Recieved data for unwnated piece idx: %u. piece state: %d\n", index, piece->state);
  } else if (index != piece->piece_idx) {
    fprintf(stderr, "[BUG] index %u and piece_idx %u doesn't match\n", index, piece->piece_idx);
    exit(1);
  } else if (begin % piece->block_size != 0) {
    printf("Recieved data doesn't align with block size: Got %u\n", begin);
  } else if (block_idx >= piece->total_blocks) {
    printf("Recieved data's block index exceeds total blocks: Got %u, Expected: %u\n", block_idx, piece->total_blocks);
  } else if (block_idx == piece->total_blocks - 1 && block_size != piece->last_block_size) {
    printf("Final block size doesn't match: Got %u, Expected: %u\n", block_size, piece->last_block_size);
  } else if (block_idx != piece->total_blocks -1 && block_size != piece->block_size) {
    printf("Full block size doesn't match: Got %u, Expected: %u\n", block_size, piece->block_size);
  } else if (piece->recieved_blocks[block_idx]) {
    if (DEBUG) printf("Block %d already recieved. Ignoring\n", block_idx);
  } else {
    // All good
    if (DEBUG) printf("Recieved block: %d, size: %d\n", block_idx, block_size);
    piece->recieved_count++;
    piece->outstanding_requests_count--;
    piece->recieved_blocks[block_idx] = 1;
    memcpy(piece->buffer + begin, msg.payload + 8, block_size);
  }
  return piece;
}

bool process_peer_connect(Peer *p) {
  int peer_idx = p->peer_idx;

  if (p->stage == S_CONNECTING) {
    struct sockaddr addr;
    socklen_t len = sizeof(struct sockaddr);
    int ret = getpeername(p->sock, &addr, &len);
    if (ret == 0) { // Connected
      printf("Connected to peer %d\n", peer_idx);
      p->stage = S_CONNECTED;
      return true;
    }

    if (ret == ENOTCONN) { // couldn't connect
      fprintf(stderr, "couldn't connect to peer %d\n", peer_idx);
    } else {
      fprintf(stderr, "[BUG?] peer ready to write but getpeername fails %d\n", ret);
      p->stage = S_ERROR;
    }
    // try reading a byte, just to get errno
    recv(p->sock, &addr, 1, 0);
    fprintf(stderr, "connection error: %d %s\n", errno, strerror(errno));
    return false;
  } else {
    fprintf(stderr, "[BUG?] process_peer_connect at stage: %d\n", p->stage);
    return false;
  }
}

Peer create_peer(int peer_idx, int n_pieces, size_t buffer_size) {
  Peer p = {0};
  p.peer_idx = peer_idx;
  p.bitmap_size = ceil_division(n_pieces, 8);
  p.bitmap = malloc(p.bitmap_size);
  memset(p.bitmap, 0, p.bitmap_size);
  p.buffer_size = buffer_size;
  p.recvbuffer = malloc(p.buffer_size);
  return p;
}

void free_peer(Peer *p) {
  free(p->bitmap);
  free(p->recvbuffer);
}

Piece *select_piece_for_download(Torrent *t, Peer *peer) {

  if (t->downloaded_pieces + t->active_pieces >= t->n_pieces) {
    printf("[.select_piece] All pieces are active or downloaded\n");
    return NULL;
  }

  if (DEBUG) {
    printf("Selecting piece for download\n");
    printf("peer bitmap: "); pprint_hex(peer->bitmap, peer->bitmap_size);printf("\n");
  }
  // find an inactive piece
  for (int i=0; i<t->n_pieces; i++) {
    Piece p = t->pieces[i];
    if (p.state == PS_INIT && aref_bit(peer->bitmap, peer->bitmap_size, p.piece_idx) == 1) {
      return t->pieces + i;
    }
  }

  printf("downloaded_pieces(%d) + active_pieces(%d) is less than "
         "n_pieces (%d). but couldn't select a piece\n",
         t->downloaded_pieces, t->active_pieces, t->n_pieces);

  return NULL;
}

Torrent create_torrent(Value *torrent) {
  String infohash = info_hash(torrent);
  Value *info = gethash_safe(torrent, "info", TDict);
  String *hash = gethash_safe(info, "pieces", TString)->val.string;
  uint64_t piece_length = gethash_safe(info, "piece length", TInteger)->val.integer;
  uint64_t file_length = torrent_total_length(info);

  int n_pieces = hash->length / 20;

  Piece *piece, *piece0 = malloc(sizeof(Piece) * n_pieces);
  memset(piece0, 0, sizeof(Piece) * n_pieces);

  piece = piece0;
  for (int piece_idx=0; piece_idx<n_pieces; piece_idx++) {
    piece->piece_idx = piece_idx;
    piece->hash.str = hash->str + piece_idx * 20;
    piece->hash.length = 20;

    piece++;
  }

  Torrent o = {.pieces = piece0,
               .n_pieces = n_pieces,
               .active_pieces = 0,
               .downloaded_pieces = 0,
               .infohash = infohash,
               .piece_length = piece_length,
               .file_length = file_length};

  return o;
}

void free_torrent(Torrent *t) {
  free(t->infohash.str);
  free(t->pieces);
}

Piece *activate_peer_and_piece(Torrent *t, Peer *peer) {
  if (!peer->unchoked) {
    fprintf(stderr, "[BUG?] Activating chocked peer\n");
    return NULL;
  }
  if (peer->stage == S_ACTIVE) {
    fprintf(stderr, "[BUG?] Activating already active peer\n");
    return NULL;
  }

  Piece *piece = select_piece_for_download(t, peer);
  if (piece != NULL) {
    initalize_piece_for_download(t, peer, piece);
    piece->state = PS_DOWNLOADING;
    peer->stage = S_ACTIVE;

    piece->current_peer = peer;
    peer->piece = piece;

    t->active_pieces++;

    return  piece;
  }
  return NULL;
}

Peer *select_peer_and_piece(Peer *peers, int n_peers, Torrent *t) {
  if (t->downloaded_pieces + t->active_pieces >= t->n_pieces) return NULL;

  Peer *best_peer = NULL;
  int best_priority = 0;
  for (int i=0; i<n_peers; i++) {
    Peer *p = peers + i;
    if (p->unchoked && p->stage == S_HANDSHAKED) {
      int count = count_interesting_pieces(t, p);
      if (count > 0) {
        if (!best_peer) {
          best_peer = p;
          best_priority = p->priority;
        } else {
          if (p->priority > best_priority) {
            best_peer = p;
            best_priority = p->priority;
          }
        }
      }
    }
  }

  return best_peer;
}

Peer *select_peer_for_download(Peer *peers, int n_peers, int piece_idx) {
  Peer *best_peer = NULL;
  int best_peer_interesting_pieces = 0;
  int best_priority = 0;
  for (int i=0; i<n_peers; i++) {
    Peer *p = peers + i;
    if (p->unchoked && p->stage == S_HANDSHAKED) {
      int has_piece =  aref_bit(p->bitmap, p->bitmap_size, piece_idx);
      if (has_piece) {
        if (!best_peer) {
          best_peer = p;
          best_priority = p->priority;
        } else {
          if (p->priority >= best_priority) {
            best_peer = p;
            best_priority = p->priority;
          }
        }
      }
    }
  }

  return best_peer;
}

// Does cleanup on piece only if its in DOWNLOADING or FLUSHED stage
void deactivate_peer_and_piece(Torrent *t, Peer *peer) {
  if (!(peer->stage == S_ACTIVE || peer->stage == S_ERROR)) {
    fprintf(stderr, "[BUG] deactivate_peer called on peer %d at stage: %d\n", peer->peer_idx, peer->stage);
    exit(1);
  }
  if (peer->piece == NULL) {
    fprintf(stderr, "[BUG] peer %d is at stage %d but peer.piece is null\n", peer->peer_idx, peer->stage);
    exit(1);
  }

  // Cleanup state in piece
  Piece *piece = peer->piece;
  piece->current_peer = NULL;
  t->active_pieces--;
  if (piece->state == PS_DOWNLOADING) {
    piece->state = PS_INIT;
    cleanup_piece_after_download(piece);
  }

  // Cleanup state in peer
  if (peer->stage != S_ERROR)
    peer->stage = S_HANDSHAKED;
  peer->piece = NULL;
}

void process_peer_read(Peer *peer, Torrent *t) {
  if (DEBUG) printf("[msg from %d]\n", peer->peer_idx);
  fflush(stdout);
  peer_recv(peer);

  // Complete handshake the first time the peer sends data
  if (peer->stage == S_WAIT_HANDSHAKE) {
    if (process_handshake(peer)) {
      send_bitfield(t, peer);
      send_unchoke(peer);
    }
  }

  Message msg = pop_message(peer);
  while (msg.type >= 0) {
    if (peer->stage != S_HANDSHAKED && peer->stage != S_ACTIVE) {
      printf("Ignoring message at stage: %d\n", peer->stage);
    } else if (msg.type == MSG_CHOKE) {
      if (DEBUG_MSGTYPE) printf(" Got CHOKE\n");
      peer->unchoked = false;
      if (peer->stage == S_ACTIVE) {
        clear_outstanding_requests(peer->piece);
      }
    } else if (msg.type == MSG_UNCHOKE) {
      if (DEBUG_MSGTYPE) printf(" Got UNCHOKE\n");

      peer->unchoked = true;
    } else if (msg.type == MSG_INTERESTED) {
      if (DEBUG_MSGTYPE) printf(" Got INTERESTED\n");

      peer->interested = true;
    } else if (msg.type == MSG_NOT_INTERESTED) {
      if (DEBUG_MSGTYPE) printf(" Got NOT_INTERESTED\n");

      peer->interested = false;
    } else if (msg.type == MSG_HAVE) {
      uint32_t piece_idx = read_uint32(msg.payload, 0);

      if (DEBUG_MSGTYPE) printf(" Got HAVE %d\n", piece_idx);

      if (piece_idx >= t->n_pieces) {
        fprintf(stderr, "Invalid piece_idx (%d) in HAVE response. n_piece = %d\n", piece_idx, t->n_pieces);
      } else {
        setf_bit(peer->bitmap, peer->bitmap_size, piece_idx, 1);
        if (DEBUG) {
          printf("New bitmap: ");
          pprint_hex(peer->bitmap, peer->bitmap_size);
          printf("\n");
        }
        enum PIECE_STATE piece_state = t->pieces[piece_idx].state;
        if (piece_state == PS_INIT) {
          send_interested(peer);
        }
      }

      /* if (peer->piece != NULL) { */
      /*   clear_outstanding_requests(peer->piece); */
      /*   request_piece_blocks(peer, peer->piece); */
      /* } */


    } else if (msg.type == MSG_BITFIELD) {
      if (DEBUG_MSGTYPE) printf(" Got BITFIELD\n");

      bool has_interesting_piece = false;
      for (int i=0; i < t->n_pieces; i++) {
        Piece *piece = t->pieces + i;
        if (aref_bit(msg.payload, msg.length, piece->piece_idx)) {
          setf_bit(peer->bitmap, peer->bitmap_size, piece->piece_idx, 1);
          if (piece->state < PS_DOWNLOADED) has_interesting_piece = true;
        }
      }

      if (has_interesting_piece) send_interested(peer);
    } else if (msg.type == MSG_REQUEST) {
      if (DEBUG_MSGTYPE) printf(" Got REQUEST\n");

      // TODO: Ignore request messages
    } else if (msg.type == MSG_CANCEL) {
      if (DEBUG_MSGTYPE) printf(" Got CANCLE\n");

      // TODO: Ignore cancle messages
    } else if (msg.type == MSG_PIECE) {
      // store
      if (peer->stage != S_ACTIVE) {
        printf("Ignoring piece from a peer who is not active\n");
      } else {
        Piece *piece = save_piece_block(t, msg);
        if (piece == NULL) {
          // do nothing
        } else if (piece->recieved_count != piece->total_blocks) {
          // request_piece_blocks(peer, piece);
        } else {
          printf("Download complete for piece %d\n", piece->piece_idx);
          if (verify_piece(piece)) {
            piece->state = PS_DOWNLOADED;
            t->downloaded_pieces++;

            if (t->output_file != NULL) {
              // Save to disk
              fseek(t->output_file, piece->piece_idx * t->piece_length, SEEK_SET);
              int success = fwrite(piece->buffer, piece->piece_length, 1, t->output_file);
              if (success == 1) {
                printf("Piece %d saved to disk\n", piece->piece_idx);
                cleanup_piece_after_download(piece);
                piece->state = PS_FLUSHED;
              }
            }
            // and send a HAVE to all active peers
          }
          deactivate_peer_and_piece(t, peer);
        }
      }
    } else {
      fprintf(stderr, "Unknown message type %d\n", msg.type);
    }

    msg = pop_message(peer);
  }

  if (peer->stage == S_ERROR && peer->piece != NULL) {
    deactivate_peer_and_piece(t, peer);
  }

  bool download_complete = t->downloaded_pieces >= t->n_pieces;
  if (download_complete) {
    // Do nothing
  } else if (peer->stage == S_ACTIVE && peer->unchoked) {
    request_piece_blocks(peer, peer->piece);
  } else if (peer->stage != S_HANDSHAKED) {
    // ignore
  } else if (!peer->unchoked) {
    //send_interested(peer);
  } else {
    Piece *piece = activate_peer_and_piece(t, peer);
    if (piece != NULL) {
      request_piece_blocks(peer, piece);
    }
  }
}

time_t prev_time = 0;

void send_keepalives_and_disconnects(Peer *peers, int n_peers, Torrent *t) {
  time_t diff = NOW - prev_time;

  if (diff > 2) {
    for (int i = 0; i < n_peers; i++) {
      Peer *p = peers + i;
      // Check for peers that don't answer outstanding piece requests, or are slow
      Peer *best_alternative_peer = p->piece == NULL ? NULL : select_peer_for_download(peers, n_peers, p->piece->piece_idx);
      if (best_alternative_peer != NULL && best_alternative_peer != p) {
        bool deactivated = false;

        if (NOW - p->last_msg_time > 10) {
          printf("Deactivating peer (%d) and piece (%d). Because no block "
                 "recieved in last 10 seconds\n",
                 p->peer_idx, p->piece->piece_idx);
          deactivate_peer_and_piece(t, p);
          p->priority-=10;
          deactivated = true;
        } else if (p->piece->speed_ma != -1 && p->piece->speed_ma < t->total_ma_speed_download * 0.1) {
          // Check for peers that are too slow
          printf("Deactivating peer (%d) and piece (%d). Because it is very "
                 "slow\n",
                 p->peer_idx, p->piece->piece_idx);
          deactivate_peer_and_piece(t, p);
          p->priority--;
          deactivated = true;
        }

        if (deactivated) {
          // Find another peer
          Peer *best_peer = select_peer_and_piece(peers, n_peers, t);
          if (best_peer != NULL) {
            Piece *piece = activate_peer_and_piece(t, best_peer);
            if (piece != NULL) request_piece_blocks(best_peer, piece);
          }
        }
      }

      // Every 30 seconds check if anyone needs a keepalive to be sent
      if ((p->stage == S_HANDSHAKED || p->stage == S_ACTIVE) &&
          NOW - p->last_msg_time > 30) {
        send_keepalive(p);
      }

    }
  }

  prev_time = NOW;
}


int start_communication_loop(Peer *peers, int n_peers, Torrent *t) {
  time_t baseline_secs = time(NULL);

  int nfds = 0;
  for (int idx = 0; idx < n_peers; idx++) {
    if (peers[idx].sock + 1 > nfds)
      nfds = peers[idx].sock + 1;
  }

  // Main loop
  fd_set readfds, writefds;
  struct timeval timeout = {};
  int done = false;
  printf("Starting communication loop with nfds: %d, n_peers: %d\n", nfds, n_peers);
  while (!done) {
    // Initialize fd sets
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    int n_active = 0;
    for (int i=0; i<n_peers; i++) {
      if (peers[i].stage != S_DONE && peers[i].stage != S_ERROR) {
        n_active++;
        int fd = peers[i].sock;
        FD_SET(fd, &readfds);
        if (peers[i].stage == S_CONNECTING)
          FD_SET(fd, &writefds);
      }
    }
    if (n_active == 0) break;
    // Wait for event
    if (DEBUG) printf("[[Waiting for events]] ");

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    int ready_count = select(nfds, &readfds, &writefds, NULL, &timeout);
    if (ready_count == -1) {
      fprintf(stderr, "select failed with error: %d %s\n", errno, strerror(errno));
      return 1;
    } else {
      if (DEBUG) printf(" got %d\n", ready_count);
    }

    struct timeval now;
    gettimeofday(&now, NULL);
    NOW = now.tv_sec - baseline_secs;
    NOW_MS = (now.tv_sec - baseline_secs) * 1000 + now.tv_usec / 1000.0;

    // Process event
    for (int i=0; i<n_peers; i++) {
      Peer *p = peers + i;
      if (FD_ISSET(p->sock, &writefds)) {
        if (process_peer_connect(p)) {
          send_handshake(&t->infohash, p->sock);
          p->stage = S_WAIT_HANDSHAKE;
        }
      }
      if (FD_ISSET(p->sock, &readfds)) {
        process_peer_read(p, t);
        shift_recvbuffer(p);
      }
    }

    if (t->downloaded_pieces == t->n_pieces) {
      printf("All pieces downloaded. Closing connections\n");
      for (int i=0; i < n_peers; i++) {
        Peer *peer = peers + i;
        if (peer->stage <= S_CONNECTING) {
          peer->stage = S_DONE;
        } else if (peer->stage == S_ERROR) {
          // Do nothing
        } else {
          printf("Closed connection with %d. \n", peer->peer_idx);
          close(peer->sock);
          peer->stage = S_DONE;
        }
      }
    } else {
      send_keepalives_and_disconnects(peers, n_peers, t);
    }
    print_summary(peers, n_peers, t);
  }
  return 0;
}

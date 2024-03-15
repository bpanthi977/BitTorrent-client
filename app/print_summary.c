#include <stdio.h>
#include "app.h"

void print_summary(Peer *peers, uint16_t n_peers, Torrent *t) {
  FILE *out;
  if (t->summary_file != NULL) {
    out = t->summary_file;
    fseek(out, 0, SEEK_SET);
  } else {
    out = stdout;
  }

  {
    int stages[S_DONE + 1] = {0};

    for (int i=0; i<n_peers; i++) {
      enum PeerStage s = peers[i].stage;
      stages[s]++;
    }

    fprintf(out, "Peers\n");
    fprintf(out, "Init:       %3d; Connecting: %3d; Connected: %3d; Hanshaking: %3d\n",
            stages[0], stages[1], stages[2], stages[3]);
    fprintf(out, "Handshaked: %3d; Active:     %3d; Error:     %3d; Done:       %3d\n\n",
            stages[4], stages[5], stages[6], stages[7]);
  }

  {
    int stages[PS_FLUSHED + 1] = {0};
    uint64_t sizes[PS_FLUSHED + 1] = {0};
    float total_instantaneous_speed = 0.0;
    float total_ma_speed = 0.0;

    for (int i=0; i<t->n_pieces; i++) {
      Piece *p = t->pieces + i;
      stages[p->state]++;
      if (p->state == PS_DOWNLOADING) {
        sizes[PS_DOWNLOADED] += p->recieved_count * p->block_size;
      } else {
        sizes[p->state] += t->piece_length;
      }


      if (p->speed_timestamp_ms != 0) {
        if (NOW_MS - p->speed_timestamp_ms > 500) {
          float instantaneous_speed = ((float) p->speed_bytes_recieved) / (NOW_MS - p->speed_timestamp_ms) * 1000 / 1024; // KiB/s
          if (p->speed_ma == -1) p->speed_ma = 0; // init
          p->speed_ma = p->speed_ma * 0.9 + 0.1 * instantaneous_speed;
          p->speed_bytes_recieved = 0;
          p->speed_timestamp_ms = NOW_MS;
          total_instantaneous_speed += instantaneous_speed;
        } else {
          total_instantaneous_speed += p->speed_ma;
        }
      }
      total_ma_speed += p->speed_ma;
    }

    t->total_ma_speed_download = total_ma_speed;

    fprintf(out, "Speed: %6.2f [%6.2f] KiB/s\n\n", total_ma_speed , total_instantaneous_speed);

    fprintf(out, "Pieces\n");
    fprintf(out, "Init:   %5d; Downloading:  %5d; Downloaded:   %5d  Pieces\n",
           stages[PS_INIT], stages[PS_DOWNLOADING], stages[PS_DOWNLOADED] + stages[PS_FLUSHED]);
    fprintf(out, "Init: %7.2f; Downloaded: %7.2f; Flushed:    %7.2f  MiB\n\n",
            sizes[PS_INIT]       / 1024.0 / 1024.0,
            sizes[PS_DOWNLOADED] / 1024.0 / 1024.0,
            sizes[PS_FLUSHED]    / 1024.0 / 1024.0);

  }

  {
    // Active peers
    fprintf(out, "\nActive Peers\n");
    for (int i = 0; i < n_peers; i++) {
      Peer *p = peers + i;
      if (p->stage == S_ACTIVE) {
        Piece *piece = p->piece;
        fprintf(out, "Peer %3d (%3.0f%%): Piece %5d (%4d / %4d) @ %8.2f KiB/s Priority: %d Last Msg: %ld\n",
                p->peer_idx, ((float)count_available_pieces(p, t->n_pieces)) / t->n_pieces * 100, piece->piece_idx,
                piece->recieved_count,piece->total_blocks,
                piece->speed_ma == -1 ? 0 : piece->speed_ma,
                p->priority,
                NOW - p->last_msg_time);
      }
    }

    // Handshaked Peers
    fprintf(out, "\nHandshaked Peers\n");
    for (int i = 0; i < n_peers; i++) {
      Peer *p = peers + i;
      if (p->stage == S_HANDSHAKED) {
        int available_pieces =  count_available_pieces(p, t->n_pieces);
        int interesting_pieces = count_interesting_pieces(t, p);

        Piece *piece = p->piece;
        fprintf(out, "Peer %3d (%3.0f%%): Has %5d pieces, Interested in %5d pieces, %s, Priority: %d \n",
                p->peer_idx, ((float)available_pieces / t->n_pieces * 100), available_pieces, interesting_pieces,
                p->unchoked ? "Unchoked" : "Choked",
                p->priority);
      }
    }

    // Other peers
    for (int i = 0; i < n_peers; i++) {
      enum PeerStage s = peers[i].stage;
      if (!(s == S_HANDSHAKED || s == S_ACTIVE)) fprintf(out, "\n");
    }
  }
}

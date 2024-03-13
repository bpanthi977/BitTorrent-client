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
          float instantaneous_speed = ((float) p->speed_bytes_recieved) / (NOW_MS - p->speed_timestamp_ms) * 1000 / 1024; // kiB/s
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

    fprintf(out, "Speed: %6.2f [%6.2f] kiB/s\n\n", total_ma_speed , total_instantaneous_speed);

    fprintf(out, "Pieces\n");
    fprintf(out, "Init:       %3d; Downloading:%3d; Downloaded:    %3d\n",
           stages[PS_INIT], stages[PS_DOWNLOADING], stages[PS_DOWNLOADED] + stages[PS_FLUSHED]);
    fprintf(out, "Init    %5.2f; Downloaded:%5.2f; Flushed:  %5.2f  MiB\n",
            sizes[PS_INIT]       / 1024.0 / 1024.0,
            sizes[PS_DOWNLOADED] / 1024.0 / 1024.0,
            sizes[PS_FLUSHED]    / 1024.0 / 1024.0);

    for (int i=0; i<t->n_pieces; i++) {
      Piece *p = t->pieces + i;
      fprintf(out, "Piece %3d; [%3d (%3d) / %3d] \n", p->piece_idx, p->recieved_count, p->outstanding_requests_count, p->total_blocks);
    }
  }
}

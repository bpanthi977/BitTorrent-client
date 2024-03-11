#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include "app.h"

int main(int argc, char *argv[]) {
    srand(time(NULL));
    if (argc < 3) {
        fprintf(stderr, "Usage: your_bittorrent.sh <command> <args>\n");
        return 1;
    }

    const char* command = argv[1];

    if (strcmp(command, "decode") == 0) {
        char *encoded_str = argv[2];
        Cursor cur = {.str = encoded_str};
        Value *val = decode_bencode(&cur);
        json_print(val);

    } else if (strcmp(command, "info") == 0) {
        const char *path = argv[2];
        String *buffer = read_file_to_string(path);
        if (buffer == NULL) {
          return 1;
        }
        char *buffer_start = buffer->str;
        Cursor cur = { .str = buffer->str };
        Value *torrent = decode_bencode(&cur);
        if (!assert_type(torrent, TDict, "Torrent file is not a valid bencode dictionary")) return 1;

        Value *announce = gethash(torrent, "announce");
        if (!assert_type(announce, TString, "Torrent announce field is not a string")) return 1;

        Value *info = gethash(torrent, "info");
        if (!assert_type(info, TDict, "Torrent info is not a Dict")) return 1;

        uint64_t length = torrent_total_length(info);
        printf("Tracker URL: %s\n", announce->val.string->str);
        printf("Length: %lld\n", length);

        Value *files = gethash(info, "files");
        if (files != NULL) {
          printf("Files: ");
          json_pprint(files);
        }

        // Info Hash
        Cursor cur2 = {.str = buffer_start};
        char sha_out[20];

        encode_bencode(info, &cur2);
        SHA1(sha_out, buffer_start, cur2.str - buffer_start);

        printf("Info Hash: ");
        pprint_hex((uint8_t *)sha_out, 20);
        printf("\n");

        // Piece Length and Hashes
        Value *piece_length = gethash(info, "piece length");
        if (!assert_type(piece_length, TInteger, "Torrent info.piece_length is not an integer")) return 1;

        printf("Piece Length: %lld\n", piece_length->val.integer);

        Value *pieces = gethash(info, "pieces");
        if (!assert_type(pieces, TString, "Torrent info.pieces is not an string")) return 1;
        String *hash = pieces->val.string;
        printf("Piece Hashes:\n");
        {
          int offset = 0;
          while(offset < hash->length) {
            pprint_hex((uint8_t *)(hash->str + offset), 20);
            printf("\n");
            offset += 20;
          }
        }

    } else if (strcmp(command, "peers") == 0) {
        Value *torrent = read_torrent_file(argv[2]);
        if (torrent == NULL) return 1;

        String hash = info_hash(torrent);
        if (hash.str == NULL) return 1;

        struct sockaddr_in *peers;
        int n_peers = fetch_peers(torrent, &peers);

        struct sockaddr_in *p = peers;
        for (int i=0; i<n_peers; i++) {
          pprint_sockaddr(*p);
          p++;
        }

    } else if (strcmp(command, "handshake") == 0) {
        if (argc < 4) return 1;

        Value *torrent = read_torrent_file(argv[2]);
        if (torrent == NULL) return 1;

        // Get ip and port from command line
        struct sockaddr_in peer_addr = parse_ip_port(argv[3]);

        // create torrent
        Torrent t = create_torrent(torrent);

        // create and connect peer
        Peer p = create_peer(0, t.n_pieces, 10 * 1024);
        p.recvbuffer = malloc(p.buffer_size);
        connect_peer(&p, peer_addr);

        // start communication loop
        String infohash = info_hash(torrent);

        // Mark all pieces as downloaed, so that we just do handshake and do not
        // download
        for (int i=0; i<t.n_pieces; i++) {
          t.pieces[i].state = PS_DOWNLOADED;
          t.downloaded_pieces++;
        }
        Peer *peers = &p;
        start_communication_loop(peers, 1, &t);

        // check for success
        free(infohash.str);
        if (p.stage == S_ERROR) {
          return 1;
        }
        String peer_id = {.str = p.peer_id, .length = 20};
        printf("Peer ID: ");
        pprint_hex((uint8_t *)peer_id.str, peer_id.length);
        printf("\n");

    } else if (strcmp(command, "download_piece") == 0) {
        // 1. Parse args
        if (argc < 6) return 1;
        char *output_path = argv[3];
        char *input_file = argv[4];
        int first_piece_idx = atoi(argv[5]);
        int last_piece_idx = first_piece_idx;
        if (argc >= 7) {
          last_piece_idx = atoi(argv[6]);
        }

        // 2. Read torrent file
        Value *torrent = read_torrent_file(input_file);
        Torrent t = create_torrent(torrent);

        // 3. Get peer addresses
        struct sockaddr_in *peer_addrs;
        int n_peers = 0;
        if (argc == 8) {
          // get peer from command line
          struct sockaddr_in peer_addr = parse_ip_port(argv[7]);
          peer_addrs = malloc(sizeof(struct sockaddr_in));
          *peer_addrs = peer_addr;
          n_peers = 1;
        } else {
          printf("Fetching peers from tracker\n");
          n_peers = fetch_peers(torrent, &peer_addrs);
          if (n_peers <= 0) {
            fprintf(stderr, "Couldn't find peers. Quitting. %d\n", n_peers);
            return 1;
          }
        }
        int MAX_PEERS = 50;
        if (n_peers > MAX_PEERS) n_peers = MAX_PEERS; // Max 10 peers only. For now.

        // 4. Create and connect Peers
        Peer *peers = malloc(sizeof(Peer) * n_peers);

        Peer *p = peers;
        struct sockaddr_in *addr = peer_addrs;
        int failed_peers = 0;
        int buffer_size = 20 * 16 * 1024; // Enough size of 20 blocks of 16 kiB
        for (int idx=0; idx<n_peers; idx++) {
          *p = create_peer(idx, t.n_pieces, buffer_size);
          if (connect_peer(p, *addr)) {
            p++;
          } else {
            failed_peers++;
          }
          addr++;
        }
        n_peers -= failed_peers;

        // 5. Mark only one piece for download
        for (int i=0; i < t.n_pieces; i++) {
          if (i < first_piece_idx || i > last_piece_idx) {
            t.pieces[i].state = PS_DOWNLOADED;
            t.downloaded_pieces++;
          }
        }

        // 5. Start communication
        start_communication_loop(peers, n_peers, &t);

        // 6. Write to file
        if (t.downloaded_pieces == t.n_pieces) {
          FILE *file = fopen(output_path, "wb");
          if (file == NULL) {
            fprintf(stdout, "Coulndn't open output file %s\n", output_path);
            return 1;
          }

          for (int idx = first_piece_idx; idx <= last_piece_idx; idx++) {
            Piece piece = t.pieces[idx ];
            if ( 1 == fwrite(piece.buffer, piece.piece_length, 1, file)) {
              printf("Piece %d downloaded to file %s\n", first_piece_idx, output_path);
            } else {
              printf("Couldn't write %llu bytes to file %s\n", piece.piece_length, output_path);
            }
          }
          fclose(file);
        }

        // 6. Free
        for (int idx=0; idx<n_peers; idx++) {
          free_peer(peers + idx);
        }
        free_torrent(&t);
        return 0;

    } else if (strcmp(command, "download") == 0) {
        // 1. Parse args
        if (argc < 5) return 1;
        char *output_path = argv[3];
        char *input_path = argv[4];

        // 2. Read torrent file
        Value* torrent = read_torrent_file(input_path);
        Torrent t = create_torrent(torrent);
        json_pprint(torrent);

        // 3. Get peer addresses
        struct sockaddr_in *peer_addrs;
        int n_peers = 0;
        if (argc == 6) {
          // get peer from command line
          struct sockaddr_in peer_addr = parse_ip_port(argv[5]);
          peer_addrs = malloc(sizeof(struct sockaddr_in));
          *peer_addrs = peer_addr;
          n_peers = 1;
        } else {
          printf("Fetching peers from tracker\n");
          n_peers = fetch_peers(torrent, &peer_addrs);
          if (n_peers <= 0) {
            fprintf(stderr, "Couldn't find peers. Quitting. %d\n", n_peers);
            return 1;
          }
        }

        // 5. Open file
        FILE *file = fopen(output_path, "wb");
        if (file == NULL) {
          fprintf(stdout, "Coulndn't open output file %s\n", output_path);
          return 1;
        }
        t.output_file = file;

        // 6. Create and connect Peers
        Peer *peers = malloc(sizeof(Peer) * n_peers);

        Peer *p = peers;
        struct sockaddr_in *addr = peer_addrs;
        int failed_peers = 0;
        int buffer_size = 20 * 16 * 1024; // Enough size of 20 blocks of 16 kiB
        for (int idx=0; idx<n_peers; idx++) {
          *p = create_peer(idx, t.n_pieces, buffer_size);
          if (connect_peer(p, *addr)) {
            p++;
          } else {
            failed_peers++;
          }
          addr++;
        }
        n_peers -= failed_peers;

        // 7. Start communication
        start_communication_loop(peers, n_peers, &t);

        // 8. Free
        for (int idx=0; idx<n_peers; idx++) {
          free_peer(peers + idx);
        }
        free_torrent(&t);

        // 9. Close. Done.
        fclose(file);
        printf("Downloaded %d pieces to %s\n", t.n_pieces, output_path);
        return 0;

    } else if (strcmp(command, "info-all") == 0) {
        const char *path = argv[2];
        String *buffer = read_file_to_string(path);
        if (buffer == NULL) {
          return 1;
        }
        Cursor cur = { .str = buffer->str };
        json_pprint(decode_bencode(&cur));

    } else if (strcmp(command, "encode-decode") == 0) {
        char *encoded_str = argv[2];

        Cursor cur = {.str = encoded_str};
        Value *val = decode_bencode(&cur);
        json_pprint(val);

        char *buffer = malloc(strlen(encoded_str) + 1);
        Cursor cur2 = { .str = buffer };
        encode_bencode(val, &cur2);
        *cur2.str = '\0';
        printf("%s\n", buffer);

    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return 1;
    }

    return 0;
}

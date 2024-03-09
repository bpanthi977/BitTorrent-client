#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "app.h"

int main(int argc, char* argv[]) {
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

        Value *length = gethash(info, "length");
        if (!assert_type(length, TInteger, "Torrent info.length is not an integer")) return 1;

        printf("Tracker URL: %s\n", announce->val.string->str);
        printf("Length: %lld\n", length->val.integer);

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

        Value *res = fetch_peers(torrent);

        String *peers = gethash_safe(res, "peers", TString)->val.string;
        uint8_t *s = (uint8_t *)peers->str;
        for (int i = 0; i < peers->length; i += 6) {
          printf("%d.%d.%d.%d:%d\n", s[0], s[1], s[2], s[3], s[4] * 256 + s[5]);
          s+=6;
        }

    } else if (strcmp(command, "handshake") == 0) {
        if (argc < 4) return 1;

        Value *torrent = read_torrent_file(argv[2]);
        if (torrent == NULL) return 1;

        // Get ip and port from command line
        struct sockaddr_in peer_addr = parse_ip_port(argv[3]);

        Peer p = {0};
        connect_peer(&p, peer_addr);

        String infohash = info_hash(torrent);
        do_handshake(&p, infohash);
        free(infohash.str);

        String peer_id = {.str = p.peer_id, .length = 20};
        printf("Peer ID: ");
        pprint_hex((uint8_t *)peer_id.str, peer_id.length);
        printf("\n");

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

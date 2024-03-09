#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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
        printf("\n");

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

        String *hash = info_hash(torrent);
        if (hash == NULL) return 1;

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
        char *peer = argv[3];
        char *colon_char = strchr(peer, ':');
        *colon_char = '\0';
        char *peer_ip = peer, *peer_port = colon_char + 1;

        // Get Addrinfo
        int status;
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo *servinfo;  // will point to the results

        if ((status = getaddrinfo(peer_ip, peer_port, &hints, &servinfo)) != 0) {
          fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
          exit(1);
        }

        // Open socket
        int fd = socket(PF_INET, SOCK_STREAM, 0);

        // Connect
        if (connect(fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
          fprintf(stderr, "Error connecting to socket. errno: %d\n", errno);
          exit(1);
        }

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
        String *hash = info_hash(torrent);
        append_string(hash, &cur);
        free(hash->str);
        free(hash);
        // 20 bytes peer id
        append_str("00112233445566778899", &cur);

        // Send
        send(fd, buffer, cur.str - buffer, 0);
        //pprint_hex(buffer, cur.str - buffer);
        //printf("\nsent mesasge %ld bytes\n", cur.str - buffer);

        // Recieve response
        char recvbuffer[1024];
        int bytes = recv(fd, recvbuffer, 1024, 0);
        if (bytes == -1) {
          fprintf(stderr, "Error occured while recv: %d\n", errno);
          exit(1);
        } else if (bytes == 0) {
          fprintf(stderr, "Peer disconnected without sending\n");
          exit(1);
        }
        //printf("Recieved %d bytes\n", bytes);
        String recvstr = {.str = recvbuffer, .length = bytes};
        //pprint_str(&recvstr);
        //printf("\n");

        if (bytes < 68) {
          fprintf(stderr, "Recieved input is invalid\n");
          exit(1);
        }

        String peer_id = {.str = recvbuffer + 1 + 19 + 8 + 20, .length = 20};
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
        printf("\n");

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

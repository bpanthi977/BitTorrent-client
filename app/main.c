#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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
        char *buffer = read_file_to_string(path);
        if (buffer == NULL) {
          return 1;
        }
        Cursor cur = { .str = buffer };
        Value *torrent = decode_bencode(&cur);
        if (!assert_type(torrent, Dict, "Torrent file is not a valid bencode dictionary")) return 1;

        Value *announce = gethash(torrent, "announce");
        if (!assert_type(announce, String, "Torrent announce field is not a string")) return 1;

        Value *info = gethash(torrent, "info");
        if (!assert_type(info, Dict, "Torrent info is not a Dict")) return 1;

        Value *length = gethash(info, "length");
        if (!assert_type(length, Integer, "Torrent info.lenght is not an integer")) return 1;

        printf("Tracker URL: %s\n", announce->val.string);
        printf("Length: %lld\n", length->val.integer);
    } else if (strcmp(command, "info-all") == 0) {
        const char *path = argv[2];
        char *buffer = read_file_to_string(path);
        if (buffer == NULL) {
          return 1;
        }
        Cursor cur = { .str = buffer };
        json_pprint(decode_bencode(&cur));
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return 1;
    }

    return 0;
}

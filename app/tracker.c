#include <errno.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>
#include "app.h"
#include <netdb.h>
#include <sys/socket.h>


#define DEBUG false
//////////
/// HTTP Tracker
/////////

static size_t cb_curl_write_to_string(void *data, size_t size, size_t blocks, void *callback_data) {
  size_t realsize = size * blocks;
  String *str = (String *)callback_data;

  char *ptr = realloc(str->str, str->length + realsize + 1);
  if(!ptr)
    return 0;  /* out of memory! */

  str->str = ptr;
  memcpy(&(str->str[str->length]), data, realsize);
  str->length += realsize;
  str->str[str->length] = 0;

  return realsize;
}

int http_fetch_peers(Value* torrent, struct sockaddr_in **peers) {
  // Send HTTP GET request at <torrent.announce> with query params:
  // info_hash = info_hash(torrent)
  // peer_id = char[20]
  // port = 6881
  // uploaded = 0
  // downloaded = 0
  // left = torrent.info.length
  // compact = 1

  CURL *curl = curl_easy_init();
  String *announce = gethash_safe(torrent, "announce", TString)->val.string;
  Value *info = gethash_safe(torrent, "info", TDict);
  uint64_t length = torrent_total_length(info); // max 20 characters
  String hash = info_hash(torrent);

  char *url = malloc(announce->length + 400);
  Cursor cur = {.str = url};
  append_string(announce, &cur);
  append_str("?info_hash=", &cur); url_encode(&hash, &cur);
  append_str("&peer_id=00112233445566778899", &cur);
  append_str("&port=6881", &cur);
  append_str("&uploaded=0", &cur);
  append_str("&downloaded=0", &cur);
  append_str("&left=", &cur);
  cur.str += sprintf(cur.str, "%lld", length);
  append_str("&compact=1", &cur);
  *cur.str = '\0';

  curl_easy_setopt(curl, CURLOPT_URL, url);

  String response = {.str = NULL, .length = 0};
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb_curl_write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

  CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    fprintf(stderr, "Failed to fetch peers from tracker: %s\n", announce->str);
    exit(1);
  }

  curl_easy_cleanup(curl);

  Cursor cur2 = {.str = response.str };
  Value *res = decode_bencode(&cur2);

  free(hash.str);
  free(url);

  String *peers_buff = gethash_safe(res, "peers", TString)->val.string;
  return parse_peer_addresses(peers_buff, peers);
}

//////////
/// UDP Tracker
/////////

enum UDP_Tracker_Actions {
  A_CONNECT = 0,
  A_ANNOUNCE = 1
};

struct addrinfo *get_tracker_addrs(Value *torrent) {
  // Parse hostname and port from announce url
  String *announce = gethash_safe(torrent, "announce", TString)->val.string;

  char hostname[announce->length];
  char port[announce->length];
  int hostname_start_idx = 6; // Length of udp://
  int port_start_idx = 0;
  int port_end_idx = announce->length;
  for (int idx = hostname_start_idx; idx < announce->length; idx++) {
    char c = announce->str[idx];
    if (c == ':') {
      port_start_idx = idx + 1;
    } else if (c == '/') {
      port_end_idx = idx - 1;
    }
  }
  if (port_start_idx == 0 || port_start_idx >= port_end_idx) {
    fprintf(stderr, "Invalid port in announce url");
    exit(1);
  }

  int hostname_len = (port_start_idx - 1) - hostname_start_idx;
  memcpy(hostname, announce->str + hostname_start_idx, hostname_len);
  hostname[hostname_len] = '\0';

  int port_len = port_end_idx - port_start_idx + 1;
  memcpy(port, announce->str + port_start_idx,  port_len);
  port[port_len] = '\0';

  // Get addrinfo from announce hostname, port
  struct addrinfo hints = {0};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICSERV;

  struct addrinfo *addrs;

  if (DEBUG) printf("tracker hostname: %s, port: %s\n", hostname, port);
  int ret = getaddrinfo(hostname, port, &hints, &addrs);
  if (ret != 0) {
    fprintf(stderr, "failed. code: %d, msg: %s\n", ret, gai_strerror(ret));
    exit(1);
  }
  return addrs;
}


bool udp_connect(int fd, uint64_t *connection_id) {
  uint32_t transaction_id = rand();
  uint8_t connect_req[16];
  *(uint64_t *)connect_req =
      htonll((uint64_t)0x41727101980); // protocol_id (magic number) 8 bytes
  *(uint32_t *)(connect_req + 8) = A_CONNECT;       // action_id 4 bytes
  *(uint32_t *)(connect_req + 12) = transaction_id; // transaction_id 4 bytes

  if (DEBUG) {
    printf("[udp_fetch_peers] sending connect_req:\n\t");
    pprint_hex(&connect_req, 16);
    printf("\n");
  }

  send(fd, connect_req, 16, 0);

  uint8_t connect_res[16];
  int bytes = recv(fd, connect_res, 16, 0);
  if (DEBUG) {
    printf("[udp_fetch_peers] recieved:\n\t ");
    pprint_hex(&connect_res, 16);
    printf("\n");
  }

  if (bytes < 16) {
    fprintf(stderr,
            "[udp_fetch_peers] connect response is less than 16 bytes\n");
    pprint_hex(connect_res, 16);
  } else if (*(uint32_t *)connect_res != A_CONNECT) {
    fprintf(stderr,
            "[udp_fetch_peers] connect response action_id is not connect\n");
  } else if (*(uint32_t *)(connect_res + 4) != transaction_id) {
    fprintf(
        stderr,
        "[udp_fetch_peers] connect response transaction_id doesn't match\n");
  } else {
    // All good
    *connection_id = *(uint64_t *)(connect_res + 8);
    if (DEBUG) {
      printf("Got connection id: ");
      pprint_hex(connection_id, 8);
      printf("\n");
    }
    return true;
  }
  return false;
}

int UDP_MAX_DATA = 65527;

int udp_fetch_peers(Value *torrent, struct sockaddr_in **peers) {
  Value *info = gethash_safe(torrent, "info", TDict);
  uint64_t length = torrent_total_length(info); // max 20 characters
  String infohash = info_hash(torrent);

  struct addrinfo *addr, *addr0 = get_tracker_addrs(torrent);

  // Open a socket to any one of the address
  addr = addr0;
  int fd = -1;
  while (addr != NULL) {
    fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (fd != -1) {
      // picks a port and sets default reciever for udp
      int ret = connect(fd, addr->ai_addr, addr->ai_addrlen);
      if (ret == -1) {
        fprintf(stderr, "[udp_fetch_peers] .connect failed: %s\n", strerror(errno));
      } else {
        // success
        if (DEBUG) printf("socket created \n");
        break;
      }
    } else {
      fprintf(stderr, "[udp_fetch_peers] .socket failed: %s\n", strerror(errno));
    };

    fd = -1;
    addr = addr->ai_next;
  }


  // Connect
  uint64_t connection_id;
  if (!udp_connect(fd, &connection_id)) {
    fprintf(stderr, "tracker A_CONNECT failed\n");
    return -1;
  }
  /// Announce
  uint64_t downloaded = 0;
  uint64_t left = 0;
  uint64_t uploaded = 0;
  uint16_t port = 6881;
  char *PEER_ID = "00112233445566778899";

  // Assemble request packet
  uint32_t transaction_id = rand();
  uint8_t announce_req[98];
  uint8_t *req = announce_req;
  *(uint64_t *) req = connection_id;      req += 8;
  *(uint32_t *) req = htonl(A_ANNOUNCE);  req += 4;
  *(uint32_t *) req = transaction_id;     req += 4;
  memcpy(       req, infohash.str, 20);  req+=20;
  memcpy(       req, PEER_ID, 20); req+=20;
  *(uint64_t *) req = htonll(downloaded); req += 8;
  *(uint64_t *) req = htonll(left);       req += 8;
  *(uint64_t *) req = htonll(uploaded);   req += 8;
  *(uint32_t *) req = 0;                  req += 4; // Event
  *(uint32_t *) req = 0;                  req += 4; // IP address
  *(uint32_t *) req = 0;                  req += 4; // Key
  *(int32_t *)  req = -1;                 req += 4; // num_want
  *(uint16_t *) req = htonl(port);       req += 2;

  if (DEBUG) {
    printf("[udp_fetch_peers] Sending announce req:\n\t");
    pprint_hex(announce_req, 98);
    printf("\n");
  }
  send(fd, announce_req, 98, 0);

  uint8_t response[UDP_MAX_DATA];
  size_t bytes = recv(fd, response, UDP_MAX_DATA, 0);

  if (DEBUG) {
    printf("[udp_fetch_peers] Recieved %zu bytes announce response:\n\t", bytes);
    pprint_hex(response, bytes);
    printf("\n");
  }

  if (bytes < 20) {
    fprintf(stderr, "[udp_fetch_peers] Didn't recieve enough data in Announce response\n");
  } else if (ntohl(*(uint32_t *) response) != A_ANNOUNCE) {
    fprintf(stderr, "[udp_fetch_peers] Didn't recieve A_ANNOUNCE in Announce response\n");
  } else if (*(uint32_t *) (response + 4) != transaction_id) {
    fprintf(stderr, "[udp_fetch_peers] Recieved different transaction_id in announce response\n");
  } else {
    // All good
    uint32_t interval = ntohl(* (uint32_t *) (response + 8));
    uint32_t leechers = ntohl(* (uint32_t *) (response + 12));
    uint32_t seeders  = ntohl(* (uint32_t *) (response + 16));
    uint32_t total_peers = (bytes - 20) / 6;

    printf("[udp_fetch_peers] interval: %d, leechers: %d, seeders: %d, peers: %d\n", interval, leechers, seeders, total_peers);
    // Recieve ip and ports
    struct sockaddr_in *addr, *addr0 = malloc(sizeof(struct sockaddr_in) * total_peers);
    addr = addr0;
    for (int i=0; i<total_peers; i++) {
      addr->sin_family = AF_INET;
      addr->sin_addr.s_addr = *(uint32_t *) (response + 20 + i * 6);
      addr->sin_port = *(uint16_t *) (response + 24 + i *6);
      addr++;
    }

    *peers = addr0;
    return total_peers;
  }

  fflush(stdout);
  return 0;
}

int fetch_peers(Value *torrent, struct sockaddr_in **peers) {
  String *announce = gethash_safe(torrent, "announce", TString)->val.string;
  if (announce->length > 6 && memcmp("udp://", announce->str, 6) == 0) {
    return udp_fetch_peers(torrent, peers);
  } else {
    return http_fetch_peers(torrent, peers);
  }
}

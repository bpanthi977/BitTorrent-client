#ifndef PACKETS_H
#define PACKETS_H

// packets_send.h
void send_handshake(String *infohash, int fd);
void send_msg(Peer *p, Message msg);
void send_bitfield(Torrent *t, Peer *p);
void send_interested(Peer *peer);
void send_unchoke(Peer *peer);
void send_keepalive(Peer *peer);

// packets_recieve.c
int peer_recv(Peer *p);
Message pop_message(Peer *p);
void shift_recvbuffer(Peer *p);

#endif

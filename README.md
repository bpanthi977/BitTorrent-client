# BitTorrent Client

An implementation of BittTorrent client in C. Made while completing the ["Build Your Own BitTorrent" Challenge](https://app.codecrafters.io/courses/bittorrent/overview).
# Build

1. This project depends on [curl](https://curl.se/). The library is probably already installed in your system. If not download the library and headers following the instructions from the [curl](https://curl.se/) project.

2. Run `make` command to build the client binary: `torrent-client`

# Run

1. Download file:

    `torrent-client download sample.torrent sample.txt`
  
2. View torrent file info

    `torrent-client info sample.torrent`

# Features & Limitations

1. Can use multiple tracker from announce list
2. Can use both UDP and HTTP tracker
3. Single file downloads only 

   

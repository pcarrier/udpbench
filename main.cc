#include <chrono>
#include <err.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <thread>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

const char * get_req = "\0\0\0\0\0\0\0\0get ping\r\n";
struct iovec get_req_iov = {
    .iov_base = const_cast<char *>(get_req),
    .iov_len = sizeof(get_req)
};

int main(int argc, const char **argv) {
    int sock;
    struct sockaddr_in addr;
    struct addrinfo * peer;
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t received = 0;

    if (argc != 6) {
        errx(EX_USAGE, "expects params: host port pkts sendchunk recvchunk", argv[0]);
    }

    const char * hostname = argv[1];
    const char * port = argv[2];
    const long pkts = atoll(argv[3]);
    const long sendchunk = atoll(argv[4]);
    const long recvchunk = atoll(argv[5]);

    if (pkts <= 0 || sendchunk <= 0) {
        errx(EX_USAGE, "couldn't parse numeric argument");
    }

    if(getaddrinfo(argv[1], argv[2], nullptr, &peer)) {
        errx(EX_NOHOST, "couldn't resolve %s:%s", hostname, port);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0);

    sock = socket(peer->ai_family, peer->ai_socktype, peer->ai_protocol);
    if (sock < 0) {
        err(EX_OSERR, "cannot create socket");
    }

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        err(EX_OSERR, "cannot bind socket");
    }

    std::thread sender([sock, peer, pkts, sendchunk]() {
        auto msgs = new struct mmsghdr[sendchunk];
        for (long i = 0; i < sendchunk; i++) {
            struct msghdr * hdr = & msgs[i].msg_hdr;
            hdr->msg_name = peer->ai_addr;
            hdr->msg_namelen = peer->ai_addrlen;
            hdr->msg_iov = & get_req_iov;
            hdr->msg_iovlen = 1;
            hdr->msg_controllen=0;
        }

        for(long i = 0; i < pkts; i += sendchunk) {
            long sent = 0;
            while (sent != sendchunk) {
                sent = sendmmsg(sock, msgs, sendchunk, 0);
                if (sent < 0) {
                    err(EX_IOERR, "failed sending to peer");
                }
            }
        }
        delete[] msgs;
    });

    std::thread receiver([sock, peer, received, start_time, pkts, recvchunk]() {
        char buffer[64 * 1024];
        struct iovec recv_iov = { .iov_base = buffer, .iov_len = sizeof(buffer) };

        auto msgs = new struct mmsghdr[recvchunk];
        for (long i = 0; i < recvchunk; i++) {
            struct msghdr * hdr = & msgs[i].msg_hdr;
            hdr->msg_name = peer->ai_addr;
            hdr->msg_namelen = peer->ai_addrlen;
            hdr->msg_iov = &recv_iov;
            hdr->msg_iovlen = 1;
            hdr->msg_controllen=0;
        }

        long received = 0;

        for (long i = 0; i < pkts; i += recvchunk) {
            received += recvmmsg(sock, msgs, recvchunk, MSG_WAITFORONE, NULL);
        }

        delete[] msgs;
    });

    std::thread logger([start_time, received]() {
        for (;;) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = start_time - now;
            auto elapsed_nanos = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            double rate = (double) received * 1000 * 1000 / elapsed_nanos;
            std::cerr << "received " << received << " @ " << rate << " pkts/sec" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    sender.join();
    receiver.join();
    logger.join();
}

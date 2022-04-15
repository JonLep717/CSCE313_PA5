#include "TCPRequestChannel.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;


TCPRequestChannel::TCPRequestChannel (const std::string _ip_address, const std::string _port_no) {
    // if server (differentiated using ip_addresss)
    //      create a socket on the specified port
    //          specify domain (AF_INET, IPv4), type (SOCK_STREAM, TCP), and protocol (0)
    //      bind the socket to an address, collected from machine, sets up listening
    //      mark the socket as listening (call to listen())
    // socket() -> bind() -> listen() 

    // if client
    //      create a socket on the specified port
    //          specify domain, type, and protocol
    //      connect socket to the IP addr of the server
    // socket() -> connect()

    if (_ip_address == "") {
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;

        int port = stoi(_port_no);
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        memset(&serv_addr.sin_zero, 0, sizeof(serv_addr.sin_zero));
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("server:socket");
            exit(EXIT_FAILURE);
        }
        // forcibly re-bind port in the server
        int flag = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) < 0) {
            perror("setsockopt failed");
            exit(EXIT_FAILURE);
        }

        if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("server:bind");
            exit(EXIT_FAILURE);
        }
        if (listen(sockfd, 30) < 0) {
            perror("server:listen");
            exit(EXIT_FAILURE);
        }
    }

    else {
        struct sockaddr_in client_addr;
        memset(&client_addr,0,sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        int port = stoi(_port_no);
        client_addr.sin_port = htons(port);
        memset(&client_addr.sin_zero, 0, sizeof(client_addr.sin_zero));
        inet_pton(AF_INET, _ip_address.c_str(), &(client_addr.sin_addr));
        
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("client:socket");
            exit(EXIT_FAILURE);
        }
        
        if (connect(sockfd, (const struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
            perror("client:connect");
            exit(EXIT_FAILURE);
        }

    }
    // socket() -> bind() -> listen() 
}

TCPRequestChannel::TCPRequestChannel (int _sockfd) {
    // set member variable
    sockfd = _sockfd;
}

TCPRequestChannel::~TCPRequestChannel () {
    //close file desc of sock, sockfd
    // close()
    close(sockfd);
}

int TCPRequestChannel::accept_conn () {
    // struct sockaddr_storage, used to establish client connection
    // implementing accept(...) - returns the sockfd of the client
    // accept()
    struct sockaddr_storage conn_addr;
    socklen_t addrlen = sizeof(conn_addr);
    int client_sockfd = accept(sockfd, (struct sockaddr*) &conn_addr, &addrlen);
    if (client_sockfd < 0) {
        perror("accept_conn");
        exit(EXIT_FAILURE);
    }
    return client_sockfd;
}

int TCPRequestChannel::cread (void* msgbuf, int msgsize) {
    // read() (or recv/send)
    return recv(sockfd, msgbuf, msgsize, 0);
}

int TCPRequestChannel::cwrite (void* msgbuf, int msgsize) {
    // write()
    return send(sockfd, msgbuf, msgsize, 0);
}

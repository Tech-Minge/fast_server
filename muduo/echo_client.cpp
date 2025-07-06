#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <vector>
#include "protocol.hpp"

const int PORT = 8080;
const char* SERVER_IP = "127.0.0.1";

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serv_addr = {AF_INET, htons(PORT)};
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);
    
    connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr));

    uint8_t type = 1;
    
    char data[] = "BBB";
    char buffer[1024];
    size_t s = encoder(1, data, 3, buffer, sizeof(buffer));
    
    send(sock, buffer, s, 0);
    std::cout << "Sent type: " << static_cast<int>(type) << ", len: 3" << std::endl;
    
    char decode[1024];
    ssize_t bytes_read = recv(sock, buffer, sizeof(buffer), 0);
    uint32_t data_len;
    decoder(buffer, bytes_read, &type, decode, &data_len);
    std::cout << "Received type: " << static_cast<int>(type) << ", len: " << data_len << std::endl;
    close(sock);
    return 0;
}
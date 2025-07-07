#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <thread>
#include <vector>

#include "protocol.hpp"

const int PORT = 8080;
const int BUFFER_SIZE = 4096;

void handle_client(int client_sock) {
  char buffer[BUFFER_SIZE];

  while (true) {
    size_t bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) break;

    char decode[BUFFER_SIZE];
    uint8_t type;
    uint32_t data_len;
    decoder(buffer, bytes_read, &type, decode, &data_len);

    char out[BUFFER_SIZE];
    size_t len = encoder(type, decode, data_len, out, BUFFER_SIZE);

    send(client_sock, out, len, 0);
  }
  close(client_sock);
}

int main() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr = {AF_INET, htons(PORT), INADDR_ANY};

  // 绑定并监听
  bind(server_fd, (sockaddr*)&addr, sizeof(addr));
  listen(server_fd, 5);
  std::cout << "Server listening on port " << PORT << std::endl;

  while (true) {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &addr_len);

    // 新客户端线程
    std::thread(handle_client, client_fd).detach();
  }
  close(server_fd);
  return 0;
}
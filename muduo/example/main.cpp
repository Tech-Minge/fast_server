
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "TcpSpi.hpp"
#include "TcpApi.hpp"


#define DEFAULT_PORT    8080


void server_run();
void client_run();



class EchoServer: public TcpSpi {
public:
	void onAccepted(std::shared_ptr<Connection> conn) override {
		printf("[+] new connection accepted\n");
		// conn->setSpi(this);
	}

	void onDisconnected(std::shared_ptr<Connection> conn, int reason, const char* reason_str) override {
		printf("[+] connection closed: %s\n", reason_str);
	}

	void onMessage(std::shared_ptr<Connection> conn, const char* data, size_t len) override {
		printf("[+] received message: %.*s\n", (int)len, data);
		std::vector<char> vec;
		for (int i = 0; i < 10; ++i) {
			vec.insert(vec.end(), data, data + len);
		}
		conn->send(vec.data(), vec.size()); // echo back
	}
};

void reactor_run()
{
	TcpApi api;
	api.bindAddress("127.0.0.1", DEFAULT_PORT);
	EchoServer echo_server;
	api.registerSpi(&echo_server);
	api.run();
}

int main(int argc, char *argv[])
{
	
	int opt;
	char role = 's';
	while ((opt = getopt(argc, argv, "cs")) != -1) {
		switch (opt) {
		case 'c':
			role = 'c';
			break;
		case 's':
			break;
		default:
			printf("usage: %s [-cs]\n", argv[0]);
			exit(1);
		}
	}
	if (role == 's') {
		// server_run();
		reactor_run(); // 使用Reactor模式运行服务器
	} else {
		client_run();
	}
	return 0;
}

/*
 * register events of fd to epfd
 */
static void epoll_ctl_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev;
	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl()\n");
		exit(1);
	}
}

static void set_sockaddr(struct sockaddr_in *addr, const char* ip, int port)
{
	bzero((char *)addr, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(ip);
	addr->sin_port = htons(port);
}

static int setnonblocking(int sockfd)
{
	if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) ==
	    -1) {
		return -1;
	}
	return 0;
}


void server_run()
{
	int i;
	int n;
	int epfd;
	int nfds;
	int listen_sock;
	int conn_sock;
	socklen_t socklen;
	char buf[256];
	struct sockaddr_in srv_addr;
	struct sockaddr_in cli_addr;
	struct epoll_event events[16];

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);

	set_sockaddr(&srv_addr, "127.0.0.1", DEFAULT_PORT);
	bind(listen_sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr));

	setnonblocking(listen_sock);
	listen(listen_sock, 16);

	epfd = epoll_create(1);
	epoll_ctl_add(epfd, listen_sock, EPOLLIN | EPOLLOUT | EPOLLET);

	socklen = sizeof(cli_addr);
	for (;;) {
		nfds = epoll_wait(epfd, events, 16, -1);
		for (i = 0; i < nfds; i++) {
			if (events[i].data.fd == listen_sock) {
				/* handle new connection */
				conn_sock =
				    accept(listen_sock,
					   (struct sockaddr *)&cli_addr,
					   &socklen);

				inet_ntop(AF_INET, (char *)&(cli_addr.sin_addr),
					  buf, sizeof(cli_addr));
				printf("[+] connected with %s:%d\n", buf,
				       ntohs(cli_addr.sin_port));

				setnonblocking(conn_sock);
				epoll_ctl_add(epfd, conn_sock,
					      EPOLLIN | EPOLLET | EPOLLRDHUP |
					      EPOLLHUP);
			} else if (events[i].events & EPOLLIN) {
				/* handle EPOLLIN event */
				for (;;) {
					bzero(buf, sizeof(buf));
					n = read(events[i].data.fd, buf,
						 sizeof(buf));
					if (n <= 0 /* || errno == EAGAIN */ ) {
						break;
					} else {
						printf("[+] data: %s\n", buf);
						int n = write(events[i].data.fd, buf,
						      strlen(buf));
						printf("[+] write %d bytes\n", n);
					}
				}
			} else {
				printf("[+] unexpected\n");
			}
			/* check if the connection is closing */
			if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
				printf("[+] connection closed\n");
				epoll_ctl(epfd, EPOLL_CTL_DEL,
					  events[i].data.fd, NULL);
				close(events[i].data.fd);
				continue;
			}
		}
	}
}

/*
 * test clinet 
 */
void client_run()
{
	int n;
	int c;
	int sockfd;
	char buf[1024];
	struct sockaddr_in srv_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	set_sockaddr(&srv_addr, "127.0.0.1", DEFAULT_PORT);

	if (connect(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
		perror("connect()");
		exit(1);
	}
	for (;;) {
		printf("input: ");
		fgets(buf, sizeof(buf), stdin);
		c = strlen(buf) - 1;
		buf[c] = '\0';
		write(sockfd, buf, c + 1);

		bzero(buf, sizeof(buf));
		while (errno != EAGAIN
		       && (n = read(sockfd, buf, sizeof(buf))) > 0) {
			printf("echo %d: %s\n", n, buf);
			bzero(buf, sizeof(buf));

			c -= n;
			if (c <= 0) {
				break;
			}
		}
	}
	close(sockfd);
}

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
#include <utility>
#include <chrono>
#include <stdlib.h>
#include <optional>
#include "TcpSpi.hpp"
#include "TcpApi.hpp"


#define DEFAULT_PORT    8080

void client_run();

struct Request {
	uint64_t timestamp;
	std::string payload;
};

struct Response {
	uint64_t timestamp;
	std::string payload;
};

#pragma pack(push, 1)
struct Data {
	uint64_t id;
	uint32_t cvl;
	uint32_t cto;
	uint32_t lpr;
	uint32_t opx;
	double cpx;
	uint32_t cpx_len;
	uint32_t opx_len;
	int32_t bp[5];
	int32_t ap[5];
	int32_t bs[5];
	int32_t as[5];
};
#pragma pack(pop)


uint64_t getMicroTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}


size_t encoder(uint8_t type, const char* data, uint32_t data_len, char* output, size_t out_size) {
    if (out_size < 5 + data_len || !output)
        return 0;

    output[0] = static_cast<char>(type);
	auto total_len = 5 + data_len;
    std::memcpy(output + 1, &total_len, 4);

    for (uint32_t i = 0; i < data_len; ++i) {
        output[5 + i] = static_cast<char>(data[i] + (i % 2 ? 0x51 : 0x4A));
    }
    return total_len;
}


class Coder {
public:
	std::pair<uint8_t, int> getMessageTypeLength(const char * data) {
		if (data == nullptr) {
			std::abort();
		}
		uint8_t type = static_cast<uint8_t>(data[0]);
		uint32_t len;
		std::memcpy(&len, data + 1, 4);
		return std::make_pair(type, len);
	}

	std::optional<Request> tryDecode(SimpleBuffer& buffer) {
		if (buffer.size() < kHeaderLen) {
			return {};
		}
		const char* data = buffer.data();
		auto [type, messageLen] = getMessageTypeLength(data);
		if (type != 2) {
			std::printf("Unsupported message type: %d\n", type);
			// close connection
			return {};
		}
		if (messageLen < 0) {
			printf("Invalid message length\n");
		} else if (buffer.size() >= messageLen) {
			buffer.advance(kHeaderLen);
			printf("Received type: %d, length: %d\n", type, messageLen);
			Request req = decode(buffer.data(), messageLen - kHeaderLen);
			// printf("Decoded timestamp: %lu, payload: %s\n", req.timestamp, req.payload.c_str());
			buffer.advance(messageLen - kHeaderLen);
			return req;
		} else {
			printf("Not enough data for a complete message\n");
			return {};
		}
	}

	void encodeResponse(Response& response, std::vector<char>& output) {
		output.resize(13 + response.payload.size());

		output[0] = 3; // type
		uint32_t total_len = 13 + response.payload.size();
		std::memcpy(output.data() + 1, &total_len, 4); // total length
		// std::memcpy(output + 1, &total_len, 4);
		auto current_time = getMicroTimestamp();
		// std::memcpy(output.data() + 5, &current_time, sizeof(current_time));
		for (int i = 0; i < 8; ++i) {
			output[5 + i] = static_cast<char>((current_time >> (i * 8)) + (i % 2 ? 0x51 : 0x4A));
		}

		for (size_t i = 0; i < response.payload.size(); ++i) {
			output[13 + i] = static_cast<char>(response.payload[i] + (i % 2 ? 0x51 : 0x4A));
		}
	}

	void encodeData(Data& data, std::vector<char>& output) {
		output.resize(sizeof(Data) + 5);
		output[0] = 1; // type
		uint32_t total_len = sizeof(Data) + 5;
		std::memcpy(output.data() + 1, &total_len, 4);
		std::memcpy(output.data() + 5, &data, sizeof(Data));
		for (size_t i = 0; i < sizeof(Data); ++i) {
			output[i + 5] += 0; // i % 2 ? 0x51 : 0x4A;
		}
	}

private:
	Request decode(const char* data, size_t length) {
		if (!data) 
			return Request();
		
		Request request;

		uint64_t decoded_timestamp = 0;
		for (int i = 0; i < 8; ++i) {
			uint8_t decoded_byte = data[i] -  (i % 2 ? 0x51 : 0x4A);
			decoded_timestamp |= (static_cast<uint64_t>(decoded_byte) << (i * 8));
		}
		request.timestamp = decoded_timestamp;
		
		const size_t payload_length = length - 8;
		request.payload.resize(payload_length);
		
		char* payload_ptr = &request.payload[0];
		for (size_t i = 8; i < length; ++i) {
			payload_ptr[i - 8] = data[i] -  (i % 2 ? 0x51 : 0x4A);
		}
		return request;
	}
private:
	constexpr static size_t kHeaderLen = 5;
};

class CubeServer: public TcpSpi {
public:
	void onAccepted(std::shared_ptr<Connection> conn) override {
	}

	void onDisconnected(std::shared_ptr<Connection> conn, int reason, const char* reason_str) override {
	}

	void onMessage(std::shared_ptr<Connection> conn, const char* data, size_t len) override {
		buffer_.write(data, len);
		while (auto req = coder_.tryDecode(buffer_)) {
			printf("Decoded timestamp: %lu, payload: %s\n", req->timestamp, req->payload.c_str());
			Response response = generateEchoBackResponse(*req);
			
			// std::vector<char> response_buffer;
			// coder_.encodeResponse(response, response_buffer);
			// conn->send(response_buffer.data(), response_buffer.size());

			std::vector<char> response_buffer;
			Data data_to_send {
				1,2,3,4,5,6,7,8,
				{9, 10, 11, 12, 13},
				{14, 15, 14, 13, 12},
				{11, 10, 9, 8, 7},
				{6, 5, 4, 3, 2},
			};
			coder_.encodeData(data_to_send, response_buffer);
			conn->send(response_buffer.data(), response_buffer.size());
		}
	}
private:
	Response generateEchoBackResponse(const Request& req) {
		Response response;
		response.timestamp = getMicroTimestamp();
		response.payload.resize(req.payload.size() + 8);
		// write req time stamp and payload to response's payload
		std::memcpy(response.payload.data(), &req.timestamp, sizeof(req.timestamp));
		std::memcpy(response.payload.data() + 8, req.payload.data(), req.payload.size());
		return response;
	}
private:
	Coder coder_;
	SimpleBuffer buffer_;
};

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
	CubeServer c;
	api.registerSpi(&c);
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
		uint64_t t = getMicroTimestamp();
		std::memcpy(buf, &t, sizeof(uint64_t));
		char ch;
		int num;
		std::cin >> ch >> num;
		for (int i = 0; i < num; ++i) {
			buf[8 + i] = ch;
		}
		char out[1024];
		auto n = encoder(2, buf, 8 + num, out, sizeof(out));
		printf("send type: %d, len: %d\n", 2, n);
		write(sockfd, out, n);

		bzero(buf, sizeof(buf));
		n = read(sockfd, buf, sizeof(buf));
		printf("echo length %d\n", n);
		
		Data data = *reinterpret_cast<Data*>(buf + 5);
		// print data struct
		printf("id: %lu, cvl: %u, cto: %u, lpr: %u, opx: %u, cpx: %f, cpx_len: %u, opx_len: %u\n",
			data.id, data.cvl, data.cto, data.lpr, data.opx, data.cpx, data.cpx_len, data.opx_len);
		printf("bp: %d %d %d %d %d\n",
			data.bp[0], data.bp[1], data.bp[2], data.bp[3], data.bp[4]);
		printf("ap: %d %d %d %d %d\n",
			data.ap[0], data.ap[1], data.ap[2], data.ap[3], data.ap[4]);
		printf("bs: %d %d %d %d %d\n",
			data.bs[0], data.bs[1], data.bs[2], data.bs[3], data.bs[4]);
		printf("as: %d %d %d %d %d\n",
			data.as[0], data.as[1], data.as[2], data.as[3], data.as[4]);
		bzero(buf, sizeof(buf));
	}
	close(sockfd);
}
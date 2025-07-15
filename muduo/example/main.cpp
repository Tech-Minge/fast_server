
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
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/async.h"

#define DEFAULT_PORT    8080

void client_run();

struct Request {
	uint64_t timestamp;
	std::string payload;
};

struct Response {
	uint64_t timestamp;
	Request request;
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

	std::pair<int, bool> decode(SimpleBuffer& buffer) {
		if (buffer.size() < kHeaderLen) {
			return {0, false};
		}
		const char* data = buffer.data();
		auto [type, messageLen] = getMessageTypeLength(data);
		if (type != 2) {
			// spdlog::warn("Unexpected message type: {}", type);
			// close connection
			return {0, false};
		}
		if (messageLen < 0) {
			// spdlog::warn("Invalid message length: {}", messageLen);
			return {0, false};
		} else if (buffer.size() >= messageLen) {
			buffer.advance(kHeaderLen);
			auto timestamp = decodeTimeStamp(buffer.data(), messageLen - kHeaderLen);
            auto timediff = getMicroTimestamp() - timestamp;
            spdlog::warn("timeSinceSend cost: {}us", timediff);
			return {messageLen - kHeaderLen, true};
		} else {
            // spdlog::warn("Buffer size {} is less than expected message length {}", buffer.size(), messageLen);
			return {0, false};
		}
	}

	void encodeHeaderWithTimeStamp(std::vector<char>& output, uint32_t totalLen) {
		ScopedTimer timer(__func__);
		output[0] = 3;
		std::memcpy(output.data() + 1, &totalLen, 4);
		auto current_time = getMicroTimestamp();

		constexpr uint8_t kOffsets[2] = {0x4A, 0x51};

		for (int i = 0; i < 8; ++i) {
			output[5 + i] = static_cast<char>((current_time >> (i * 8)) + kOffsets[i & 1]);
		}
	}

	void encodeData(Data& data, std::vector<char>& output) {
		output.resize(sizeof(Data) + 5);
		output[0] = 1; // type
		uint32_t total_len = sizeof(Data) + 5;
		std::memcpy(output.data() + 1, &total_len, 4);
		std::memcpy(output.data() + 5, &data, sizeof(Data));
		for (size_t i = 0; i < sizeof(Data); ++i) {
			output[i + 5] += i % 2 ? 0x51 : 0x4A;
		}
	}

private:
	uint64_t decodeTimeStamp(const char* data, size_t length) {
        ScopedTimer timer(__func__);
		if (!data) {
			std::abort();
        }
		uint64_t decoded_timestamp = 0;
		for (int i = 0; i < 8; ++i) {
			uint8_t decoded_byte = data[i] -  (i % 2 ? 0x51 : 0x4A);
			decoded_timestamp |= (static_cast<uint64_t>(decoded_byte) << (i * 8));
		}
		
		return decoded_timestamp;
	}
private:
	constexpr static size_t kHeaderLen = 5;
};

class CubeServer: public TcpSpi {
public:
	void onAccepted(std::shared_ptr<Connection> conn) override {
        spdlog::info("onAccepted called with fd: {}", conn->fdWrapper().fd());
        auto id = conn->registerTimer(10000, [conn] {
            // heartbeat
            spdlog::info("heartbeat");
        }, true);
        timerId_ = id;
        spdlog::info("Connection accepted with timer ID: {}", id);
	}

	void onDisconnected(std::shared_ptr<Connection> conn, int reason, const char* reason_str) override {
        bool res = conn->cancelTimer(timerId_);
        spdlog::info("Connection disconnected with timer ID: {}, res: {}", timerId_, res);
	}

	void onMessage(std::shared_ptr<Connection> conn, const char* data, size_t len) override {
		// write connection's private buffer
        buffer_.write(data, len);
		{
            ScopedTimer timer("onMessageLoop");
            for (;;) {
                auto [messageBodyLen, valid] = coder_.decode(buffer_);
                if (!valid) {
                    break; // 退出循环，等待下次消息
                }
                auto sendLen = messageBodyLen + 5 + 8;
                std::vector<char> header(13);
                coder_.encodeHeaderWithTimeStamp(header, messageBodyLen);
                {
                    ScopedTimer timer("sendData");
                    conn->send(header.data(), header.size());
                    conn->send(buffer_.data(), messageBodyLen);
                }
                
                buffer_.advance(messageBodyLen);
            }
        }
	}
private:
	Coder coder_;
	SimpleBuffer buffer_;
    uint64_t timerId_;
};


/*
void onAccepted(std::shared_ptr<Connection> conn) override {
    int fd = conn->fdWrapper().fd();
    lastActiveMap_[fd] = std::chrono::steady_clock::now();

    auto id = conn->registerTimer(10000, [this, conn, fd] {
        auto now = std::chrono::steady_clock::now();
        auto lastActive = lastActiveMap_[fd];
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActive).count();

        spdlog::info("heartbeat, idle for {}s", elapsed);

        if (elapsed > 30) {  // 超过 30 秒未活动，断开连接
            spdlog::warn("Closing idle connection: fd={}", fd);
            conn->close();  // 触发 onDisconnected
        }
    }, true);
    timerId_ = id;
}


void onMessage(std::shared_ptr<Connection> conn, const char* data, size_t len) override {
    // 更新活跃时间
    lastActiveMap_[conn->fdWrapper().fd()] = std::chrono::steady_clock::now();

    buffer_.write(data, len);
    // ... 保持原有逻辑 ...
}

// 可定义在 Connection 或 CubeServer 的某个映射中
std::unordered_map<int, std::chrono::steady_clock::time_point> lastActiveMap_;



#include <chrono>
#include <memory>
#include <unordered_map>
#include <spdlog/spdlog.h>

class CubeServer : public TcpSpi {
public:
    void onAccepted(std::shared_ptr<Connection> conn) override {
        spdlog::info("onAccepted called with fd: {}", conn->fdWrapper().fd());
        
        // 为连接创建状态管理对象
        auto connState = std::make_shared<ConnectionState>();
        connStates_[conn->fdWrapper().fd()] = connState;
        
        // 注册心跳定时器
        auto heartbeatTimerId = conn->registerTimer(10000, [conn, connState] {
            // 检查是否超时
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - connState->lastActivityTime
            );
            
            if (elapsed.count() > 30000) { // 30秒超时
                spdlog::warn("Connection timeout detected, fd: {}", conn->fdWrapper().fd());
                conn->close(); // 主动关闭连接
                return;
            }
            
            // 发送心跳消息
            const char heartbeatMsg[] = "\x08HEARTBEAT"; // 自定义心跳协议
            conn->send(heartbeatMsg, sizeof(heartbeatMsg) - 1);
            spdlog::debug("Heartbeat sent to fd: {}", conn->fdWrapper().fd());
        }, true); // 重复执行的定时器
        
        connState->heartbeatTimerId = heartbeatTimerId;
        spdlog::info("Connection accepted with heartbeat timer ID: {}", heartbeatTimerId);
    }

    void onDisconnected(std::shared_ptr<Connection> conn, int reason, const char* reason_str) override {
        int fd = conn->fdWrapper().fd();
        auto it = connStates_.find(fd);
        if (it != connStates_.end()) {
            // 取消心跳定时器
            bool res = conn->cancelTimer(it->second->heartbeatTimerId);
            spdlog::info("Connection disconnected fd: {}, cancel timer: {}, result: {}", 
                         fd, it->second->heartbeatTimerId, res);
            
            // 清理连接状态
            connStates_.erase(it);
        } else {
            spdlog::warn("Connection state not found for fd: {}", fd);
        }
    }

    void onMessage(std::shared_ptr<Connection> conn, const char* data, size_t len) override {
        int fd = conn->fdWrapper().fd();
        auto it = connStates_.find(fd);
        if (it == connStates_.end()) {
            spdlog::error("Connection state missing for fd: {}", fd);
            return;
        }
        
        // 更新最后活动时间
        it->second->lastActivityTime = std::chrono::steady_clock::now();
        
        // 处理心跳响应
        if (len == 4 && memcmp(data, "PONG", 4) == 0) {
            spdlog::debug("Received PONG from fd: {}", fd);
            return; // 不需要进一步处理心跳响应
        }
        
        // 处理正常业务消息
        buffer_.write(data, len);
        {
            ScopedTimer timer("onMessageLoop");
            for (;;) {
                auto [messageBodyLen, valid] = coder_.decode(buffer_);
                if (!valid) {
                    break; // 退出循环，等待下次消息
                }
                auto sendLen = messageBodyLen + 5 + 8;
                std::vector<char> header(13);
                coder_.encodeHeaderWithTimeStamp(header, messageBodyLen);
                {
                    ScopedTimer timer("sendData");
                    conn->send(header.data(), header.size());
                    conn->send(buffer_.data(), messageBodyLen);
                }
                
                buffer_.advance(messageBodyLen);
            }
        }
    }

private:
    // 连接状态管理
    struct ConnectionState {
        std::chrono::steady_clock::time_point lastActivityTime;
        uint64_t heartbeatTimerId;
        
        ConnectionState() : lastActivityTime(std::chrono::steady_clock::now()) {}
    };
    
    std::unordered_map<int, std::shared_ptr<ConnectionState>> connStates_;
    Coder coder_;
    SimpleBuffer buffer_;
};






#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

// 模拟连接对象
class Connection {
public:
    void sendData(const std::string& data) {
        std::cout << "Sending data: " << data << " via Connection " << this << std::endl;
    }
};

// ClassB：消费者，处理连接对象
class ClassB {
private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::shared_ptr<Connection>> conn_queue_;
    bool stop_flag_ = false;

public:
    // 后台线程运行函数
    void run() {
        while (true) {
            std::shared_ptr<Connection> conn;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                // 等待唤醒条件：队列非空或停止信号
                cv_.wait(lock, [this] { return !conn_queue_.empty() || stop_flag_; });
                if (stop_flag_ && conn_queue_.empty()) break;

                conn = conn_queue_.front();
                conn_queue_.pop();
            }

            // 处理连接：发送数据
            conn->sendData("Processed by ClassB");
            std::cout << "ClassB: Data sent. Going back to sleep..." << std::endl;
        }
    }

    // 添加连接并唤醒线程
    void addConnection(std::shared_ptr<Connection> conn) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            conn_queue_.push(conn);
        }
        cv_.notify_one(); // 唤醒处理线程
    }

    // 停止线程
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_flag_ = true;
        }
        cv_.notify_one();
    }
};

// ClassA：生产者，生成连接对象并提交给ClassB
class ClassA {
private:
    ClassB& class_b_; // 持有ClassB的引用

public:
    ClassA(ClassB& b) : class_b_(b) {}

    void generateConnection() {
        auto conn = std::make_shared<Connection>();
        std::cout << "ClassA: Generated Connection " << conn.get() << std::endl;
        class_b_.addConnection(conn); // 提交连接并唤醒ClassB
    }
};

int main() {
    ClassB class_b;
    ClassA class_a(class_b);

    // 启动ClassB的后台线程
    std::thread worker_thread([&class_b] { class_b.run(); });

    // 模拟生产连接
    for (int i = 0; i < 3; ++i) {
        class_a.generateConnection();
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 模拟生产间隔
    }

    // 停止ClassB线程
    class_b.stop();
    worker_thread.join();
    return 0;
}
*/

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
	auto logger = spdlog::create_async_nb<spdlog::sinks::basic_file_sink_mt>("async_logger", "log/muduo.log");
	spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_every(std::chrono::seconds(3));
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
	struct sockaddr_in srv_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	set_sockaddr(&srv_addr, "127.0.0.1", DEFAULT_PORT);

	if (connect(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
		perror("connect()");
		exit(1);
	}
	for (;;) {
		printf("input: ");

		char ch;
		int num;
		std::cin >> ch >> num;
		char* buf = new char[100 + num];

		uint64_t t = getMicroTimestamp();
		std::memcpy(buf, &t, sizeof(uint64_t));
		
		for (int i = 0; i < num; ++i) {
			buf[8 + i] = ch;
		}
		char* out = new char[100 + num];
		auto n = encoder(2, buf, 8 + num, out, 100 + num + 5);
		auto currentTime = getMicroTimestamp();
		int act = write(sockfd, out, n);
        printf("send len: %d\n", act);

		int expect = n + 5;
		bzero(buf, sizeof(buf));
		n = read(sockfd, buf, 100 + num);
		printf("echo length %d\n", n);
		auto endTime = getMicroTimestamp();
		// print latency in ms
		printf("-----------latency: %ld us ----------\n", (endTime - currentTime) / 1);
		
		bzero(buf, sizeof(buf));

		delete[] buf;
		delete[] out;
	}
	close(sockfd);
}

/*

Request decode(const char* data, size_t length) {
    if (!data || length < 8)
        return Request();

    Request request;

    // 时间戳解码 - 手动展开 + 预计算
    const uint8_t offsets[] = {0x4A, 0x51, 0x4A, 0x51, 0x4A, 0x51, 0x4A, 0x51};
    uint64_t decoded_timestamp = 0;
    decoded_timestamp |= static_cast<uint64_t>(data[0] - offsets[0]) << 0;
    decoded_timestamp |= static_cast<uint64_t>(data[1] - offsets[1]) << 8;
    decoded_timestamp |= static_cast<uint64_t>(data[2] - offsets[2]) << 16;
    decoded_timestamp |= static_cast<uint64_t>(data[3] - offsets[3]) << 24;
    decoded_timestamp |= static_cast<uint64_t>(data[4] - offsets[4]) << 32;
    decoded_timestamp |= static_cast<uint64_t>(data[5] - offsets[5]) << 40;
    decoded_timestamp |= static_cast<uint64_t>(data[6] - offsets[6]) << 48;
    decoded_timestamp |= static_cast<uint64_t>(data[7] - offsets[7]) << 56;
    request.timestamp = decoded_timestamp;

    // Payload 解码 - 使用 std::transform
    const size_t payload_length = length - 8;
    request.payload.resize(payload_length);

    std::transform(data + 8, data + length, request.payload.begin(),
        [i = 0U](char c) mutable {
            return c - ((i++ % 2) ? 0x51 : 0x4A);
        });

    return request;
}


#include <atomic>
#include <memory>
#include <iostream>

template<typename T>
class LockFreeQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;

        Node() : next(nullptr) {}
        explicit Node(T value) : data(value), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head;  // 避免伪共享
    alignas(64) std::atomic<Node*> tail;

public:
    LockFreeQueue() {
        // 初始化虚拟节点（Dummy Node）
        Node* dummy = new Node();
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        while (pop());  // 清空队列
        delete head.load();  // 释放虚拟节点
    }

    // 入队操作
    void add(T value) {
        Node* new_node = new Node(value);
        Node* current_tail = nullptr;
        Node* next = nullptr;

        while (true) {
            current_tail = tail.load(std::memory_order_acquire);
            next = current_tail->next.load(std::memory_order_acquire);

            // 检查tail是否被其他线程更新
            if (current_tail != tail.load(std::memory_order_relaxed)) 
                continue;

            // 若next非空，说明其他线程已更新tail但未完成链接
            if (next != nullptr) {
                tail.compare_exchange_weak(
                    current_tail, next, 
                    std::memory_order_release, std::memory_order_relaxed
                );
                continue;
            }

            // 尝试链接新节点到尾部
            if (current_tail->next.compare_exchange_weak(
                next, new_node, 
                std::memory_order_release, std::memory_order_relaxed
            )) {
                break;
            }
        }

        // 更新tail指针（允许失败，其他线程会协助更新）
        tail.compare_exchange_weak(
            current_tail, new_node, 
            std::memory_order_release, std::memory_order_relaxed
        );
    }

    // 出队操作
    bool pop(T& result) {
        Node* current_head = nullptr;
        Node* current_tail = nullptr;
        Node* next = nullptr;

        while (true) {
            current_head = head.load(std::memory_order_acquire);
            current_tail = tail.load(std::memory_order_acquire);
            next = current_head->next.load(std::memory_order_acquire);

            // 检查队列是否为空或head是否过时
            if (current_head == head.load(std::memory_order_relaxed)) {
                if (current_head == current_tail) {
                    if (next == nullptr) return false;  // 队列为空
                    // 协助更新tail
                    tail.compare_exchange_weak(
                        current_tail, next, 
                        std::memory_order_release, std::memory_order_relaxed
                    );
                } else {
                    result = next->data;  // 读取数据
                    // 尝试移动head指针
                    if (head.compare_exchange_weak(
                        current_head, next, 
                        std::memory_order_release, std::memory_order_relaxed
                    )) {
                        delete current_head;  // 释放旧头节点
                        return true;
                    }
                }
            }
        }
    }
};





#include <vector>
#include <memory>
#include <cstddef>
#include <cstring>
#include <sys/uio.h> // For struct iovec
#include <unistd.h>  // For readv, writev

// 内存块描述符
struct MemoryChunk {
    void* data;        // 内存块起始地址
    size_t size;       // 内存块大小
    size_t used = 0;   // 已使用字节数
    bool owned = true; // 是否拥有内存所有权

    MemoryChunk(void* d, size_t s, bool own = true)
        : data(d), size(s), owned(own) {}
    
    ~MemoryChunk() {
        if (owned && data) {
            free(data);
            data = nullptr;
        }
    }
};

// 分段连续零拷贝缓冲区
class ScatterGatherBuffer {
public:
    // 添加外部内存块（零拷贝）
    void add_external_chunk(void* data, size_t size) {
        chunks_.emplace_back(new MemoryChunk(data, size, false));
        total_size_ += size;
    }

    // 分配新内存块
    MemoryChunk* allocate_chunk(size_t size) {
        void* new_data = malloc(size);
        if (!new_data) return nullptr;
        
        chunks_.emplace_back(new MemoryChunk(new_data, size));
        total_size_ += size;
        return chunks_.back().get();
    }

    // 获取当前写位置
    void* current_write_position() const {
        if (chunks_.empty()) return nullptr;
        auto& last = chunks_.back();
        return static_cast<char*>(last->data) + last->used;
    }

    // 获取剩余可写空间
    size_t remaining_space() const {
        if (chunks_.empty()) return 0;
        auto& last = chunks_.back();
        return last->size - last->used;
    }

    // 提交写入数据
    void commit_write(size_t bytes) {
        if (chunks_.empty()) return;
        
        auto& last = chunks_.back();
        const size_t actual = std::min(bytes, last->size - last->used);
        last->used += actual;
        total_used_ += actual;
    }

    // 准备 gather I/O 结构
    std::vector<iovec> prepare_gather_io() const {
        std::vector<iovec> vec;
        vec.reserve(chunks_.size());
        
        for (const auto& chunk : chunks_) {
            if (chunk->used == 0) continue;
            
            iovec iov;
            iov.iov_base = chunk->data;
            iov.iov_len = chunk->used;
            vec.push_back(iov);
        }
        
        return vec;
    }

    // 准备 scatter I/O 结构
    std::vector<iovec> prepare_scatter_io() {
        std::vector<iovec> vec;
        vec.reserve(chunks_.size());
        
        for (auto& chunk : chunks_) {
            if (chunk->used == chunk->size) continue;
            
            iovec iov;
            iov.iov_base = static_cast<char*>(chunk->data) + chunk->used;
            iov.iov_len = chunk->size - chunk->used;
            vec.push_back(iov);
        }
        
        return vec;
    }

    // 执行 gather write
    ssize_t gather_write(int fd) {
        auto iovs = prepare_gather_io();
        return writev(fd, iovs.data(), iovs.size());
    }

    // 执行 scatter read
    ssize_t scatter_read(int fd) {
        auto iovs = prepare_scatter_io();
        ssize_t bytes_read = readv(fd, iovs.data(), iovs.size());
        if (bytes_read > 0) {
            // 更新缓冲区使用情况
            size_t remaining = bytes_read;
            for (auto& chunk : chunks_) {
                if (remaining == 0) break;
                
                const size_t space = chunk->size - chunk->used;
                const size_t to_use = std::min(space, static_cast<size_t>(remaining));
                
                chunk->used += to_use;
                total_used_ += to_use;
                remaining -= to_use;
            }
        }
        return bytes_read;
    }

    // 获取总数据量
    size_t total_size() const { return total_size_; }
    size_t total_used() const { return total_used_; }

    // 拼接所有数据到连续内存 (可选)
    std::vector<char> flatten() const {
        std::vector<char> result(total_used_);
        char* ptr = result.data();
        
        for (const auto& chunk : chunks_) {
            if (chunk->used == 0) continue;
            memcpy(ptr, chunk->data, chunk->used);
            ptr += chunk->used;
        }
        
        return result;
    }

private:
    std::vector<std::unique_ptr<MemoryChunk>> chunks_;
    size_t total_size_ = 0;   // 总容量
    size_t total_used_ = 0;   // 已使用字节
};



#include <memory>
#include <functional>

class SegmentBuffer {
public:
    // 添加外部内存并转移所有权
    void add_segment(std::unique_ptr<uint8_t[]> data, size_t size) {
        segments_.emplace_back(Segment{
            .data = data.release(),
            .size = size,
            .deleter = [](void* ptr) { delete[] static_cast<uint8_t*>(ptr); }
        });
    }

    // 添加外部内存并指定自定义删除器
    void add_segment(void* data, size_t size, std::function<void(void*)> deleter) {
        segments_.emplace_back(Segment{
            .data = data,
            .size = size,
            .deleter = std::move(deleter)
        });
    }

    ~SegmentBuffer() {
        for (auto& seg : segments_) {
            if (seg.deleter) {
                seg.deleter(seg.data);
            }
        }
    }

private:
    struct Segment {
        void* data;
        size_t size;
        std::function<void(void*)> deleter;
    };

    std::vector<Segment> segments_;
};


#include <vector>
#include <mutex>
#include <unordered_set>

class MemoryPool {
public:
    // 分配内存并注册到池中
    void* allocate(size_t size) {
        void* mem = ::malloc(size);
        if (!mem) return nullptr;
        
        std::lock_guard<std::mutex> lock(mutex_);
        allocated_.insert(mem);
        return mem;
    }

    // 释放内存（仅当在池中注册过）
    void deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (allocated_.find(ptr) != allocated_.end()) {
            ::free(ptr);
            allocated_.erase(ptr);
        }
    }

    // 显式注册外部内存
    void register_external(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        external_.insert(ptr);
    }

    // 取消注册外部内存
    void unregister_external(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        external_.erase(ptr);
    }

    // 检查内存是否受管理
    bool is_managed(void* ptr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocated_.find(ptr) != allocated_.end() || 
               external_.find(ptr) != external_.end();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_set<void*> allocated_;
    std::unordered_set<void*> external_;
};

class SegmentBuffer {
public:
    SegmentBuffer(MemoryPool& pool) : pool_(pool) {}

    void add_segment(void* data, size_t size) {
        pool_.register_external(data);
        segments_.push_back({data, size});
    }

    ~SegmentBuffer() {
        for (auto& seg : segments_) {
            pool_.unregister_external(seg.data);
        }
    }

private:
    MemoryPool& pool_;
    std::vector<std::pair<void*, size_t>> segments_;
};

*/
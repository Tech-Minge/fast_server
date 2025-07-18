sudo apt-get install libsnappy-dev

sudo yum install snappy-devel

git submodule add https://github.com/google/snappy.git third_party/snappy



在 C++ 中实现 FIX 协议的序列化，推荐使用 QuickFIX 开源库。它提供了完整的 FIX 协议支持，包括消息构建、序列化、会话管理和网络通信。以下是具体实现步骤和示例：

🔧 1. 安装 QuickFIX 库

• 下载源码：从 https://github.com/quickfix/quickfix 获取最新版本。

• 编译安装：
  # 编译安装
  ./configure
  make
  sudo make install
  

📝 2. 序列化流程（核心代码示例）

(1) 创建 FIX 消息对象

使用 FIX::Message 或子类（如 FIX::NewOrderSingle）构建消息：
#include <quickfix/Application.h>
#include <quickfix/Message.h>
#include <quickfix/field/MsgType.h>
#include <quickfix/field/ClOrdID.h>
#include <quickfix/field/Symbol.h>
#include <quickfix/field/Side.h>
#include <quickfix/field/OrdType.h>
#include <quickfix/field/Price.h>
#include <quickfix/field/OrderQty.h>

// 创建新订单消息（FIX 4.4）
FIX44::NewOrderSingle order;
order.set(FIX::ClOrdID("ORDER123"));       // 客户端订单ID
order.set(FIX::Symbol("AAPL"));            // 交易标的
order.set(FIX::Side(FIX::Side_BUY));       // 买卖方向（BUY）
order.set(FIX::OrdType(FIX::OrdType_LIMIT));// 订单类型（限价单）
order.set(FIX::Price(150.0));               // 价格
order.set(FIX::OrderQty(100));              // 数量


(2) 序列化为字符串

调用 toString() 将消息转为 FIX 协议格式的字符串：
#include <quickfix/Message.h>

// 序列化为字符串（含 SOH 分隔符）
std::string fixMessage = order.toString();
// 输出示例：8=FIX.4.4|9=...|35=D|11=ORDER123|55=AAPL|54=1|40=2|44=150.0|38=100|...


(3) 自定义字段扩展

支持自定义 Tag（5000-9999 范围）：
order.setField(5001, "CustomValue"); // 自定义字段


⚙️ 3. 关键配置

(1) 数据字典（DataDictionary）

• 作用：验证消息格式和字段合法性。

• 配置：
  #include <quickfix/DataDictionary.h>
  FIX::DataDictionary dd("FIX44.xml"); // 加载协议版本定义
  order.validate(dd); // 验证消息
  

(2) 会话设置（SessionSettings）

• 文件配置：定义会话参数（如协议版本、心跳间隔）：
  # settings.cfg
  [DEFAULT]
  ConnectionType=initiator
  ReconnectInterval=30
  [SESSION]
  BeginString=FIX.4.4
  SenderCompID=CLIENT
  TargetCompID=SERVER
  
• 代码加载：
  #include <quickfix/SessionSettings.h>
  FIX::SessionSettings settings("settings.cfg");
  

🛠️ 4. 高级特性

1. 消息工厂（MessageFactory）  
   支持动态创建消息类型：
   FIX::MessageFactory factory;
   FIX::Message* msg = factory.create("FIX.4.4", "D"); // 创建 NewOrderSingle
   

2. 二进制序列化优化  
   • 默认使用文本格式（Tag=Value），可通过修改配置支持二进制格式（如 RawData 字段）。

   • 示例：使用 FIX::FieldBase 直接操作二进制数据。

3. 性能调优  
   • 内存存储：MemoryStoreFactory 替代默认的 FileStoreFactory 提升速度。

   • 异步处理：配置 ThreadedSocketAcceptor 避免阻塞。

⚠️ 5. 注意事项

1. 协议版本兼容性  
   • 不同版本（如 FIX 4.2 与 4.4）字段差异大，需严格匹配 DataDictionary。

   • 示例：FIX 4.2 中成交数量用 LastShares（Tag 32），而 4.4 用 LastQty（Tag 151）。

2. 字段校验  
   必填字段缺失会抛出异常（如 MsgType/ClOrdID）：
   try { order.validate(dd); }
   catch (FIX::RequiredTagMissing& e) { /* 处理缺失字段 */ }
   

3. 安全性  
   • 敏感字段（如密码）需加密传输（配置 SocketUseSSL=true）。

   • 避免在日志中输出完整消息（可能含敏感数据）。

💎 总结

• 推荐库：QuickFIX 是 C++ 实现 FIX 序列化的最优解，覆盖协议解析、会话管理、网络通信。

• 核心步骤：构建消息 → 调用 toString() 序列化 → 通过 DataDictionary 校验。

• 适用场景：证券交易系统、行情网关、订单路由引擎等。

完整示例见 https://www.quickfixengine.org/。进阶需求可参考协议规范 https://www.fixtrading.org/。




在C++中，使用开源库实现FIX协议的序列化可以通过 **QuickFIX**（一个开源的FIX协议库）来完成。QuickFIX 是一个成熟的开源库，支持 FIX 4.0 到 FIX 5.0 的多种版本，并提供了完整的会话管理、消息序列化/反序列化功能。以下是使用 QuickFIX 实现 FIX 协议序列化的步骤和示例代码：

---

### **1. 安装 QuickFIX**
首先，需要安装 QuickFIX 库。可以通过包管理器或从源码编译安装：

- **Linux (Ubuntu/Debian)**:
  ```bash
  sudo apt-get install libquickfix-dev
  ```

- **macOS (Homebrew)**:
  ```bash
  brew install quickfix
  ```

- **Windows**:
  - 下载源码：https://github.com/quickfix/quickfix
  - 使用 CMake 编译并安装。

---

### **2. 配置 FIX 会话参数**
QuickFIX 使用配置文件定义会话参数。例如，创建一个 `config.cfg` 文件：
```ini
[DEFAULT]
ConnectionType=initiator
SenderCompID=CLIENT
TargetCompID=SERVER
HeartBtInt=30
DataDictionary=path/to/FIX50SP2.xml
FileStorePath=./store
FileLogPath=./log
StartTime=00:00:00
EndTime=23:59:59
ResetOnLogon=Y

[SESSION]
BeginString=FIX.5.0SP2
SocketConnectHost=127.0.0.1
SocketConnectPort=5001
```

- **`DataDictionary`**：指定 FIX 协议版本的 XML 数据字典（可以从 QuickFIX 的示例中获取）。
- **`ConnectionType`**：设置为 `initiator`（客户端）或 `acceptor`（服务端）。

---

### **3. 实现消息处理逻辑**
需要定义一个类继承自 `quickfix/MessageCracker`，用于处理接收到的 FIX 消息。

#### **示例代码：消息处理器**
```cpp
#include <quickfix/MessageCracker.h>
#include <quickfix/Values.h>
#include <quickfix/Message.h>
#include <iostream>

class MyApplication : public FIX::MessageCracker, public FIX::Application {
public:
    void fromApplication(const FIX::Message& message, const FIX::SessionID&) override {
        crack(message); // 根据消息类型调用对应的处理函数
    }

    void onMessage(const FIX42::NewOrderSingle& message, const FIX::SessionID&) {
        std::cout << "Received NewOrderSingle:" << std::endl;
        std::cout << "  ClOrdID: " << message.getClOrdID() << std::endl;
        std::cout << "  Symbol: " << message.getSymbol() << std::endl;
        std::cout << "  OrderQty: " << message.getOrderQty() << std::endl;
    }

    // 其他消息类型的处理函数...
};
```

---

### **4. 初始化 QuickFIX 并发送消息**
使用 QuickFIX 的 `FIX::Initiator` 或 `FIX::Acceptor` 来启动会话，并发送 FIX 消息。

#### **示例代码：客户端发送订单**
```cpp
#include <quickfix/Session.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/MessageFactory.h>

int main() {
    try {
        // 加载配置文件
        FIX::SessionSettings settings("config.cfg");
        MyApplication application;
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);

        // 启动会话
        initiator.start();

        // 等待登录完成
        while (!FIX::Session::isLoggedOn()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // 构造并发送 NewOrderSingle 消息
        FIX42::NewOrderSingle order;
        order.set(FIX::ClOrdID("12345"));
        order.set(FIX::Symbol("AAPL"));
        order.set(FIX::Side('1')); // Buy
        order.set(FIX::TransactTime(FIX::UTCTimestamp());
        order.set(FIX::OrderQty(100));
        order.set(FIX::Price(150.00));
        order.set(FIX::OrdType('2')); // Limit

        FIX::SessionID sessionID = FIX::Session::lookupSessionID("FIX.5.0SP2");
        FIX::Session::sendToTarget(order, sessionID);

        // 等待一段时间后停止
        std::this_thread::sleep_for(std::chrono::seconds(10));
        initiator.stop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

---

### **5. 编译和运行**
确保在编译时链接 QuickFIX 库。例如，使用 g++ 编译：
```bash
g++ -std=c++11 -o fix_client main.cpp -lquickfix
```

运行程序：
```bash
./fix_client
```

---

### **6. 关键点说明**
1. **FIX 消息序列化**：
   - QuickFIX 自动将 FIX 消息序列化为 ASCII 字符串（遵循 FIX 协议格式），例如：
     ```
     8=FIX.5.0SP2|9=...|35=D|49=CLIENT|56=SERVER|34=1|52=20250718-232732|...
     ```
   - 消息字段通过 `set()` 方法添加，QuickFIX 会自动计算 `BodyLength` 和 `CheckSum`。

2. **自定义数据类型**：
   - 如果需要扩展 FIX 协议，可以通过修改数据字典（XML 文件）或直接操作 `Message` 对象的字段。

3. **异常处理**：
   - QuickFIX 提供了详细的日志记录功能，可以通过 `FileLogFactory` 配置日志路径，便于调试。

4. **性能优化**：
   - QuickFIX 支持异步处理和多线程，可以通过调整线程池大小和缓冲区参数优化性能。

---

### **7. 推荐开源库**
- **QuickFIX**：核心库，用于实现 FIX 协议的完整功能。
- **Boost.Asio**（可选）：如果需要更底层的网络控制，可以结合 Boost.Asio。
- **cereal**（可选）：如果需要将 FIX 消息与其他数据结构序列化，可以结合 cereal 库。

---

### **8. 验证消息完整性**
QuickFIX 内置以下机制确保消息完整性：
- **校验和计算**：自动计算 `CheckSum` 字段。
- **序列号跟踪**：通过 `SeqNum` 跟踪消息顺序，检测丢失的消息。
- **心跳机制**：定期发送 `Heartbeat` 消息，维持连接。

---

### **总结**
通过 QuickFIX，可以高效地实现 FIX 协议的序列化和通信。其开源特性、成熟的代码库和社区支持，使其成为 C++ 项目中处理 FIX 协议的理想选择。
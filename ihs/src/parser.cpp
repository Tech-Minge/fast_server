#include <vector>
#include <cstdint>
#include <functional>
#include <system_error>

// FIX协议常量
constexpr char SOH = '\x01';
constexpr size_t MIN_FIX_MSG_LENGTH = 20; // 最小合法消息长度

class FixParser {
public:
    using MessageCallback = std::function<void(std::string_view)>;
    
    explicit FixParser(MessageCallback callback) : callback_(std::move(callback)) {}
    
    // 处理接收到的原始数据（处理粘包/半包）
    void on_data(const char* data, size_t len) {
        buffer_.insert(buffer_.end(), data, data + len);
        process_buffer();
    }

private:
    std::vector<char> buffer_;
    MessageCallback callback_;

    // 主解析逻辑
    void process_buffer() {
        while (true) {
            // 1. 查找消息起始符 "8=FIX.x.x"
            auto it = std::search(buffer_.begin(), buffer_.end(), "8=FIX", "8=FIX" + 5);
            if (it == buffer_.end()) return;

            // 2. 定位BodyLength字段 (Tag=9)
            auto body_length_start = std::find(it, buffer_.end(), '9');
            if (body_length_start == buffer_.end()) return;
            
            // 3. 解析BodyLength值
            size_t body_length = 0;
            auto num_end = parse_length(body_length_start + 2, body_length); // +2跳过'9='
            if (num_end == buffer_.end()) return;

            // 4. 计算完整消息长度 = 头部长度 + body_length + 尾部长度
            size_t header_len = std::distance(it, num_end);
            size_t total_msg_len = header_len + body_length + 7; // 7= "10=xxx"SOH
            
            // 5. 检查缓冲区是否足够
            if (buffer_.size() - std::distance(buffer_.begin(), it) < total_msg_len) {
                return; // 等待更多数据
            }

            // 6. 提取完整消息并验证
            std::string_view msg(&(*it), total_msg_len);
            if (validate_checksum(msg)) {
                callback_(msg); // 触发业务回调
            }

            // 7. 从缓冲区移除已处理消息
            buffer_.erase(buffer_.begin(), it + total_msg_len);
        }
    }

    // 解析BodyLength数字部分
    std::vector<char>::iterator parse_length(std::vector<char>::iterator start, size_t& out_len) {
        auto it = start;
        while (it != buffer_.end() && *it != SOH) {
            if (*it < '0' || *it > '9') {
                throw std::runtime_error("Invalid BodyLength format");
            }
            out_len = out_len * 10 + (*it - '0');
            ++it;
        }
        return it; // 返回SOH位置
    }

    // 校验和验证（基于FIX规范[7](@ref)）
    bool validate_checksum(std::string_view msg) {
        size_t calculated_sum = 0;
        size_t check_sum_pos = msg.find("10=");
        if (check_sum_pos == std::string_view::npos) return false;

        // 计算从开头到'10='之前的ASCII和
        for (size_t i = 0; i < check_sum_pos; ++i) {
            calculated_sum += static_cast<uint8_t>(msg[i]);
        }
        calculated_sum %= 256;

        // 提取消息中的CheckSum值
        int msg_checksum = std::stoi(msg.substr(check_sum_pos + 3, 3));
        return calculated_sum == msg_checksum;
    }
};



#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>

// FIX消息类
class FixMessage {
public:
    using FieldMap = std::unordered_map<int, std::string>;
    
    // 添加字段
    void addField(int tag, const std::string& value) {
        fields[tag] = value;
    }
    
    // 获取字段值
    std::string getField(int tag) const {
        auto it = fields.find(tag);
        if (it != fields.end()) {
            return it->second;
        }
        return "";
    }
    
    // 检查字段是否存在
    bool hasField(int tag) const {
        return fields.find(tag) != fields.end();
    }
    
    // 获取所有字段
    const FieldMap& getFields() const {
        return fields;
    }
    
private:
    FieldMap fields;
};

// FIX协议解析器
class FixParser {
public:
    // 定义SOH分隔符(ASCII 0x01)
    static constexpr char SOH = '\x01';
    
    // 标准FIX头部字段
    static constexpr int BEGIN_STRING = 8;
    static constexpr int BODY_LENGTH = 9;
    static constexpr int MESSAGE_TYPE = 35;
    static constexpr int CHECK_SUM = 10;
    
    // 解析缓冲区中的所有完整消息
    std::vector<FixMessage> parse(const std::vector<char>& buffer) {
        std::vector<FixMessage> messages;
        size_t pos = 0;
        
        while (pos < buffer.size()) {
            // 查找消息起始位置(第一个SOH之后的BEGIN_STRING字段)
            pos = findNextMessageStart(buffer, pos);
            if (pos == std::string::npos) {
                break;  // 没有更多完整消息
            }
            
            // 尝试解析消息
            size_t messageEnd = 0;
            try {
                FixMessage msg = parseMessage(buffer, pos, messageEnd);
                messages.push_back(msg);
                pos = messageEnd;
            } catch (const std::runtime_error& e) {
                // 解析失败，跳过此消息
                std::cerr << "Error parsing message: " << e.what() << std::endl;
                // 寻找下一个可能的消息起始位置
                pos++;
            }
        }
        
        return messages;
    }
    
private:
    // 查找下一个消息的起始位置
    size_t findNextMessageStart(const std::vector<char>& buffer, size_t startPos) const {
        // 查找第一个SOH
        size_t sohPos = startPos;
        while (sohPos < buffer.size() && buffer[sohPos] != SOH) {
            sohPos++;
        }
        
        if (sohPos >= buffer.size()) {
            return std::string::npos;  // 没有找到SOH
        }
        
        // 检查是否是BEGIN_STRING字段(8=)
        size_t tagStart = sohPos + 1;
        if (tagStart + 2 >= buffer.size()) {
            return std::string::npos;  // 剩余数据不足
        }
        
        if (buffer[tagStart] == '8' && buffer[tagStart + 1] == '=') {
            return sohPos + 1;  // 找到消息起始
        }
        
        // 继续查找下一个SOH
        return findNextMessageStart(buffer, sohPos + 1);
    }
    
    // 解析单个消息
    FixMessage parseMessage(const std::vector<char>& buffer, size_t startPos, size_t& endPos) const {
        FixMessage msg;
        size_t pos = startPos;
        
        // 解析头部字段
        int bodyLength = 0;
        bool hasBeginString = false;
        bool hasBodyLength = false;
        bool hasMsgType = false;
        
        // 解析字段直到找到BodyLength
        while (pos < buffer.size()) {
            // 查找字段结束符SOH
            size_t fieldEnd = pos;
            while (fieldEnd < buffer.size() && buffer[fieldEnd] != SOH) {
                fieldEnd++;
            }
            
            if (fieldEnd >= buffer.size()) {
                throw std::runtime_error("Incomplete field");
            }
            
            // 解析字段: tag=value
            size_t equalPos = pos;
            while (equalPos < fieldEnd && buffer[equalPos] != '=') {
                equalPos++;
            }
            
            if (equalPos >= fieldEnd) {
                throw std::runtime_error("Invalid field format");
            }
            
            int tag = std::stoi(std::string(&buffer[pos], equalPos - pos));
            std::string value(&buffer[equalPos + 1], fieldEnd - equalPos - 1);
            
            // 添加到消息
            msg.addField(tag, value);
            
            // 检查特殊字段
            if (tag == BEGIN_STRING) {
                hasBeginString = true;
            } else if (tag == BODY_LENGTH) {
                hasBodyLength = true;
                bodyLength = std::stoi(value);
            } else if (tag == MESSAGE_TYPE) {
                hasMsgType = true;
            } else if (tag == CHECK_SUM) {
                // 到达校验和字段，消息结束
                endPos = fieldEnd + 1;
                // 验证消息完整性
                if (!hasBeginString || !hasBodyLength || !hasMsgType) {
                    throw std::runtime_error("Missing required header fields");
                }
                
                // 验证消息长度
                size_t calculatedBodyLength = calculateBodyLength(buffer, startPos, fieldEnd);
                if (calculatedBodyLength != static_cast<size_t>(bodyLength)) {
                    throw std::runtime_error("Body length mismatch");
                }
                
                // 验证校验和(简化实现)
                if (!verifyChecksum(buffer, startPos, fieldEnd, value)) {
                    throw std::runtime_error("Checksum mismatch");
                }
                
                return msg;
            }
            
            pos = fieldEnd + 1;
        }
        
        throw std::runtime_error("Message incomplete, missing checksum");
    }
    
    // 计算消息体长度(从BEGIN_STRING之后到CHECK_SUM之前的字节数)
    size_t calculateBodyLength(const std::vector<char>& buffer, size_t startPos, size_t checksumPos) const {
        // 跳过BEGIN_STRING字段(8=XX\x01)
        size_t bodyStart = startPos;
        while (bodyStart < buffer.size() && buffer[bodyStart] != SOH) {
            bodyStart++;
        }
        bodyStart++;  // 移过SOH
        
        // 消息体长度 = 从bodyStart到CHECK_SUM字段前一个SOH的字节数
        return checksumPos - bodyStart;
    }
    
    // 验证消息校验和(简化实现)
    bool verifyChecksum(const std::vector<char>& buffer, size_t startPos, size_t checksumPos, 
                       const std::string& checksumValue) const {
        // 实际应用中需要实现完整的校验和计算
        // 校验和 = (消息中所有字节的和) % 256，格式为000-255
        // 这里简化处理，仅作示例
        return true;
    }
};

// 使用示例
int main() {
    // 示例FIX消息(简化，使用|表示SOH)
    std::string fixMsg = "8=FIX.4.4|9=108|35=D|49=CLIENT|56=BROKER|34=123|52=20230726-12:34:56|55=IBM|54=1|60=20230726-12:34:56|11=ORDER123|38=100|40=2|44=150.50|10=123|";
    
    // 转换为缓冲区
    std::vector<char> buffer(fixMsg.begin(), fixMsg.end());
    for (size_t i = 0; i < buffer.size(); i++) {
        if (buffer[i] == '|') {
            buffer[i] = FixParser::SOH;  // 替换为实际的SOH字符
        }
    }
    
    // 解析消息
    FixParser parser;
    auto messages = parser.parse(buffer);
    
    // 输出解析结果
    for (const auto& msg : messages) {
        std::cout << "Message Type: " << msg.getField(FixParser::MESSAGE_TYPE) << std::endl;
        std::cout << "Body Length: " << msg.getField(FixParser::BODY_LENGTH) << std::endl;
        
        // 输出所有字段
        const auto& fields = msg.getFields();
        for (const auto& field : fields) {
            std::cout << "  Tag: " << field.first << ", Value: " << field.second << std::endl;
        }
    }
    
    return 0;
}



/*
以下是一个基于C++的FIX协议解析器实现，它使用长度字段（BodyLength, tag 9）来正确解析消息边界，并处理各种边界情况：

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <iomanip>

class FixParser {
public:
    // FIX消息解析结果
    struct FixMessage {
        std::unordered_map<int, std::string> fields;
        std::string raw_message;
        bool valid = false;
    };

    // 添加数据到解析缓冲区
    void addData(const char* data, size_t length) {
        buffer_.append(data, length);
        parseBuffer();
    }

    // 获取已解析的消息
    std::vector<FixMessage> getMessages() {
        return std::move(messages_);
    }

private:
    // SOH字符 (ASCII 1) - FIX协议分隔符
    static constexpr char SOH = '\x01';

    std::string buffer_;
    std::vector<FixMessage> messages_;

    // 计算校验和
    std::string calculateChecksum(const std::string& msg) const {
        uint8_t sum = 0;
        for (char c : msg) {
            sum += static_cast<uint8_t>(c);
        }
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill('0') << static_cast<int>(sum % 256);
        return oss.str();
    }

    // 解析缓冲区中的消息
    void parseBuffer() {
        while (!buffer_.empty()) {
            // 步骤1: 查找消息开始 (8=)
            size_t start_pos = buffer_.find("8=");
            if (start_pos == std::string::npos) {
                // 没有找到消息开始，清空无效数据
                buffer_.clear();
                return;
            }

            // 移除无效前缀
            if (start_pos > 0) {
                buffer_.erase(0, start_pos);
            }

            // 步骤2: 查找BodyLength字段 (9=)
            size_t body_length_pos = buffer_.find("9=", 0);
            if (body_length_pos == std::string::npos || body_length_pos > 20) {
                // 9=应该在消息开头附近
                buffer_.erase(0, 2); // 跳过这个8=，可能是不完整的
                continue;
            }

            // 步骤3: 提取BodyLength值
            size_t soh_pos = buffer_.find(SOH, body_length_pos);
            if (soh_pos == std::string::npos) {
                // 没有找到SOH，需要更多数据
                return;
            }

            // 提取BodyLength值
            std::string length_str = buffer_.substr(
                body_length_pos + 2, 
                soh_pos - (body_length_pos + 2)
            );

            // 验证长度值
            if (length_str.empty() || !std::all_of(length_str.begin(), length_str.end(), ::isdigit)) {
                buffer_.erase(0, soh_pos + 1);
                continue;
            }

            // 转换长度值
            uint32_t body_length = 0;
            try {
                body_length = std::stoul(length_str);
            } catch (...) {
                buffer_.erase(0, soh_pos + 1);
                continue;
            }

            // 步骤4: 计算消息结束位置
            // body_start: BodyLength字段结束后的第一个字符
            size_t body_start = soh_pos + 1;
            // 消息结束位置 = body_start + body_length + 7 (10=xxx) + 1 (SOH)
            size_t message_end = body_start + body_length + 8;

            // 检查是否收到完整消息
            if (buffer_.size() < message_end) {
                // 数据不足，等待更多数据
                return;
            }

            // 步骤5: 验证校验和
            std::string checksum_field = buffer_.substr(message_end - 4, 7); // 10=xxx
            if (checksum_field.size() < 7 || checksum_field.substr(0, 3) != "10=") {
                // 无效的校验和字段
                buffer_.erase(0, message_end);
                continue;
            }

            // 提取接收到的校验和
            std::string received_checksum = checksum_field.substr(3, 3);

            // 计算期望的校验和
            std::string message_body = buffer_.substr(0, message_end - 4); // 不包括10=xxx
            std::string expected_checksum = calculateChecksum(message_body);

            // 步骤6: 解析消息字段
            FixMessage msg;
            msg.raw_message = buffer_.substr(0, message_end);

            if (received_checksum == expected_checksum) {
                parseFields(msg, body_start, body_length);
                msg.valid = true;
            }

            messages_.push_back(msg);

            // 从缓冲区移除已处理的消息
            buffer_.erase(0, message_end);
        }
    }

    // 解析消息字段
    void parseFields(FixMessage& msg, size_t body_start, size_t body_length) const {
        std::string body = msg.raw_message.substr(body_start, body_length);
        size_t pos = 0;
        
        while (pos < body.length()) {
            // 查找等号（分隔tag和值）
            size_t equal_pos = body.find('=', pos);
            if (equal_pos == std::string::npos) break;
            
            // 提取tag
            std::string tag_str = body.substr(pos, equal_pos - pos);
            if (tag_str.empty() || !std::all_of(tag_str.begin(), tag_str.end(), ::isdigit)) {
                break;
            }
            
            int tag = 0;
            try {
                tag = std::stoi(tag_str);
            } catch (...) {
                break;
            }
            
            // 查找SOH（字段结束）
            size_t soh_pos = body.find(SOH, equal_pos + 1);
            if (soh_pos == std::string::npos) {
                // 最后一个字段可能没有SOH
                soh_pos = body.length();
            }
            
            // 提取值
            std::string value = body.substr(equal_pos + 1, soh_pos - (equal_pos + 1));
            msg.fields[tag] = value;
            
            // 移动到下一个字段
            pos = soh_pos + 1;
        }
    }
};

// 示例使用
int main() {
    // 示例FIX消息 (NewOrderSingle)
    std::string fix_message = 
        "8=FIX.4.4"  "\x01" 
        "9=150"       "\x01" 
        "35=D"        "\x01" 
        "49=BUYTRADE" "\x01" 
        "56=SELLSIDE" "\x01" 
        "34=237"      "\x01" 
        "52=20230801-15:42:37.123" "\x01" 
        "11=ORD10001" "\x01" 
        "55=AAPL"     "\x01" 
        "54=1"        "\x01" 
        "60=20230801-15:42:37" "\x01" 
        "38=100"      "\x01" 
        "40=2"        "\x01" 
        "44=150.25"   "\x01" 
        "59=0"        "\x01" 
        "10=132"      "\x01"; // 校验和

    FixParser parser;
    
    // 模拟网络数据流 - 分多次添加数据
    parser.addData(fix_message.data(), 50);  // 添加前半部分
    parser.addData(fix_message.data() + 50, fix_message.size() - 50);  // 添加剩余部分
    
    auto messages = parser.getMessages();
    
    for (const auto& msg : messages) {
        std::cout << "Received FIX message (" 
                  << (msg.valid ? "Valid" : "Invalid")
                  << "):\n";
        std::cout << "Raw: " << msg.raw_message << "\n";
        
        if (msg.valid) {
            std::cout << "Parsed fields:\n";
            for (const auto& [tag, value] : msg.fields) {
                std::cout << "  " << tag << " = " << value << "\n";
            }
        }
        
        std::cout << "------------------------\n";
    }
    
    return 0;
}
```

### 关键设计说明

#### 1. FIX消息结构
FIX消息的基本结构：
```
8=BeginString<SOH>9=BodyLength<SOH>...MessageBody...10=CheckSum<SOH>
```
- `8=`：协议版本开始
- `9=`：消息体长度（从9=之后到10=之前的所有字符数）
- `10=`：校验和（3位数字）

#### 2. 解析算法步骤

1. **查找消息开始**：
   - 在缓冲区中查找"8="标志
   - 丢弃"8="之前的任何无效数据

2. **定位BodyLength字段**：
   - 在消息开始附近查找"9="字段
   - 提取并验证长度值

3. **计算消息结束位置**：
   ```
   消息结束位置 = BodyLength开始位置 + BodyLength字段长度 + BodyLength值 + 8
   ```
   - 最后的8字节：7字节用于"10=xxx"，1字节用于SOH

4. **验证校验和**：
   - 计算消息的校验和（不包括"10="字段）
   - 与接收到的校验和比较

5. **解析消息字段**：
   - 按SOH分隔符分割字段
   - 每个字段按"="分割tag和value

6. **处理不完整消息**：
   - 如果消息不完整，保留在缓冲区等待更多数据
   - 如果消息无效，跳过并继续处理后续数据

#### 3. 边界情况处理

1. **不完整消息**：
   - 当缓冲区中没有完整的消息时，保留数据
   - 示例：只收到消息前半部分

2. **多个消息**：
   - 正确处理缓冲区中背靠背的多个消息
   - 示例：`消息1...SOH10=xxx消息2...`

3. **无效消息**：
   - 无效的BodyLength（非数字）
   - 校验和不匹配
   - 格式错误的字段

4. **粘包处理**：
   - 正确处理网络传输中的消息粘连
   - 示例：`消息1...SOH10=xxx消息2...`

5. **分片消息**：
   - 处理分多次到达的消息
   - 示例：先收到前50字节，再收到剩余部分

#### 4. 校验和计算
```cpp
std::string calculateChecksum(const std::string& msg) const {
    uint8_t sum = 0;
    for (char c : msg) {
        sum += static_cast<uint8_t>(c);
    }
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << static_cast<int>(sum % 256);
    return oss.str();
}
```
- 计算所有字符的ASCII值之和
- 取模256得到8位值
- 格式化为3位数字符串

#### 5. 字段解析
```cpp
void parseFields(FixMessage& msg, size_t body_start, size_t body_length) const {
    std::string body = msg.raw_message.substr(body_start, body_length);
    size_t pos = 0;
    
    while (pos < body.length()) {
        size_t equal_pos = body.find('=', pos);
        // 提取tag和value
        // ...
    }
}
```
- 按SOH字符分割字段
- 每个字段按第一个"="分割tag和value
- 支持重复tag（虽然FIX协议通常不允许）

### 使用示例

```cpp
int main() {
    FixParser parser;
    
    // 模拟网络数据流
    std::string data1 = "8=FIX.4.4\x01""9=150\x01""35=D\x01""49=BUYTRADE";
    std::string data2 = "\x01""56=SELLSIDE\x01""...完整消息剩余部分...";
    
    parser.addData(data1.data(), data1.size());
    parser.addData(data2.data(), data2.size());
    
    auto messages = parser.getMessages();
    
    for (const auto& msg : messages) {
        if (msg.valid) {
            std::cout << "Order ID: " << msg.fields.at(11) << "\n";
            std::cout << "Symbol: " << msg.fields.at(55) << "\n";
            std::cout << "Price: " << msg.fields.at(44) << "\n";
        }
    }
    
    return 0;
}
```

### 性能优化建议

1. **零拷贝解析**：
   ```cpp
   // 使用string_view避免复制
   void parseFields(std::string_view body) {
       // 解析逻辑
   }
   ```

2. **内存预分配**：
   ```cpp
   buffer_.reserve(4096); // 预分配缓冲区
   ```

3. **快速查找**：
   ```cpp
   // 使用memchr代替string::find
   const char* pos = std::memchr(buffer_.data(), SOH, buffer_.size());
   ```

4. **批处理**：
   ```cpp
   // 一次解析多个消息
   while (hasCompleteMessage()) {
       processMessage();
   }
   ```

5. **自定义分配器**：
   ```cpp
   // 使用内存池分配字段存储
   boost::pool_allocator<std::pair<const int, std::string>> alloc;
   std::unordered_map<int, std::string, std::hash<int>, std::equal_to<int>, decltype(alloc)> fields{alloc};
   ```

### FIX协议关键字段

| 标签 | 名称         | 描述                     |
|------|--------------|--------------------------|
| 8    | BeginString  | FIX协议版本              |
| 9    | BodyLength   | 消息体长度               |
| 35   | MsgType      | 消息类型（D=新订单）     |
| 49   | SenderCompID | 发送方ID                 |
| 56   | TargetCompID | 接收方ID                 |
| 34   | MsgSeqNum    | 消息序列号               |
| 52   | SendingTime  | 发送时间                 |
| 11   | ClOrdID      | 客户端订单ID             |
| 55   | Symbol       | 交易品种                 |
| 54   | Side         | 买卖方向（1=买，2=卖）   |
| 38   | OrderQty     | 订单数量                 |
| 40   | OrdType      | 订单类型（2=限价）       |
| 44   | Price        | 价格                     |
| 59   | TimeInForce  | 订单有效时间（0=当日有效）|
| 10   | CheckSum     | 校验和                   |

这个FIX解析器能够正确处理各种边界情况，包括不完整消息、多个消息粘连、分片消息等，并通过BodyLength字段准确确定消息边界，同时验证消息的完整性。*/
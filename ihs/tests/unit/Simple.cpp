# 添加交易模块测试
# 测试 order_handler.cpp
add_executable(test_order_handler
    trading/test_order_handler.cpp
)

target_link_libraries(test_order_handler
    PRIVATE
        trading_core  # 只链接被测试模块
        GTest::gtest_main
)

# 测试 market_parser.cpp
add_executable(test_market_parser
    trading/test_market_parser.cpp
)

target_link_libraries(test_market_parser
    PRIVATE
        trading_core  # 只链接被测试模块
        GTest::gtest_main
)

# 测试算法模块
# 测试 strategy.cpp
add_executable(test_strategy
    algo/test_strategy.cpp
)

target_link_libraries(test_strategy
    PRIVATE
        algo_core  # 测试整个算法模块
        GTest::gtest_main
)

# 自动发现并注册所有单元测试
include(GoogleTest)

# 为每个测试可执行文件添加测试发现
gtest_discover_tests(test_order_handler
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "unit;trading"
)

gtest_discover_tests(test_market_parser
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "unit;trading"
)

gtest_discover_tests(test_strategy
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "unit;algo"
)


/*
以下是一个简洁高效的 CMake 项目结构，集成了 Google Test (gtest) 使用 FetchContent，支持单元测试和集成测试，每个单元测试可针对特定的源文件或模块：

### 项目结构
```
my_project/
├── CMakeLists.txt              # 顶层配置
├── src/                        # 源代码
│   ├── trading/                # 交易核心模块
│   │   ├── order_handler.cpp
│   │   ├── order_handler.h
│   │   ├── market_parser.cpp
│   │   └── market_parser.h
│   ├── algo/                   # 算法模块
│   │   ├── strategy.cpp
│   │   └── strategy.h
│   └── main.cpp                # 主程序
└── test/                       # 测试代码
    ├── unit/                   # 单元测试
    │   ├── trading/            # 交易模块测试
    │   │   ├── test_order_handler.cpp
    │   │   └── test_market_parser.cpp
    │   ├── algo/               # 算法模块测试
    │   │   └── test_strategy.cpp
    │   └── CMakeLists.txt
    ├── integration/            # 集成测试
    │   ├── test_trading_system.cpp
    │   └── CMakeLists.txt
    └── CMakeLists.txt          # 测试主配置
```

### 文件内容

#### 1. 顶层 CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.14)
project(TradingSystem VERSION 1.0.0 LANGUAGES CXX)

# 测试开关选项
option(BUILD_TESTS "Build tests" ON)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加主项目
# 交易核心模块
add_library(trading_core
    src/trading/order_handler.cpp
    src/trading/market_parser.cpp
)

target_include_directories(trading_core
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 算法模块
add_library(algo_core
    src/algo/strategy.cpp
)

target_include_directories(algo_core
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(algo_core
    PUBLIC
        trading_core
)

# 可执行文件
add_executable(trading_system src/main.cpp)
target_link_libraries(trading_system
    PRIVATE
        algo_core
)

# 测试配置
if(BUILD_TESTS)
    add_subdirectory(test)
endif()
```

#### 2. test/CMakeLists.txt (测试主配置)
```cmake
# 获取GoogleTest
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.14.0  # 指定稳定版本
)

# 防止覆盖父项目的编译器设置
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# 添加测试子目录
add_subdirectory(unit)
add_subdirectory(integration)
```

#### 3. test/unit/CMakeLists.txt (单元测试)
```cmake
# 添加交易模块测试
# 测试 order_handler.cpp
add_executable(test_order_handler
    trading/test_order_handler.cpp
)

target_link_libraries(test_order_handler
    PRIVATE
        trading_core  # 只链接被测试模块
        GTest::gtest_main
)

# 测试 market_parser.cpp
add_executable(test_market_parser
    trading/test_market_parser.cpp
)

target_link_libraries(test_market_parser
    PRIVATE
        trading_core  # 只链接被测试模块
        GTest::gtest_main
)

# 测试算法模块
# 测试 strategy.cpp
add_executable(test_strategy
    algo/test_strategy.cpp
)

target_link_libraries(test_strategy
    PRIVATE
        algo_core  # 测试整个算法模块
        GTest::gtest_main
)

# 自动发现并注册所有单元测试
include(GoogleTest)

# 为每个测试可执行文件添加测试发现
gtest_discover_tests(test_order_handler
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "unit;trading"
)

gtest_discover_tests(test_market_parser
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "unit;trading"
)

gtest_discover_tests(test_strategy
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "unit;algo"
)
```

#### 4. test/integration/CMakeLists.txt (集成测试)
```cmake
# 创建集成测试可执行文件
add_executable(integration_tests
    test_trading_system.cpp
)

# 链接所有核心模块
target_link_libraries(integration_tests
    PRIVATE
        algo_core
        trading_core
        GTest::gtest_main
)

# 自动发现并注册测试
include(GoogleTest)
gtest_discover_tests(integration_tests
    DISCOVERY_TIMEOUT 10
    PROPERTIES LABELS "integration"
)
```

#### 5. test/unit/trading/test_order_handler.cpp (示例单元测试)
```cpp
#include "trading/order_handler.h"
#include <gtest/gtest.h>

TEST(OrderHandlerTest, OrderCreation) {
    OrderHandler handler;
    Order order = handler.create_order(100, "AAPL", OrderType::LIMIT);
    
    EXPECT_EQ(order.symbol, "AAPL");
    EXPECT_EQ(order.quantity, 100);
    EXPECT_EQ(order.type, OrderType::LIMIT);
    EXPECT_FALSE(order.order_id.empty());
}

TEST(OrderHandlerTest, InvalidOrder) {
    OrderHandler handler;
    EXPECT_THROW(handler.create_order(0, "AAPL", OrderType::MARKET), std::invalid_argument);
    EXPECT_THROW(handler.create_order(100, "", OrderType::MARKET), std::invalid_argument);
}
```

#### 6. test/integration/test_trading_system.cpp (示例集成测试)
```cpp
#include "trading/market_parser.h"
#include "algo/strategy.h"
#include <gtest/gtest.h>

class TradingSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化所有组件
        parser.initialize();
        strategy.initialize();
    }
    
    MarketParser parser;
    TradingStrategy strategy;
};

TEST_F(TradingSystemTest, FullOrderFlow) {
    MarketData data = parser.parse("AAPL,150.25,100");
    OrderDecision decision = strategy.analyze(data);
    
    EXPECT_TRUE(decision.should_trade);
    EXPECT_EQ(decision.symbol, "AAPL");
    EXPECT_GT(decision.quantity, 0);
}
```

### 使用说明

#### 1. 配置项目
```bash
# 启用测试（默认）
cmake -B build

# 禁用测试
cmake -B build -DBUILD_TESTS=OFF
```

#### 2. 构建项目
```bash
cmake --build build
```

#### 3. 运行测试
```bash
# 运行所有测试
cd build && ctest

# 运行特定模块的单元测试
ctest -L trading   # 交易模块测试
ctest -L algo      # 算法模块测试

# 运行单个测试可执行文件
./test/unit/test_order_handler

# 带过滤运行特定测试
./test/unit/test_strategy --gtest_filter=StrategyTest.*
```

### 关键特性说明

#### 1. 模块化测试结构
```cmake
# 针对特定源文件的测试
add_executable(test_order_handler
    trading/test_order_handler.cpp
)

target_link_libraries(test_order_handler
    PRIVATE
        trading_core  # 只链接被测试模块
)
```

- **精准测试**：每个测试可执行文件针对特定源文件或模块
- **最小依赖**：只链接必要的依赖，减少构建时间
- **隔离性**：测试失败时更容易定位问题

#### 2. FetchContent 集成 GTest
```cmake
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.14.0
)
```

- **版本控制**：明确指定 GTest 版本
- **自包含**：无需系统级安装
- **自动管理**：CMake 自动处理依赖关系

#### 3. 标签化测试管理
```cmake
gtest_discover_tests(test_order_handler
    PROPERTIES LABELS "unit;trading"
)
```

- **模块标签**：`trading`, `algo` 标识测试所属模块
- **类型标签**：`unit`, `integration` 标识测试类型
- **灵活筛选**：使用 `ctest -L <label>` 运行特定测试

#### 4. 高效测试组织
```plaintext
test/
├── unit/
│   ├── trading/            # 交易模块测试
│   │   ├── test_order_handler.cpp  # 测试 order_handler.cpp
│   │   └── test_market_parser.cpp  # 测试 market_parser.cpp
│   ├── algo/               # 算法模块测试
│   │   └── test_strategy.cpp       # 测试 strategy.cpp
```

- **路径映射**：测试文件路径对应源码路径
- **模块化组织**：按功能模块分组测试
- **可扩展**：轻松添加新测试文件

### 高级技巧

#### 1. 测试特定源文件（不依赖整个库）
```cmake
# 直接编译源文件进行测试
add_executable(test_market_parser
    trading/test_market_parser.cpp
    ../src/trading/market_parser.cpp  # 直接包含源文件
)

target_include_directories(test_market_parser
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(test_market_parser
    PRIVATE
        GTest::gtest_main
)
```

#### 2. 模拟对象支持
```cmake
# 添加模拟库
add_library(mock_trading
    test/mocks/mock_order_handler.cpp
)

# 在测试中使用模拟
target_link_libraries(test_strategy
    PRIVATE
        algo_core
        mock_trading  # 使用模拟代替真实实现
        GTest::gtest_main
)
```

#### 3. 测试覆盖率
```cmake
# 在顶层CMakeLists.txt中添加
if(ENABLE_COVERAGE AND BUILD_TESTS)
    # 为所有测试目标添加覆盖率选项
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    
    # 添加覆盖率目标
    add_custom_target(coverage
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_report
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report"
    )
endif()
```

#### 4. 测试时间限制
```cmake
# 为特定测试设置超时
set_tests_properties(
    TradingSystemTest.FullOrderFlow
    PROPERTIES TIMEOUT 5  # 5秒超时
)
```

### 最佳实践建议

1. **测试命名规范**：
   - 测试文件：`test_<source_file_name>.cpp`
   - 测试用例：`<ClassName>Test.<methodName>`

2. **测试与源码对应**：
   ```plaintext
   src/trading/order_handler.cpp
   test/unit/trading/test_order_handler.cpp
   ```

3. **最小化测试依赖**：
   - 每个测试只链接必要的依赖
   - 使用模拟代替复杂依赖

4. **并行测试构建**：
   ```bash
   cmake --build build --parallel 8
   ctest -j8
   ```

5. **持续集成配置**：
   ```yaml
   # GitHub Actions 示例
   name: CI
   on: [push, pull_request]
   jobs:
     build:
       runs-on: ubuntu-latest
       steps:
       - uses: actions/checkout@v3
       - name: Configure CMake
         run: cmake -B build -DBUILD_TESTS=ON
       - name: Build
         run: cmake --build build --config Release
       - name: Run tests
         run: cd build && ctest -C Release --output-on-failure
   ```

### 完整工作流程示例

1. **克隆并配置项目**：
   ```bash
   git clone https://github.com/your/trading-system.git
   cd trading-system
   mkdir build && cd build
   cmake -DBUILD_TESTS=ON ..
   ```

2. **构建并运行测试**：
   ```bash
   cmake --build . --parallel 4
   ctest --output-on-failure
   
   # 只运行交易模块测试
   ctest -L trading
   ```

3. **生成覆盖率报告**：
   ```bash
   cmake -DENABLE_COVERAGE=ON ..
   cmake --build .
   ctest
   make coverage  # 生成HTML覆盖率报告
   ```

4. **开发循环**：
   ```bash
   # 只构建并运行特定测试
   cmake --build . --target test_order_handler
   ./test/unit/test_order_handler
   
   # 或者使用过滤器
   ./test/unit/test_strategy --gtest_filter=StrategyTest.*
   ```

这个配置提供了：
1. **简洁性**：最小化的CMake配置
2. **高效性**：精确测试特定源文件，减少构建时间
3. **灵活性**：通过标签管理测试
4. **可扩展性**：轻松添加新模块和测试
5. **现代CMake实践**：使用目标属性而非全局变量

每个测试可执行文件针对特定源文件或模块，确保测试的精准性和隔离性，同时保持构建系统的高效性。
*/


/*
 ### 独立 C++ 文件的测试配置方案

如果你的项目由多个独立的 C++ 文件组成（而非库），可以调整 CMake 配置如下。这种方案为每个源文件创建单独的可执行文件，并为每个可执行文件编写对应的测试。


### 项目结构

```
myproject/
├── CMakeLists.txt               # 主CMake文件
├── src/                         # 源代码目录
│   ├── CMakeLists.txt           # 源代码子CMake
│   ├── main.cpp                 # 主程序入口（可选）
│   ├── module1.cpp              # 独立模块1
│   ├── module2.cpp              # 独立模块2
│   └── ...
└── tests/                       # 测试代码目录
    ├── CMakeLists.txt           # 测试子CMake
    ├── test_module1.cpp         # 测试module1
    ├── test_module2.cpp         # 测试module2
    └── ...
```


### CMake 配置实现

#### 1. 主 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject VERSION 1.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用FetchContent模块
include(FetchContent)

# 配置GoogleTest
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    
    # 对于Windows：防止gtest安装
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    # 添加测试支持
    enable_testing()
    include(GoogleTest)
endif()

# 添加源代码
add_subdirectory(src)

# 添加测试
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
```


#### 2. 源代码目录 CMakeLists.txt (src/CMakeLists.txt)

```cmake
# 收集所有源文件（除了main.cpp）
file(GLOB_RECURSE SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "main\\.cpp$")

# 为每个源文件创建一个对象库
foreach(SOURCE ${SOURCES})
    get_filename_component(MODULE_NAME ${SOURCE} NAME_WE)
    
    # 创建对象库（方便测试和主程序复用）
    add_library(${MODULE_NAME}_obj OBJECT ${SOURCE})
    
    # 设置编译选项
    target_compile_options(${MODULE_NAME}_obj PRIVATE
        -Wall -Wextra -Wpedantic
    )
endforeach()

# 如果有main.cpp，创建主程序
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    add_executable(myproject main.cpp)
    
    # 链接所有对象库到主程序
    foreach(SOURCE ${SOURCES})
        get_filename_component(MODULE_NAME ${SOURCE} NAME_WE)
        target_link_libraries(myproject ${MODULE_NAME}_obj)
    endforeach()
endif()
```


#### 3. 测试目录 CMakeLists.txt (tests/CMakeLists.txt)

```cmake
# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

# 为每个测试文件创建对应的测试可执行文件
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    
    # 提取被测试的模块名（假设测试文件名为test_<module>.cpp）
    string(REGEX REPLACE "^test_" "" MODULE_NAME ${TEST_NAME})
    
    # 检查是否存在对应的对象库
    if(TARGET ${MODULE_NAME}_obj)
        # 创建测试可执行文件
        add_executable(${TEST_NAME} ${TEST_SOURCE})
        
        # 链接GoogleTest和被测模块
        target_link_libraries(${TEST_NAME}
            gtest
            gtest_main
            ${MODULE_NAME}_obj
        )
        
        # 添加包含目录（如果需要）
        target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
        
        # 将测试添加到CTest
        gtest_discover_tests(${TEST_NAME})
    else()
        message(WARNING "No source file found for test: ${TEST_NAME}")
    endif()
endforeach()
```


### 测试文件示例

#### 测试 module1.cpp (tests/test_module1.cpp)

```cpp
#include <gtest/gtest.h>

// 包含被测模块的头文件（如果有）
#include "module1.h"  // 假设module1.h存在

// 或者直接声明被测函数
extern int module1_function(int arg);

TEST(Module1Test, BasicTest) {
    EXPECT_EQ(module1_function(0), 0);
    EXPECT_EQ(module1_function(1), 1);
}

TEST(Module1Test, EdgeCaseTest) {
    EXPECT_EQ(module1_function(-1), -1);
}
```


### 构建与运行测试

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目（开启测试构建）
cmake .. -DBUILD_TESTS=ON

# 编译项目
cmake --build .

# 运行所有测试
ctest

# 或者单独运行某个测试
./tests/test_module1
```


### 关键点说明

1. **独立模块处理**：
   - 为每个 C++ 文件创建对象库（`add_library(... OBJECT)`）
   - 对象库可被主程序和测试程序复用

2. **测试匹配规则**：
   - 测试文件命名为 `test_<module>.cpp`
   - CMake 自动匹配测试文件与对应的源文件

3. **灵活配置**：
   - 支持有或没有主程序的项目
   - 自动处理头文件包含路径
   - 可选择性构建测试（通过 `-DBUILD_TESTS=ON/OFF`）

这种方案适用于脚本式 C++ 项目或工具集，保持了测试的独立性和可维护性，同时避免了将所有文件强制组织成库的复杂性。 
 */


 /*
 在 CMake 中，可以通过以下方式实现 **每个单元测试针对 `src` 目录下的一个或多个源文件** 的需求。我们将结合 `FetchContent` 下载 GTest、通过选项控制测试开关，并灵活组织单元测试和集成测试的结构。

---

### **项目结构**
```
my_project/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── math_utils.cpp
│   └── string_utils.cpp
├── test/
│   ├── unit/
│   │   ├── test_math_utils.cpp
│   │   └── test_string_utils.cpp
│   ├── integration/
│   │   └── test_integration.cpp
│   └── CMakeLists.txt
└── build/
```

---

### **根目录 CMakeLists.txt**
```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开关选项：是否启用测试
option(ENABLE_TEST "Build unit/integration tests" OFF)

# 主程序目标
add_executable(my_app src/main.cpp src/math_utils.cpp src/string_utils.cpp)

# 引入 GTest（仅在启用测试时）
if(ENABLE_TEST)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.12.1  # 指定版本
    )
    FetchContent_MakeAvailable(googletest)
endif()

# 子目录（测试逻辑放在子目录中）
add_subdirectory(test)
```

---

### **test/CMakeLists.txt**
```cmake
# 仅在启用测试时处理测试逻辑
if(ENABLE_TEST)
    enable_testing()

    # 单元测试
    file(GLOB UNIT_TESTS "unit/test_*.cpp")
    foreach(TEST_FILE ${UNIT_TESTS})
        # 提取测试名称（例如 test_math_utils -> math_utils）
        get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
        string(REPLACE "test_" "" TEST_NAME ${TEST_NAME})

        # 自动推断对应的源文件（假设源文件名与测试文件名一致，去掉 test_ 前缀）
        set(SRC_FILE "../src/${TEST_NAME}.cpp")

        # 创建测试目标
        add_executable(${TEST_NAME}_test ${TEST_FILE} ${SRC_FILE})
        target_link_libraries(${TEST_NAME}_test
            PRIVATE
                my_app  # 链接主程序源文件（可选）
                GTest::gtest_main  # GTest 主入口
        )

        # 添加到 CTest
        add_test(NAME ${TEST_NAME}_test COMMAND ${TEST_NAME}_test)
    endforeach()

    # 集成测试
    add_executable(integration_test integration/test_integration.cpp)
    target_link_libraries(integration_test
        PRIVATE
            my_app
            GTest::gtest_main
    )
    add_test(NAME integration_test COMMAND integration_test)
endif()
```

---

### **关键点说明**

1. **自动推断源文件**  
   - 假设每个单元测试文件（如 `test_math_utils.cpp`）对应一个同名的源文件（如 `math_utils.cpp`），通过 `string(REPLACE ...)` 和 `set(SRC_FILE ...)` 自动推断源文件路径。
   - 如果某个测试需要多个源文件（如 `math_utils.cpp` 和 `string_utils.cpp`），可以手动修改 `add_executable` 中的源文件列表：
     ```cmake
     add_executable(${TEST_NAME}_test ${TEST_FILE} "../src/math_utils.cpp" "../src/string_utils.cpp")
     ```

2. **支持多文件测试**  
   - 如果某个测试需要多个源文件，可以直接在 `add_executable` 中显式列出所有源文件。例如：
     ```cmake
     add_executable(${TEST_NAME}_test ${TEST_FILE} "../src/math_utils.cpp" "../src/string_utils.cpp")
     ```

3. **测试开关控制**  
   - 通过 `option(ENABLE_TEST ...)` 控制是否构建测试目标，避免无谓的依赖。

4. **简洁高效的写法**  
   - 使用 `foreach` 循环批量处理单元测试文件，避免重复代码。
   - 通过 `file(GLOB ...)` 收集所有单元测试文件，确保灵活性。

---

### **构建与运行测试**
```bash
# 构建（启用测试）
mkdir build && cd build
cmake .. -DENABLE_TEST=ON
make

# 运行测试
ctest --output-on-failure
```

---

### **注意事项**
1. **测试文件命名规范**  
   - 单元测试文件命名为 `test_<模块名>.cpp`（如 `test_math_utils.cpp`），以便自动推断对应的源文件。
2. **避免重复编译源文件**  
   - 如果主程序和测试需要共享源文件（如 `math_utils.cpp`），需在 `add_executable` 中显式包含。
3. **跨平台兼容性**  
   - 使用 `FetchContent` 可避免手动管理 GTest 源码，确保跨平台一致性。

---

### **扩展：手动指定源文件**
如果某些测试文件需要多个源文件或非同名源文件，可以手动指定源文件列表。例如：

```cmake
# 在 test/CMakeLists.txt 中
if(ENABLE_TEST)
    enable_testing()

    # 单元测试
    file(GLOB UNIT_TESTS "unit/test_*.cpp")
    foreach(TEST_FILE ${UNIT_TESTS})
        get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
        string(REPLACE "test_" "" TEST_NAME ${TEST_NAME})

        # 手动指定源文件（例如 math_utils.cpp 和 string_utils.cpp）
        set(SRC_FILES
            "../src/math_utils.cpp"
            "../src/string_utils.cpp"
        )

        # 创建测试目标
        add_executable(${TEST_NAME}_test ${TEST_FILE} ${SRC_FILES})
        target_link_libraries(${TEST_NAME}_test
            PRIVATE
                my_app
                GTest::gtest_main
        )

        # 添加到 CTest
        add_test(NAME ${TEST_NAME}_test COMMAND ${TEST_NAME}_test)
    endforeach()
endif()
```

---

### **总结**
- 通过自动推断源文件路径，可以快速为每个单元测试文件关联对应的 `src` 源文件。
- 如果需要更复杂的依赖关系，可以手动指定源文件列表。
- 该方法兼顾简洁性和灵活性，适合中小型项目快速集成 GTest 并管理单元/集成测试。 */


/*
下面详细介绍如何使用 CMake 集成 Google Test（GTest），通过 FetchContent 管理依赖、开关测试选项，并构建高效的项目结构（含单元测试/集成测试），兼顾简洁性与可维护性。

一、CMake 集成 Google Test（FetchContent）

1. 核心配置（CMakeLists.txt）

cmake_minimum_required(VERSION 3.11)  # FetchContent 需 ≥3.11
project(MyProject LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 定义库和主程序
add_library(core_lib src/core.cpp)       # 核心功能库
add_executable(main_app src/main.cpp)    # 主程序
target_link_libraries(main_app core_lib)

# 测试开关选项
option(ENABLE_TESTS "Enable unit tests" ON)  

# 仅当测试开关开启时集成 GTest
if(ENABLE_TESTS)
    include(FetchContent)
    # 指定 GIT 仓库和版本标签
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1  # 推荐指定稳定版本
    )
    FetchContent_MakeAvailable(googletest)  
endif()


2. 关键说明

• 版本控制：GIT_TAG 指定版本（如 release-1.12.1），避免兼容性问题。

• 条件编译：option(ENABLE_TESTS) 控制是否启用测试，便于在无网络环境构建。

• 依赖隔离：FetchContent_MakeAvailable 自动下载并构建 GTest，无需手动管理源码。

二、项目结构设计

1. 推荐目录结构

MyProject/
├── CMakeLists.txt         # 根配置
├── src/                   # 主代码
│   ├── core.cpp
│   └── main.cpp
└── tests/                 # 测试代码
    ├── unit/              # 单元测试
    │   ├── test_math.cpp
    │   └── test_utils.cpp
    ├── integration/       # 集成测试
    │   └── api_test.cpp
    └── CMakeLists.txt     # 测试配置


2. 测试层配置（tests/CMakeLists.txt）

if(ENABLE_TESTS)
    # 单元测试可执行文件
    add_executable(unit_tests
        unit/test_math.cpp
        unit/test_utils.cpp
    )
    target_link_libraries(unit_tests
        core_lib           # 链接核心库
        gtest_main         # 使用 GTest 的主入口
    )
    
    # 集成测试可执行文件
    add_executable(integration_tests
        integration/api_test.cpp
    )
    target_link_libraries(integration_tests
        core_lib
        gtest_main
    )

    # 注册测试到 CTest
    enable_testing()
    add_test(NAME unit_tests COMMAND unit_tests)
    add_test(NAME integration_tests COMMAND integration_tests)  
endif()


3. 优势

• 模块化：单元测试与集成测试分离，便于维护。

• 多文件支持：通过 add_executable 合并多个测试文件。

• 统一管理：所有测试通过 ctest 命令运行。

三、测试代码示例

1. 单元测试文件（test_math.cpp）

#include <gtest/gtest.h>
#include "core/math_utils.h"  // 被测头文件

TEST(MathTest, AddPositiveNumbers) {
    EXPECT_EQ(add(2, 3), 5);   // 基础断言
}

TEST(MathTest, HandleZero) {
    EXPECT_EQ(add(0, 0), 0);
}

// 测试固件（复用设置）
class MathFixture : public testing::Test {
protected:
    void SetUp() override { ... }
};
TEST_F(MathFixture, ComplexOperation) { ... }  


2. 集成测试文件（api_test.cpp）

#include <gtest/gtest.h>
#include "core/api.h"

TEST(ApiTest, RequestValidation) {
    ApiResponse res = call_api("valid_request");
    EXPECT_TRUE(res.is_valid);
}


四、构建与执行

1. 构建命令

mkdir build && cd build
cmake .. -DENABLE_TESTS=ON  # 开启测试
cmake --build .


2. 运行测试

# 运行所有测试
ctest

# 运行特定测试
./build/tests/unit_tests --gtest_filter=MathTest.*  


3. 测试输出示例


[==========] Running 3 tests from MathTest
[ PASSED ] MathTest.AddPositiveNumbers (0 ms)
[ PASSED ] MathTest.HandleZero (0 ms)
[ PASSED ] MathTest.ComplexOperation (1 ms)


五、最佳实践

1. 编译选项优化  
   在根 CMakeLists.txt 中启用严格编译警告：
   target_compile_options(core_lib PRIVATE -Wall -Wextra -Wpedantic)  
   

2. 跨平台兼容  
   Windows 需显式导出符号：
   set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)  # 避免 DLL 链接问题 
   

3. 依赖更新  
   定期更新 GIT_TAG 至最新稳定版（如 release-1.13.0）。

4. 测试发现  
   使用 gtest_discover_tests 自动注册用例（需 CMake ≥3.10）：
   include(GoogleTest)
   gtest_discover_tests(unit_tests)  
   

总结

通过 FetchContent 动态集成 GTest，结合 option 控制编译开关，实现了轻量级依赖管理；分层级的测试目录结构（单元测试/集成测试分离）和模块化的 CMake 配置，确保了项目的可扩展性。此方案支持多测试文件、跨平台构建，且完全兼容 ctest 工具链，适合中大型 C++ 项目。*/
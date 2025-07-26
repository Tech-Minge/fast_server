# 添加交易模块测试
# 测试 order_handler.cpp
add_executable(test_simple
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
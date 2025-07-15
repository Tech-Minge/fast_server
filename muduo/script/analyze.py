import re
import numpy as np
import pandas as pd
from collections import defaultdict

def parse_spdlog_file(file_path):
    """
    解析spdlog日志文件，提取函数名和耗时（微秒）
    格式示例：[2025-07-14 23:31:36.394] [warning] advance cost: 0us
    """
    pattern = r'\[(.*?)\] \[(\w+)\]\s+(\w+)\s+cost:\s+(\d+)us'
    function_times = defaultdict(list)
    
    with open(file_path, 'r') as f:
        for line in f:
            match = re.match(pattern, line)
            if match:
                timestamp, log_level, func_name, cost_us = match.groups()
                if int(cost_us) > 0:  # 只记录耗时大于0的函数
                    function_times[func_name].append(int(cost_us))
    
    return function_times

def calculate_statistics(function_times):
    """
    计算每个函数的统计指标
    """
    stats = {}
    for func, times in function_times.items():
        if not times:
            continue
            
        arr = np.array(times)
        stats[func] = {
            "count": len(times),
            "average": np.mean(arr),
            "p95": np.percentile(arr, 95),
            "p99": np.percentile(arr, 99),
            "min": np.min(arr),
            "max": np.max(arr),
            "std_dev": np.std(arr)  # 标准差
        }
    return stats

def print_statistics(stats):
    """
    格式化输出统计结果
    """
    print("{:<30} {:<8} {:<10} {:<10} {:<10} {:<10} {:<10} {:<10}".format(
        "Function", "Count", "Avg(us)", "P95(us)", "P99(us)", "Min(us)", "Max(us)", "StdDev"
    ))
    print("-" * 100)
    for func, data in stats.items():
        print("{:<30} {:<8} {:<10.2f} {:<10.2f} {:<10.2f} {:<10} {:<10} {:<10.2f}".format(
            func, 
            data["count"],
            data["average"],
            data["p95"],
            data["p99"],
            int(data["min"]),
            int(data["max"]),
            data["std_dev"]
        ))

if __name__ == "__main__":
    file_path = "log/muduo.log"  # 替换为你的日志文件路径
    function_times = parse_spdlog_file(file_path)
    
    if not function_times:
        print("未找到符合格式的日志记录")
        exit(0)
        
    stats = calculate_statistics(function_times)
    print_statistics(stats)
    
import re
from datetime import datetime
import numpy as np
from scapy.all import PcapReader, UDP
import argparse
import json

# ===================================================
# 1. 解析 tcpdump 的 PCAP 文件
# ===================================================
def parse_pcap(pcap_path: str) -> dict:
    """
    提取 UDP 包的前4字节作为 seqno，记录微秒时间戳
    """
    udp_data = {}
    with PcapReader(pcap_path) as pcap:
        for pkt in pcap:
            if UDP in pkt and pkt[UDP].payload:
                # 提取时间戳（转换为微秒整数）
                timestamp = int(pkt.time * 1e6)  
                # 提取前4字节作为 seqno（大端序）
                payload = bytes(pkt[UDP].payload)
                if len(payload) >= 4:
                    seqno = int.from_bytes(payload[:4], 'big')
                    udp_data[seqno] = timestamp
    return udp_data

# ===================================================
# 2. 解析服务器日志（支持多级别：info/debug/warn等）
# ===================================================
def parse_server_log(log_path: str) -> dict:
    """
    提取日志中的时间戳、日志级别、动作和 seqno
    日志格式示例：[2025-07-21 01:30:39.755119] [info] finish decode 11
    """
    # 正则匹配：时间戳、日志级别、动作、seqno [1,7](@ref)
    log_pattern = re.compile(
        r'\[(?P<date>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]'
        r' \[(?P<level>\w+)\]'
        r' finish (?P<action>\w+) (?P<seqno>\d+)'
    )
    
    action_data = {}
    with open(log_path) as f:
        for line in f:
            match = log_pattern.search(line)
            if match:
                # 转换时间戳到微秒整数
                log_time = datetime.strptime(
                    match.group('date'), 
                    '%Y-%m-%d %H:%M:%S.%f'
                )
                timestamp = int(log_time.timestamp() * 1e6)
                seqno = int(match.group('seqno'))
                action = match.group('action')
                level = match.group('level')  # 提取日志级别
                
                # 按 seqno 和动作存储 [5](@ref)
                if seqno not in action_data:
                    action_data[seqno] = {}
                action_data[seqno][action] = {
                    'timestamp': timestamp,
                    'level': level  # 保留日志级别
                }
    return action_data

# ===================================================
# 3. 对齐数据并计算延迟
# ===================================================
def calculate_latencies(udp_data: dict, action_data: dict) -> dict:
    """
    计算每个动作的延迟（微秒）
    """
    results = {}
    
    for seqno, packet_time in udp_data.items():
        if seqno not in action_data:
            continue
            
        for action, action_info in action_data[seqno].items():
            # 只统计特定动作（如 decode/algo）
            if action not in ['decode', 'algo']:  
                continue
                
            latency = action_info['timestamp'] - packet_time
            if action not in results:
                results[action] = []
            results[action].append({
                'latency': latency,
                'level': action_info['level']  # 关联日志级别
            })
    return results

# ===================================================
# 4. 统计延迟指标
# ===================================================
def generate_stats(latency_list: list) -> dict:
    """
    计算 min/max/mean/percentiles
    """
    arr = np.array([x['latency'] for x in latency_list])
    return {
        'min': np.min(arr),
        'max': np.max(arr),
        'mean': np.mean(arr),
        '50%': np.percentile(arr, 50),
        '90%': np.percentile(arr, 90),
        '99%': np.percentile(arr, 99),
        'samples': len(arr)  # 样本数量
    }

# ===================================================
# 5. 主函数 & 命令行接口
# ===================================================
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pcap', required=True, help='tcpdump抓包文件路径')
    parser.add_argument('--log', required=True, help='服务器日志文件路径')
    parser.add_argument('--output', default='stats.json', help='统计结果输出路径')
    args = parser.parse_args()

    # 解析数据源
    udp_data = parse_pcap(args.pcap)
    action_data = parse_server_log(args.log)
    
    # 计算延迟
    latencies = calculate_latencies(udp_data, action_data)
    
    # 生成统计结果
    stats = {}
    for action, data in latencies.items():
        stats[action] = generate_stats(data)
        stats[action]['level_samples'] = {
            level: sum(1 for x in data if x['level'] == level)
            for level in set(x['level'] for x in data)
        }
    
    # 保存结果
    with open(args.output, 'w') as f:
        json.dump(stats, f, indent=2)
    
    print(f"分析完成！结果已保存至: {args.output}")

if __name__ == '__main__':
    main()
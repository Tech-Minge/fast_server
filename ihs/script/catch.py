import re
import json
import argparse
import numpy as np
from datetime import datetime
from scapy.all import PcapReader, UDP
from termcolor import colored

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
                    # 记录首次出现时间（避免重传包干扰）
                    if seqno not in udp_data:
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
    # 增强版正则：兼容不同日志级别和空格变化
    log_pattern = re.compile(
        r'\[(?P<date>\d{4}-\d{2}-\d{2}\s\d{2}:\d{2}:\d{2}\.\d+)\]\s*'
        r'\[(?P<level>\w+)\]\s*finish\s+(?P<action>\w+)\s+(?P<seqno>\d+)'
    )
    
    action_data = {}
    with open(log_path) as f:
        for line in f:
            match = log_pattern.search(line.strip())
            if match:
                try:
                    log_time = datetime.strptime(
                        match.group('date'), 
                        '%Y-%m-%d %H:%M:%S.%f'
                    )
                    timestamp = int(log_time.timestamp() * 1e6)
                    seqno = int(match.group('seqno'))
                    action = match.group('action').lower()
                    level = match.group('level').lower()
                    
                    # 按 seqno 和动作存储 
                    if seqno not in action_data:
                        action_data[seqno] = {}
                    action_data[seqno][action] = {
                        'timestamp': timestamp,
                        'level': level
                    }
                except ValueError:
                    continue  # 跳过格式错误日志行
    return action_data

# ===================================================
# 3. 对齐数据并计算延迟（新增丢包检测）
# ===================================================
def calculate_latencies(udp_data: dict, action_data: dict) -> dict:
    """
    计算每个动作的延迟（微秒）并检测丢包
    返回: (延迟字典, 丢包列表)
    """
    results = {}
    missing_packets = []
    
    # 第一遍：检测抓包存在但日志缺失的seqno
    for seqno in udp_data:
        if seqno not in action_data:
            missing_packets.append({
                'seqno': seqno,
                'capture_time': udp_data[seqno]
            })
    
    # 第二遍：计算有效seqno的延迟
    for seqno, packet_time in udp_data.items():
        if seqno not in action_data:
            continue  # 已记录为丢包
            
        for action, action_info in action_data[seqno].items():
            # 只统计特定动作（如 decode/algo）
            if action not in ['decode', 'algo']:  
                continue
                
            latency = action_info['timestamp'] - packet_time
            if action not in results:
                results[action] = []
            results[action].append({
                'latency': latency,
                'level': action_info['level'],
                'seqno': seqno
            })
    
    return results, missing_packets

# ===================================================
# 4. 统计延迟指标
# ===================================================
def generate_stats(latency_list: list) -> dict:
    """
    计算 min/max/mean/percentiles
    """
    if not latency_list:
        return {}
        
    latencies = [x['latency'] for x in latency_list]
    arr = np.array(latencies)
    return {
        'min': float(np.min(arr)),
        'max': float(np.max(arr)),
        'mean': float(np.mean(arr)),
        '50%': float(np.percentile(arr, 50)),
        '90%': float(np.percentile(arr, 90)),
        '99%': float(np.percentile(arr, 99)),
        'samples': len(arr)
    }

# ===================================================
# 5. 主函数 & 命令行接口（增强丢包报告）
# ===================================================
def main():
    parser = argparse.ArgumentParser(
        description='网络包处理延迟分析工具（含丢包检测）',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument('--pcap', required=True, help='tcpdump抓包文件路径')
    parser.add_argument('--log', required=True, help='服务器日志文件路径')
    parser.add_argument('--output', default='latency_stats.json', help='统计结果输出路径')
    parser.add_argument('--show-missing', type=int, default=5, 
                        help='控制台显示的丢包示例数量（0=不显示）')
    args = parser.parse_args()

    # 解析数据源
    print(colored("[状态] 解析抓包文件...", 'cyan'))
    udp_data = parse_pcap(args.pcap)
    
    print(colored("[状态] 解析服务器日志...", 'cyan'))
    action_data = parse_server_log(args.log)
    
    print(colored("[状态] 计算延迟并检测丢包...", 'cyan'))
    latencies, missing_packets = calculate_latencies(udp_data, action_data)
    
    # 生成统计结果
    stats = {
        'latency_stats': {action: generate_stats(data) for action, data in latencies.items()},
        'missing_packets': {
            'total': len(missing_packets),
            'rate': f"{len(missing_packets) / len(udp_data) * 100:.2f}%" if udp_data else "N/A",
            'samples': missing_packets[:10]  # 最多保存10个示例
        },
        'summary': {
            'total_captured': len(udp_data),
            'total_logged': len(action_data),
            'matched_packets': sum(len(v) for v in latencies.values())
        }
    }
    
    # 保存结果
    with open(args.output, 'w') as f:
        json.dump(stats, f, indent=2)
    
    # 控制台输出摘要
    print("\n" + "="*50)
    print(colored("分析结果摘要", 'green', attrs=['bold']))
    print("="*50)
    
    # 延迟统计
    for action, data in stats['latency_stats'].items():
        if data:
            print(f"{action.upper()} 延迟统计 (μs):")
            print(f"  样本数: {data['samples']}")
            print(f"  最小值: {data['min']:.2f} | 最大值: {data['max']:.2f}")
            print(f"  平均值: {data['mean']:.2f} | 中位数: {data['50%']:.2f}")
            print(f"  90%分位: {data['90%']:.2f} | 99%分位: {data['99%']:.2f}")
            print("-"*50)
    
    # 丢包报告
    missing_info = stats['missing_packets']
    if missing_info['total'] > 0:
        status = colored(f"发现丢包! 总数: {missing_info['total']} ({missing_info['rate']})", 'red')
        print(status)
        
        if args.show_missing > 0:
            samples = missing_packets[:args.show_missing]
            print(f"示例SeqNo: {', '.join(str(p['seqno']) for p in samples)}")
            
            # 建议检查点
            print("\n" + colored("建议排查:", 'yellow'))
            print("1. 检查日志记录逻辑是否漏处理某些包")
            print("2. 确认日志级别过滤是否导致信息丢失")
            print("3. 检查服务器处理能力是否过载[3](@ref)")
            print("4. 使用命令验证网络丢包: ping -c 100 <目标IP>[1](@ref)")
    else:
        print(colored("未检测到丢包! 所有抓包SeqNo均匹配日志记录", 'green'))
    
    print("\n" + "="*50)
    print(f"完整结果已保存至: {colored(args.output, 'blue')}")
    print("="*50)

if __name__ == '__main__':
    main()
import struct
from scapy.all import *

def extract_seqnos(pcap_file):
    seqnos = set()
    packets = rdpcap(pcap_file)
    for pkt in packets:
        if UDP in pkt and pkt[UDP].dport == 8888 and len(pkt[UDP].payload) >= 4:
            seqno = struct.unpack("!I", bytes(pkt[UDP].payload)[:4])[0]
            seqnos.add(seqno)
    return sorted(seqnos)

def compare_with_log(pcap_seqnos, log_file):
    with open(log_file, "rb") as f:
        log_data = f.read()
    log_seqnos = struct.unpack(f"!{len(log_data)//4}I", log_data)
    
    pcap_set = set(pcap_seqnos)
    log_set = set(log_seqnos)
    
    print(f"Packets in PCAP: {len(pcap_seqnos)}")
    print(f"Packets in log: {len(log_set)}")
    print(f"Missing in log: {sorted(pcap_set - log_set)}")
    print(f"Unexpected in log: {sorted(log_set - pcap_set)}")

if __name__ == "__main__":
    pcap_seqnos = extract_seqnos("capture.pcap")
    compare_with_log(pcap_seqnos, "seqno.log")
    
# sudo tcpdump -i eth0 -w capture.pcap 'udp port 8888'
// g++ -o udp_sender udp_sender.cpp -std=c++11 -O2
// ./udp_sender 
// ./udp_sender 1000 发1000包
// ./udp_sender 500 "Hello" 50	500包，每50ms发1包（20Hz）

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>

// ============ 关键修改：目标改为 AP 板子的 IP ============
#define TARGET_IP "192.168.100.1"   // AP板子（接收端）
#define TARGET_PORT 8888
#define BUFFER_SIZE 2048

struct SensorData {
    uint32_t seq;
    uint64_t timestamp;
    char payload[512];
};

int main(int argc, char* argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TARGET_PORT);
    if (inet_pton(AF_INET, TARGET_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton error");
        close(sockfd);
        return 1;
    }
    
    int total_packets = 100;
    if (argc > 1) {
        total_packets = std::stoi(argv[1]);
    }
    
    std::string payload_text = "Hello from STA";
    if (argc > 2) {
        payload_text = argv[2];
    }
    
    int interval_ms = 0;
    if (argc > 3) {
        interval_ms = std::stoi(argv[3]);
    }
    
    SensorData send_data;
    strncpy(send_data.payload, payload_text.c_str(), sizeof(send_data.payload) - 1);
    send_data.payload[sizeof(send_data.payload) - 1] = '\0';
    
    std::cout << "[STA Sender] Sending " << total_packets << " packets to " 
              << TARGET_IP << ":" << TARGET_PORT << std::endl;
    std::cout << "[STA Sender] Payload: " << payload_text << std::endl;
    std::cout << "[STA Sender] Interval: " << interval_ms << " ms" << std::endl;
    std::cout << "========================================" << std::endl;
    
    int rtt_sum = 0;
    int rtt_count = 0;
    struct sockaddr_in recv_addr;
    socklen_t addr_len = sizeof(recv_addr);
    char recv_buffer[BUFFER_SIZE];
    
    for (int i = 0; i < total_packets; i++) {
        send_data.seq = i + 1;
        send_data.timestamp = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        ssize_t sent_bytes = sendto(sockfd, &send_data, sizeof(send_data), 0,
                                     (const struct sockaddr *)&server_addr, sizeof(server_addr));
        
        if (sent_bytes < 0) {
            perror("sendto failed");
            break;
        }
        
        // 等待AP回显（确认收到）
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        
        int n = recvfrom(sockfd, recv_buffer, BUFFER_SIZE, 0,
                         (struct sockaddr *)&recv_addr, &addr_len);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        if (n < 0) {
            std::cout << "[STA Sender] ❌ Packet " << (i + 1) << " timed out (lost)" << std::endl;
        } else {
            SensorData* recv_data = (SensorData*)recv_buffer;
            if (recv_data->seq == send_data.seq) {
                rtt_sum += rtt_us;
                rtt_count++;
                if ((i + 1) % 10 == 0) {
                    std::cout << "[STA Sender] Packet " << (i + 1) << " RTT = " 
                              << rtt_us / 1000 << " ms" << std::endl;
                }
            }
        }
        
        if (interval_ms > 0) {
            usleep(interval_ms * 1000);
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "[STA Sender] Total sent: " << total_packets << std::endl;
    std::cout << "[STA Sender] Total received (echo): " << rtt_count << std::endl;
    std::cout << "[STA Sender] Lost packets: " << (total_packets - rtt_count) << std::endl;
    if (rtt_count > 0) {
        std::cout << "[STA Sender] Average RTT: " << (rtt_sum / rtt_count) / 1000 << " ms" << std::endl;
    }
    
    close(sockfd);
    return 0;
}
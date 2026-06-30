//g++ -o udp_receiver udp_receiver.cpp -std=c++11 -O2
//./udp_receiver 

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8888
#define BUFFER_SIZE 2048

struct SensorData {
    uint32_t seq;
    uint64_t timestamp;
    char payload[512];
};

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }
    
    std::cout << "[AP Receiver] UDP Receiver started on port " << PORT << std::endl;
    std::cout << "[AP Receiver] Waiting for data from STA ..." << std::endl;
    std::cout << "========================================" << std::endl;
    
    int packet_count = 0;
    uint32_t last_seq = 0;
    int lost_total = 0;
    
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr *)&client_addr, &addr_len);
        
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }
        
        if (n != sizeof(SensorData)) {
            std::cout << "[AP Receiver] Received " << n << " bytes (ignored)" << std::endl;
            continue;
        }
        
        SensorData* data = (SensorData*)buffer;
        packet_count++;
        
        // 检测丢包
        if (last_seq != 0 && data->seq > last_seq + 1) {
            int lost = data->seq - last_seq - 1;
            lost_total += lost;
            std::cout << "[AP Receiver]  Lost " << lost << " packet(s) between " 
                      << last_seq << " and " << data->seq 
                      << " (total lost: " << lost_total << ")" << std::endl;
        }
        last_seq = data->seq;
        
        // 每10包打印统计
        if (packet_count % 10 == 0) {
            std::cout << "[AP Receiver] Received " << packet_count << " packets, "
                      << "last seq=" << data->seq 
                      << ", lost=" << lost_total << std::endl;
            // 打印收到的payload内容
            std::cout << "[AP Receiver] Payload: " << data->payload << std::endl;
        }
        
        // ===== 回显功能：收到后原路返回给STA =====
        sendto(sockfd, buffer, n, 0, 
               (const struct sockaddr *)&client_addr, addr_len);
    }
    
    close(sockfd);
    return 0;
}
// BLR-4000 UDP接收端
// 编译: g++ -std=c++11 -o BLR4000_receiver BLR4000_receiver.cpp
// 运行: ./BLR4000_receiver

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#define PORT 8888
#define BUFFER_SIZE 2048

// 必须和发送端定义一致
#pragma pack(push, 1)
struct SensorData {
    uint32_t seq;
    uint64_t timestamp_ms;
    int32_t distance_mm;
    uint16_t crc;
};
#pragma pack(pop)

static volatile bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);

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

    if (bind(sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }

    std::cout << "============================================" << std::endl;
    std::cout << "  BLR-4000 UDP Receiver (AP)" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "  监听端口: " << PORT << std::endl;
    std::cout << "  等待数据..." << std::endl;
    std::cout << "============================================" << std::endl;

    int packet_count = 0;
    uint32_t last_seq = 0;
    int lost_total = 0;

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr*)&client_addr, &addr_len);

        if (n < 0) {
            perror("recvfrom error");
            continue;
        }

        if (n != sizeof(SensorData)) {
            std::cout << "[AP] 收到 " << n << " 字节，格式不匹配" << std::endl;
            continue;
        }

        SensorData* data = (SensorData*)buffer;
        packet_count++;

        // 检测丢包
        if (last_seq != 0 && data->seq > last_seq + 1) {
            int lost = data->seq - last_seq - 1;
            lost_total += lost;
            std::cout << "[AP] ⚠️ 丢包: " << lost << " 包 (seq " 
                      << last_seq << " -> " << data->seq << ")" << std::endl;
        }
        last_seq = data->seq;

        // 显示距离
        if (data->crc == 0xFFFF) {
            std::cout << "[AP] seq=" << data->seq << " [读取失败]" << std::endl;
        } else if (data->distance_mm < 0) {
            std::cout << "[AP] seq=" << data->seq << " 距离=超出量程 (>4m or <5cm)" << std::endl;
        } else {
            std::cout << "[AP] seq=" << data->seq 
                      << " 距离=" << data->distance_mm << " mm"
                      << " (" << data->distance_mm / 1000.0f << " m)" << std::endl;
        }

        // 每10包打印丢包统计
        if (packet_count % 10 == 0) {
            std::cout << "[AP] 统计: 收到 " << packet_count << " 包, 丢包累计 " << lost_total << std::endl;
        }
    }

    close(sockfd);
    std::cout << std::endl << "程序已退出" << std::endl;
    return 0;
}
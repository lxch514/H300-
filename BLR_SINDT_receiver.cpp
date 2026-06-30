// BLR-4000 + SINDT-485 双传感器 UDP接收 同一串口 
// 编译: g++ -std=c++11 -o BLR_SINDT_receiver BLR_SINDT_receiver.cpp
// 运行: ./BLR_SINDT_receiver

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <iomanip>

#define PORT 8888
#define BUFFER_SIZE 2048

#pragma pack(push, 1)
struct SensorData {
    uint32_t seq;
    uint64_t timestamp_ms;
    float distance_m;
    bool blr_ok;
    float roll;
    float pitch;
    float yaw;
    bool sint_ok;
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

    std::cout << "==================================================" << std::endl;
    std::cout << "  BLR-4000 + SINDT-485 UDP接收 " << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "  监听端口: " << PORT << std::endl;
    std::cout << "  等待数据..." << std::endl;
    std::cout << "==================================================" << std::endl;

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
            std::cout << "[AP] 收到 " << n << " 字节，格式不匹配 (期望 " 
                      << sizeof(SensorData) << ")" << std::endl;
            continue;
        }

        SensorData* data = (SensorData*)buffer;
        packet_count++;

        // 检测丢包
        if (last_seq != 0 && data->seq > last_seq + 1) {
            int lost = data->seq - last_seq - 1;
            lost_total += lost;
            std::cout << "[AP]  丢包: " << lost << " 包 (seq " 
                      << last_seq << " -> " << data->seq << ")" << std::endl;
        }
        last_seq = data->seq;

        // ----- 显示所有传感器数据 -----
        std::cout << "[AP] seq=" << data->seq << std::fixed;

        // BLR-4000
        if (data->blr_ok) {
            if (data->distance_m < 0) {
                std::cout << " 距离=超量程";
            } else {
                std::cout << " 距离=" << std::setprecision(3) << data->distance_m << "m";
            }
        } else {
            std::cout << " 距离=--";
        }

        // SINDT-485 角度
        if (data->sint_ok) {
            std::cout << " 角度(R/P/Y)=" << std::setprecision(1) 
                      << data->roll << "°/" << data->pitch << "°/" << data->yaw << "°";
        } else {
            std::cout << " 角度=--";
        }

        std::cout << std::endl;

        // 每10包打印统计
        if (packet_count % 10 == 0) {
            std::cout << "[AP] 统计: 收到 " << packet_count << " 包, 丢包累计 " << lost_total << std::endl;
        }
    }

    close(sockfd);
    std::cout << std::endl << "程序已退出" << std::endl;
    return 0;
}
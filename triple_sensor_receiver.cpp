// BLR-4000 + SINDT-485 + AFF500RS 三传感器 UDP接收 
// 编译: g++ -std=c++11 -o triple_sensor_receiver triple_sensor_receiver.cpp
// 运行: ./triple_sensor_receiver

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <iomanip>
#include <chrono>

#define PORT 8080
#define BUFFER_SIZE 2048

#define WATCHDOG_TIMEOUT_SEC  5
#define WATCHDOG_EXIT_SEC    30

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
    float wind_speed;
    bool wind_ok;
    uint16_t crc;
};
#pragma pack(pop)

static volatile bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

uint64_t get_current_ms() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
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
    std::cout << "  三传感器 UDP接收 + 通信看门狗 (AP)" << std::endl;
    std::cout << "  BLR-4000 + SINDT-485 + AFF500RS" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "  监听端口: " << PORT << std::endl;
    std::cout << "  看门狗  : " << WATCHDOG_TIMEOUT_SEC << "秒告警, " 
              << WATCHDOG_EXIT_SEC << "秒自动退出" << std::endl;
    std::cout << "  等待数据..." << std::endl;
    std::cout << "==================================================" << std::endl;

    int packet_count = 0;
    uint32_t last_seq = 0;
    int lost_total = 0;

    uint64_t last_recv_time = get_current_ms();
    bool first_packet_received = false;

    while (running) {
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr*)&client_addr, &addr_len);

        uint64_t now = get_current_ms();

        if (n < 0) {
            if (first_packet_received) {
                uint64_t elapsed = now - last_recv_time;
                if (elapsed > WATCHDOG_TIMEOUT_SEC * 1000) {
                    std::cout << "[WATCHDOG]   " << (elapsed / 1000) 
                              << "秒未收到数据" << std::endl;
                }
                if (elapsed > WATCHDOG_EXIT_SEC * 1000) {
                    std::cout << "[WATCHDOG]  " << WATCHDOG_EXIT_SEC 
                              << "秒未收到数据，连接中断!" << std::endl;
                    break;
                }
            }
            continue;
        }

        first_packet_received = true;
        last_recv_time = now;

        if (n != sizeof(SensorData)) {
            std::cout << "[AP] 收到 " << n << " 字节，格式不匹配" << std::endl;
            continue;
        }

        SensorData* data = (SensorData*)buffer;
        packet_count++;

        if (last_seq != 0 && data->seq > last_seq + 1) {
            int lost = data->seq - last_seq - 1;
            lost_total += lost;
            std::cout << "[AP]  丢包: " << lost << " 包 (seq " 
                      << last_seq << " -> " << data->seq << ")" << std::endl;
        }
        last_seq = data->seq;

        // ----- 显示 -----
        std::cout << "[AP] seq=" << data->seq << std::fixed;

        // 距离
        if (data->blr_ok) {
            if (data->distance_m < 0) {
                std::cout << " 距离=超量程";
            } else {
                std::cout << " 距离=" << std::setprecision(3) << data->distance_m << "m";
            }
        } else {
            std::cout << " 距离=--";
        }

        // 角度
        if (data->sint_ok) {
            std::cout << " 角度(R/P/Y)=" << std::setprecision(1) 
                      << data->roll << "°/" << data->pitch << "°/" << data->yaw << "°";
        } else {
            std::cout << " 角度=--";
        }

        // 风速
        if (data->wind_ok) {
            std::cout << " 风速=" << std::setprecision(3) << data->wind_speed << "m/s";
        } else {
            std::cout << " 风速=--";
        }

        std::cout << std::endl;

        if (packet_count % 10 == 0) {
            std::cout << "[AP] 统计: 收到 " << packet_count << " 包, 丢包累计 " << lost_total << std::endl;
        }
    }

    close(sockfd);
    std::cout << std::endl << "程序已退出" << std::endl;
    return 0;
}

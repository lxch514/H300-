// g++ -o udp_receiver_hello udp_receiver_hello.cpp -std=c++11
//./udp_receiver_hello

#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8888
#define BUFFER_SIZE 2048

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // 1. 创建UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
    // 2. 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网卡
    server_addr.sin_port = htons(PORT);
    
    // 3. 绑定端口
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }
    
    std::cout << "[STA] UDP Receiver started on port " << PORT << std::endl;
    std::cout << "[STA] Waiting for data from AP..." << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 4. 循环接收数据
    int packet_count = 0;
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr *)&client_addr, &addr_len);
        
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }
        
        packet_count++;
        buffer[n] = '\0';
        
        // 获取发送端IP和端口
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        
        // 打印接收信息
        std::cout << "[" << packet_count << "] Received " << n << " bytes from " 
                  << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
        std::cout << "    Data: " << buffer << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }
    
    close(sockfd);
    return 0;
}
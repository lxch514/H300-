// g++ -o udp_sender_hello udp_sender_hello.cpp -std=c++11
//./udp_sender_hello

#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define TARGET_IP "192.168.100.2"
#define TARGET_PORT 8888
#define BUFFER_SIZE 2048

int main(int argc, char* argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // 1. 创建UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
    // 2. 设置目标地址（STA板子）
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TARGET_PORT);
    if (inet_pton(AF_INET, TARGET_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton error");
        close(sockfd);
        return 1;
    }
    
    // 3. 准备要发送的数据
    std::string message;
    if (argc > 1) {
        // 如果命令行有参数，用参数作为消息
        message = argv[1];
    } else {
        message = "Hello from AP, Hello to STA";
    }
    
    // 4. 发送数据
    ssize_t sent_bytes = sendto(sockfd, message.c_str(), message.length(), 0,(const struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (sent_bytes < 0) {
        perror("sendto failed");
        close(sockfd);
        return 1;
    }
    
    std::cout << "[AP] Sent " << sent_bytes << " bytes to " << TARGET_IP << ":" << TARGET_PORT << std::endl;
    std::cout << "[AP] Message: " << message << std::endl;
    
    close(sockfd);
    return 0;
}
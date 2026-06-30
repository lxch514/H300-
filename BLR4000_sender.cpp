// BLR-4000-485 + UDP发送
// 编译: g++ -std=c++11 -o BLR4000_sender BLR4000_sender.cpp -lpthread
// 运行: ./BLR4000_sender

#include <iostream>
#include <string>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

// ============================================================
// 1. UDP 发送目标配置
// ============================================================
#define TARGET_IP "192.168.100.1"   // AP板子（接收端）
#define TARGET_PORT 8888

// ============================================================
// 2. 传感器数据结构体（发送给AP）
// ============================================================
#pragma pack(push, 1)
struct SensorData {
    uint32_t seq;           // 包序号
    uint64_t timestamp_ms;  // 采集时间戳（毫秒）
    int32_t distance_mm;    // 距离值（mm），-1表示超量程
    uint16_t crc;           // 简单校验（可选）
};
#pragma pack(pop)

// ============================================================
// 3. SerialPort 类 - 串口通信封装
// ============================================================
class SerialPort {
public:
    SerialPort() : fd_(-1) {}
    ~SerialPort() { close(); }

    bool open(const std::string& port, uint32_t baudrate) {
        fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0) {
            std::cerr << "Failed to open " << port << ": " << strerror(errno) << std::endl;
            return false;
        }

        struct termios options;
        tcgetattr(fd_, &options);
        cfmakeraw(&options);

        options.c_cflag &= ~CRTSCTS;
        options.c_iflag &= ~(IXON | IXOFF | IXANY);

        speed_t speed;
        switch (baudrate) {
            case 9600:   speed = B9600; break;
            case 19200:  speed = B19200; break;
            case 38400:  speed = B38400; break;
            case 57600:  speed = B57600; break;
            case 115200: speed = B115200; break;
            case 230400: speed = B230400; break;
            case 460800: speed = B460800; break;
            default:     speed = B9600; break;
        }
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);

        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag |= CREAD | CLOCAL;

        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 5;

        tcsetattr(fd_, TCSANOW, &options);
        tcflush(fd_, TCIOFLUSH);

        return true;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool is_open() const { return fd_ >= 0; }

    int write(const uint8_t* data, uint32_t size) {
        if (fd_ < 0) return -1;
        return ::write(fd_, data, size);
    }

    int read(uint8_t* buffer, uint32_t buffer_size, int timeout_ms) {
        if (fd_ < 0) return -1;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd_, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ret <= 0) {
            return 0;
        }

        return ::read(fd_, buffer, buffer_size);
    }

private:
    int fd_;
};

// ============================================================
// 4. BLR4000Sensor 类 - 传感器驱动
// ============================================================
class BLR4000Sensor {
public:
    BLR4000Sensor() : addr_(1), initialized_(false) {}
    ~BLR4000Sensor() { close(); }

    bool initialize(const std::string& port, uint32_t baudrate = 115200, uint8_t address = 1) {
        addr_ = address;
        if (!serial_.open(port, baudrate)) {
            std::cerr << "Failed to open serial port: " << port << std::endl;
            return false;
        }
        initialized_ = true;
        return true;
    }

    void close() {
        if (initialized_) {
            serial_.close();
            initialized_ = false;
        }
    }

    bool is_initialized() const { return initialized_; }

    bool readDistance(int32_t& distance_mm) {
        uint16_t value;
        if (!readRegister(0x0000, value)) {
            return false;
        }
        if (value == 0xFFFF) {
            distance_mm = -1;
        } else {
            distance_mm = value;
        }
        return true;
    }

private:
    static uint16_t crc16(const uint8_t* data, uint32_t len) {
        uint16_t crc = 0xFFFF;
        for (uint32_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    bool sendRequest(const uint8_t* req, uint32_t req_len, uint8_t* resp, uint32_t& resp_len, int timeout_ms = 100) {
        if (!initialized_ || !serial_.is_open()) {
            return false;
        }

        uint8_t dummy[64];
        while (serial_.read(dummy, sizeof(dummy), 10) > 0) {}

        int written = serial_.write(req, req_len);
        if (written != (int)req_len) {
            return false;
        }

        uint8_t buffer[256];
        uint32_t total_read = 0;
        int retry = 0;
        const int max_retry = 3;

        while (total_read < 6 && retry < max_retry) {
            int n = serial_.read(buffer + total_read, sizeof(buffer) - total_read, timeout_ms);
            if (n > 0) {
                total_read += n;
            } else {
                retry++;
                usleep(10000);
            }
        }

        if (total_read < 6) {
            return false;
        }

        if (buffer[0] != req[0] || buffer[1] != req[1]) {
            return false;
        }

        uint16_t calc_crc = crc16(buffer, total_read - 2);
        uint16_t resp_crc = (buffer[total_read - 1] << 8) | buffer[total_read - 2];
        if (calc_crc != resp_crc) {
            return false;
        }

        memcpy(resp, buffer, total_read);
        resp_len = total_read;
        return true;
    }

    bool readRegister(uint16_t reg_addr, uint16_t& value) {
        uint8_t req[8];
        uint32_t idx = 0;
        req[idx++] = addr_;
        req[idx++] = 0x03;
        req[idx++] = (reg_addr >> 8) & 0xFF;
        req[idx++] = reg_addr & 0xFF;
        req[idx++] = 0x00;
        req[idx++] = 0x01;
        uint16_t crc = crc16(req, idx);
        req[idx++] = crc & 0xFF;
        req[idx++] = (crc >> 8) & 0xFF;

        uint8_t resp[256];
        uint32_t resp_len = 0;
        if (!sendRequest(req, idx, resp, resp_len)) {
            return false;
        }

        if (resp_len < 5 || resp[2] != 0x02) {
            return false;
        }

        value = (resp[3] << 8) | resp[4];
        return true;
    }

    SerialPort serial_;
    uint8_t addr_;
    bool initialized_;
};

// ============================================================
// 5. UDP发送封装
// ============================================================
class UDPSender {
public:
    UDPSender() : sockfd_(-1) {}

    bool init(const std::string& target_ip, uint16_t target_port) {
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0) {
            perror("socket creation failed");
            return false;
        }

        memset(&target_addr_, 0, sizeof(target_addr_));
        target_addr_.sin_family = AF_INET;
        target_addr_.sin_port = htons(target_port);
        if (inet_pton(AF_INET, target_ip.c_str(), &target_addr_.sin_addr) <= 0) {
            perror("inet_pton error");
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        return true;
    }

    ~UDPSender() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }

    bool send(const void* data, size_t len) {
        if (sockfd_ < 0) return false;
        ssize_t sent = sendto(sockfd_, data, len, 0,
                              (const struct sockaddr*)&target_addr_,
                              sizeof(target_addr_));
        return (sent == (ssize_t)len);
    }

private:
    int sockfd_;
    struct sockaddr_in target_addr_;
};

// ============================================================
// 6. 主程序
// ============================================================
static volatile bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

uint64_t get_timestamp_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);

    // ---------- 参数 ----------
    std::string port = "/dev/ttyS0";
    uint32_t baudrate = 9600;
    uint8_t address = 0x53;

    if (argc >= 2) port = argv[1];
    if (argc >= 3) baudrate = std::stoul(argv[2]);
    if (argc >= 4) address = (uint8_t)std::stoul(argv[3]);

    std::cout << "============================================" << std::endl;
    std::cout << "  BLR-4000 + UDP Sender (STA -> AP)" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "  串口     : " << port << std::endl;
    std::cout << "  波特率   : " << baudrate << std::endl;
    std::cout << "  从机地址 : 0x" << std::hex << (int)address << std::dec << std::endl;
    std::cout << "  目标IP   : " << TARGET_IP << ":" << TARGET_PORT << std::endl;
    std::cout << "============================================" << std::endl;

    // ---------- 初始化传感器 ----------
    BLR4000Sensor sensor;
    if (!sensor.initialize(port, baudrate, address)) {
        std::cerr << "Failed to initialize sensor!" << std::endl;
        return -1;
    }

    // ---------- 初始化UDP ----------
    UDPSender udp;
    if (!udp.init(TARGET_IP, TARGET_PORT)) {
        std::cerr << "Failed to initialize UDP!" << std::endl;
        return -1;
    }

    // ---------- 主循环 ----------
    uint32_t seq = 0;
    while (running) {
        int32_t distance_mm;
        SensorData data;
        data.seq = ++seq;
        data.timestamp_ms = get_timestamp_ms();

        if (sensor.readDistance(distance_mm)) {
            data.distance_mm = distance_mm;
            // 简单CRC校验（可选）
            data.crc = 0;
        } else {
            data.distance_mm = 0;  // 读取失败标记
            data.crc = 0xFFFF;
            std::cerr << "[STA] 读取传感器失败!" << std::endl;
        }

        // 发送UDP
        if (udp.send(&data, sizeof(data))) {
            if (seq % 10 == 0) {
                if (distance_mm < 0) {
                    std::cout << "[STA] 发送: seq=" << seq << ", 距离=超出量程" << std::endl;
                } else {
                    std::cout << "[STA] 发送: seq=" << seq << ", 距离=" 
                              << distance_mm << " mm (" << distance_mm/1000.0f << " m)" << std::endl;
                }
            }
        } else {
            std::cerr << "[STA] UDP发送失败!" << std::endl;
        }

        usleep(1000000);  // 1秒采集一次
    }

    sensor.close();
    std::cout << std::endl << "程序已退出" << std::endl;
    return 0;
}
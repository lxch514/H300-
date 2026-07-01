// BLR-4000 + SINDT-485 双传感器 + UDP发送
// 编译: g++ -std=c++11 -o BLR_SINDT_sender BLR_SINDT_sender.cpp -lpthread
// 运行: ./BLR_SINDT_sender /dev/ttyS0 9600 0x53 0x65

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
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <pthread.h>   // 新增：用于看门狗线程

// ============================================================
// 0. UDP 配置
// ============================================================
#define TARGET_IP "192.168.100.1"
#define TARGET_PORT 8080

// ============================================================
// 1. 数据结构（发送给AP）
// ============================================================
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

// ============================================================
// 2. SerialPort 类
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
        if (ret <= 0) return 0;

        return ::read(fd_, buffer, buffer_size);
    }

private:
    int fd_;
};

// ============================================================
// 3. Modbus 底层工具
// ============================================================
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

bool sendModbusRequest(SerialPort& serial, uint8_t* req, uint32_t req_len,
                        uint8_t* resp, uint32_t& resp_len, int timeout_ms = 200) {
    if (!serial.is_open()) return false;

    uint8_t dummy[64];
    while (serial.read(dummy, sizeof(dummy), 10) > 0) {}

    int written = serial.write(req, req_len);
    if (written != (int)req_len) {
        return false;
    }

    uint8_t buffer[256];
    uint32_t total_read = 0;
    int retry = 0;
    const int max_retry = 4;
    uint32_t expected_min = 5;

    while (total_read < expected_min && retry < max_retry) {
        int n = serial.read(buffer + total_read, sizeof(buffer) - total_read, timeout_ms);
        if (n > 0) {
            total_read += n;
            if (total_read >= 3) {
                expected_min = 3 + buffer[2] + 2;
            }
        } else {
            retry++;
            usleep(5000);
        }
    }

    if (total_read < expected_min) {
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

// ============================================================
// 4. BLR-4000 读取
// ============================================================
bool readBLR4000(SerialPort& serial, uint8_t addr, float& distance_m) {
    uint8_t req[8];
    req[0] = addr;
    req[1] = 0x04;
    req[2] = 0x00;
    req[3] = 0x00;
    req[4] = 0x00;
    req[5] = 0x01;
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    uint8_t resp[256];
    uint32_t resp_len = 0;
    if (!sendModbusRequest(serial, req, 8, resp, resp_len)) {
        return false;
    }

    if (resp_len < 5 || resp[2] != 0x02) return false;

    uint16_t dist_mm = (resp[3] << 8) | resp[4];
    if (dist_mm == 0xFFFF) {
        distance_m = -1.0f;
    } else {
        distance_m = dist_mm / 1000.0f;
    }
    return true;
}

// ============================================================
// 5. SINDT-485 角度读取
// ============================================================
bool readSINDT_Angle(SerialPort& serial, uint8_t addr, float& roll, float& pitch, float& yaw) {
    uint8_t req[8];
    req[0] = addr;
    req[1] = 0x03;
    req[2] = 0x00;
    req[3] = 0x3D;
    req[4] = 0x00;
    req[5] = 0x03;
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    uint8_t resp[256];
    uint32_t resp_len = 0;
    if (!sendModbusRequest(serial, req, 8, resp, resp_len)) {
        return false;
    }

    if (resp_len < 5 || resp[2] != 0x06) return false;

    roll  = (int16_t)((resp[3] << 8) | resp[4]) / 32768.0f * 180.0f;
    pitch = (int16_t)((resp[5] << 8) | resp[6]) / 32768.0f * 180.0f;
    yaw   = (int16_t)((resp[7] << 8) | resp[8]) / 32768.0f * 180.0f;
    return true;
}

// ============================================================
// 6. UDP发送封装
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
// 7. 获取时间戳
// ============================================================
uint64_t get_timestamp_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// ============================================================
// 8. 软件看门狗
// ============================================================
static volatile bool running = true;
static volatile uint32_t heartbeat = 0;      // 主循环心跳计数器
static volatile bool watchdog_triggered = false;

void* watchdog_thread_func(void* arg) {
    (void)arg;
    uint32_t last_heartbeat = 0;
    int timeout_count = 0;
    
    while (!watchdog_triggered && running) {
        sleep(2);  // 每2秒检查一次
        
        if (heartbeat == last_heartbeat) {
            timeout_count++;
            std::cerr << "[WATCHDOG] 主循环无响应 " << timeout_count << " 次" << std::endl;
            if (timeout_count >= 3) {  // 6秒无变化，判定为卡死
                std::cerr << "[WATCHDOG]  主循环卡死! 程序即将退出..." << std::endl;
                watchdog_triggered = true;
                running = false;
                exit(1);  // 主动退出，让外部脚本重启
            }
        } else {
            timeout_count = 0;
            last_heartbeat = heartbeat;
        }
    }
    return nullptr;
}

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [port] [baudrate] [addr_BLR] [addr_SINDT]" << std::endl;
    std::cout << "  port        : serial device, default /dev/ttyS0" << std::endl;
    std::cout << "  baudrate    : default 9600" << std::endl;
    std::cout << "  addr_BLR    : BLR-4000 address, default 0x53" << std::endl;
    std::cout << "  addr_SINDT  : SINDT-485 address, default 0x65" << std::endl;
    std::cout << std::endl;
    std::cout << "Example: " << prog_name << " /dev/ttyS0 9600 0x53 0x65" << std::endl;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);

    // ---------- 解析参数 ----------
    std::string port = "/dev/ttyS0";
    uint32_t baudrate = 9600;
    uint8_t addr_BLR = 0x53;
    uint8_t addr_SINDT = 0x65;

    if (argc >= 2) {
        if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        port = argv[1];
    }
    if (argc >= 3) baudrate = std::stoul(argv[2]);
    if (argc >= 4) {
        std::string s = argv[3];
        addr_BLR = (s.find("0x") == 0) ? (uint8_t)std::stoul(s, nullptr, 16) : (uint8_t)std::stoul(s);
    }
    if (argc >= 5) {
        std::string s = argv[4];
        addr_SINDT = (s.find("0x") == 0) ? (uint8_t)std::stoul(s, nullptr, 16) : (uint8_t)std::stoul(s);
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "  BLR-4000 + SINDT-485 + UDP发送 + 看门狗" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "  串口      : " << port << std::endl;
    std::cout << "  波特率    : " << baudrate << std::endl;
    std::cout << "  BLR-4000  : 0x" << std::hex << (int)addr_BLR << std::dec << std::endl;
    std::cout << "  SINDT-485 : 0x" << std::hex << (int)addr_SINDT << std::dec << std::endl;
    std::cout << "  目标IP    : " << TARGET_IP << ":" << TARGET_PORT << std::endl;
    std::cout << "  看门狗    : 已启用 (6秒无响应自动退出)" << std::endl;
    std::cout << "==================================================" << std::endl;

    // ---------- 打开串口 ----------
    SerialPort serial;
    if (!serial.open(port, baudrate)) {
        std::cerr << "无法打开串口 " << port << std::endl;
        return -1;
    }

    // ---------- 初始化UDP ----------
    UDPSender udp;
    if (!udp.init(TARGET_IP, TARGET_PORT)) {
        std::cerr << "UDP初始化失败!" << std::endl;
        return -1;
    }

    // ---------- 启动看门狗线程 ----------
    pthread_t watchdog_thread;
    if (pthread_create(&watchdog_thread, nullptr, watchdog_thread_func, nullptr) != 0) {
        std::cerr << "看门狗线程创建失败!" << std::endl;
        return -1;
    }

    std::cout << "按 Ctrl+C 停止发送" << std::endl << std::endl;

    // ---------- 主循环 ----------
    uint32_t seq = 0;
    while (running) {
        seq++;
        SensorData data;
        memset(&data, 0, sizeof(data));
        data.seq = seq;
        data.timestamp_ms = get_timestamp_ms();

        // ----- 读取 BLR-4000 -----
        data.blr_ok = readBLR4000(serial, addr_BLR, data.distance_m);

        // ----- 读取 SINDT-485 角度 -----
        data.sint_ok = readSINDT_Angle(serial, addr_SINDT, data.roll, data.pitch, data.yaw);

        data.crc = 0;

        // ----- UDP发送 -----
        if (udp.send(&data, sizeof(data))) {
            if (seq % 5 == 0) {
                std::cout << "[STA] seq=" << seq 
                          << " 距离=" << std::fixed << std::setprecision(3) << data.distance_m << "m"
                          << " 角度(R/P/Y)=" << std::setprecision(1) 
                          << data.roll << "°/" << data.pitch << "°/" << data.yaw << "°" << std::endl;
            }
        } else {
            std::cerr << "[STA] UDP发送失败!" << std::endl;
        }

        // ----- 喂狗：更新心跳计数器 -----
        heartbeat++;

        usleep(500000);  // 0.5秒采集一次
    }

    // ---------- 清理 ----------
    running = false;
    pthread_join(watchdog_thread, nullptr);
    serial.close();
    std::cout << std::endl << "程序已退出" << std::endl;
    return 0;
}
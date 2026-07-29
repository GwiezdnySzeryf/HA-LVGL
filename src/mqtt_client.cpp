#include "mqtt_client.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>

static std::vector<uint8_t> encode_remaining_length(size_t length) {
    std::vector<uint8_t> encoded;
    do {
        uint8_t d = length % 128;
        length /= 128;
        if (length > 0) d |= 128;
        encoded.push_back(d);
    } while (length > 0);
    return encoded;
}

static size_t decode_remaining_length(const std::function<int(uint8_t*)>& read_one, bool& ok) {
    size_t multiplier = 1;
    size_t value = 0;
    uint8_t encoded_byte;
    do {
        if (read_one(&encoded_byte) <= 0) {
            ok = false;
            return 0;
        }
        value += (encoded_byte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) {
            ok = false;
            return 0;
        }
    } while ((encoded_byte & 128) != 0);
    ok = true;
    return value;
}

MqttClient::MqttClient() {}

MqttClient::~MqttClient() {
    stop();
}

void MqttClient::configure(const MqttConfig& config) {
    config_ = config;
    if (config_.base_topic.empty()) config_.base_topic = "panel/tpp01";
    if (config_.port <= 0) config_.port = 1883;
    if (config_.client_id.empty()) config_.client_id = "tpp01_panel";
}

void MqttClient::start() {
    if (running_) return;
    if (!config_.enabled || config_.host.empty()) {
        std::cout << "[MQTT] Client disabled or host not configured." << std::endl;
        return;
    }

    running_ = true;
    loop_thread_ = std::thread(&MqttClient::loop, this);
}

void MqttClient::stop() {
    running_ = false;
    close_socket();
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

bool MqttClient::connect_socket() {
    close_socket();

    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(config_.port);
    if (getaddrinfo(config_.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        std::cout << "[MQTT ❌] DNS lookup failed for: " << config_.host << std::endl;
        return false;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return false;
    }

    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return false;
    }

    freeaddrinfo(res);
    sock_fd_ = fd;
    return true;
}

void MqttClient::close_socket() {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
    connected_ = false;
}

bool MqttClient::send_raw(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    if (sock_fd_ < 0) return false;

    size_t sent = 0;
    while (sent < len) {
        ssize_t res = write(sock_fd_, data + sent, len - sent);
        if (res <= 0) return false;
        sent += res;
    }
    return true;
}

int MqttClient::read_raw(uint8_t* buf, size_t len, int timeout_ms) {
    if (sock_fd_ < 0) return -1;

    struct pollfd pfd;
    pfd.fd = sock_fd_;
    pfd.events = POLLIN;

    int poll_res = poll(&pfd, 1, timeout_ms);
    if (poll_res <= 0) return poll_res; // 0 = timeout, <0 = error

    ssize_t res = read(sock_fd_, buf, len);
    return (int)res;
}

bool MqttClient::send_mqtt_connect() {
    std::vector<uint8_t> var_header;

    // Protocol Name: "MQTT"
    var_header.push_back(0x00);
    var_header.push_back(0x04);
    var_header.push_back('M');
    var_header.push_back('Q');
    var_header.push_back('T');
    var_header.push_back('T');

    // Protocol Level: 4 (MQTT 3.1.1)
    var_header.push_back(0x04);

    // Connect Flags
    uint8_t flags = 0x02; // Clean session
    
    // Will Flag (LWT)
    std::string lwt_topic = config_.base_topic + "/state/status";
    std::string lwt_payload = "offline";
    flags |= 0x04; // Will Flag
    flags |= (1 << 3); // Will QoS 1
    flags |= 0x20; // Will Retain

    if (!config_.username.empty()) flags |= 0x80;
    if (!config_.password.empty()) flags |= 0x40;

    var_header.push_back(flags);

    // Keepalive: 60s
    var_header.push_back(0x00);
    var_header.push_back(0x3C);

    // Payload
    std::vector<uint8_t> payload;

    // Client ID
    payload.push_back((config_.client_id.length() >> 8) & 0xFF);
    payload.push_back(config_.client_id.length() & 0xFF);
    payload.insert(payload.end(), config_.client_id.begin(), config_.client_id.end());

    // LWT Topic & Payload
    payload.push_back((lwt_topic.length() >> 8) & 0xFF);
    payload.push_back(lwt_topic.length() & 0xFF);
    payload.insert(payload.end(), lwt_topic.begin(), lwt_topic.end());

    payload.push_back((lwt_payload.length() >> 8) & 0xFF);
    payload.push_back(lwt_payload.length() & 0xFF);
    payload.insert(payload.end(), lwt_payload.begin(), lwt_payload.end());

    // Username
    if (!config_.username.empty()) {
        payload.push_back((config_.username.length() >> 8) & 0xFF);
        payload.push_back(config_.username.length() & 0xFF);
        payload.insert(payload.end(), config_.username.begin(), config_.username.end());
    }

    // Password
    if (!config_.password.empty()) {
        payload.push_back((config_.password.length() >> 8) & 0xFF);
        payload.push_back(config_.password.length() & 0xFF);
        payload.insert(payload.end(), config_.password.begin(), config_.password.end());
    }

    size_t rem_len = var_header.size() + payload.size();
    std::vector<uint8_t> var_len_bytes = encode_remaining_length(rem_len);

    std::vector<uint8_t> packet;
    packet.push_back(0x10); // CONNECT Control Packet
    packet.insert(packet.end(), var_len_bytes.begin(), var_len_bytes.end());
    packet.insert(packet.end(), var_header.begin(), var_header.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    if (!send_raw(packet.data(), packet.size())) return false;

    // Read CONNACK
    uint8_t ack_buf[4];
    int r = read_raw(ack_buf, 4, 3000);
    if (r == 4 && ack_buf[0] == 0x20 && ack_buf[1] == 0x02 && ack_buf[3] == 0x00) {
        return true;
    }
    return false;
}

bool MqttClient::send_mqtt_ping() {
    uint8_t ping_pkt[2] = {0xC0, 0x00};
    return send_raw(ping_pkt, 2);
}

bool MqttClient::send_mqtt_subscribe(const std::string& topic_filter, int qos) {
    uint16_t pid = packet_id_++;
    std::vector<uint8_t> var_header;
    var_header.push_back((pid >> 8) & 0xFF);
    var_header.push_back(pid & 0xFF);

    std::vector<uint8_t> payload;
    payload.push_back((topic_filter.length() >> 8) & 0xFF);
    payload.push_back(topic_filter.length() & 0xFF);
    payload.insert(payload.end(), topic_filter.begin(), topic_filter.end());
    payload.push_back((uint8_t)qos);

    size_t rem_len = var_header.size() + payload.size();
    std::vector<uint8_t> var_len_bytes = encode_remaining_length(rem_len);

    std::vector<uint8_t> packet;
    packet.push_back(0x82); // SUBSCRIBE (QoS 1)
    packet.insert(packet.end(), var_len_bytes.begin(), var_len_bytes.end());
    packet.insert(packet.end(), var_header.begin(), var_header.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return send_raw(packet.data(), packet.size());
}

bool MqttClient::publish(const std::string& topic, const std::string& payload, bool retain, int qos) {
    if (!connected_) return false;

    uint8_t header = 0x30; // PUBLISH
    if (retain) header |= 0x01;
    if (qos == 1) header |= 0x02;

    std::vector<uint8_t> var_header;
    var_header.push_back((topic.length() >> 8) & 0xFF);
    var_header.push_back(topic.length() & 0xFF);
    var_header.insert(var_header.end(), topic.begin(), topic.end());

    if (qos > 0) {
        uint16_t pid = packet_id_++;
        var_header.push_back((pid >> 8) & 0xFF);
        var_header.push_back(pid & 0xFF);
    }

    size_t rem_len = var_header.size() + payload.length();
    std::vector<uint8_t> var_len_bytes = encode_remaining_length(rem_len);

    std::vector<uint8_t> packet;
    packet.push_back(header);
    packet.insert(packet.end(), var_len_bytes.begin(), var_len_bytes.end());
    packet.insert(packet.end(), var_header.begin(), var_header.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return send_raw(packet.data(), packet.size());
}

bool MqttClient::publish_state(const std::string& subtopic, const std::string& payload, bool retain) {
    std::string full_topic = config_.base_topic + "/state/" + subtopic;
    return publish(full_topic, payload, retain);
}

void MqttClient::publish_ha_discovery() {
    if (!config_.discovery) return;

    std::cout << "[MQTT 📢] Publishing Home Assistant Auto-Discovery configuration..." << std::endl;

    // 1. Screen Light Component
    {
        std::string disc_topic = "homeassistant/light/tpp01_screen/config";
        std::stringstream ss;
        ss << "{"
           << "\"name\":\"Screen\","
           << "\"unique_id\":\"tpp01_screen_light\","
           << "\"cmd_t\":\"" << config_.base_topic << "/cmd/screen\","
           << "\"stat_t\":\"" << config_.base_topic << "/state/screen\","
           << "\"bri_cmd_t\":\"" << config_.base_topic << "/cmd/brightness\","
           << "\"bri_stat_t\":\"" << config_.base_topic << "/state/brightness\","
           << "\"bri_scl\":255,"
           << "\"payload_on\":\"ON\","
           << "\"payload_off\":\"OFF\","
           << "\"availability_topic\":\"" << config_.base_topic << "/state/status\","
           << "\"device\":{"
           << "\"identifiers\":[\"tpp01_panel\"],"
           << "\"name\":\"TPP01 Touch Panel\","
           << "\"model\":\"MOES TPP01-Z\","
           << "\"manufacturer\":\"Tuya / MOES\""
           << "}"
           << "}";
        publish(disc_topic, ss.str(), true);
    }

    // 2. Wi-Fi RSSI Sensor
    {
        std::string disc_topic = "homeassistant/sensor/tpp01_wifi_rssi/config";
        std::stringstream ss;
        ss << "{"
           << "\"name\":\"Wi-Fi Signal\","
           << "\"unique_id\":\"tpp01_wifi_rssi\","
           << "\"stat_t\":\"" << config_.base_topic << "/state/wifi_rssi\","
           << "\"unit_of_meas\":\"dBm\","
           << "\"device_class\":\"signal_strength\","
           << "\"availability_topic\":\"" << config_.base_topic << "/state/status\","
           << "\"device\":{\"identifiers\":[\"tpp01_panel\"]}"
           << "}";
        publish(disc_topic, ss.str(), true);
    }

    // 3. IP Address Sensor
    {
        std::string disc_topic = "homeassistant/sensor/tpp01_ip/config";
        std::stringstream ss;
        ss << "{"
           << "\"name\":\"IP Address\","
           << "\"unique_id\":\"tpp01_ip\","
           << "\"stat_t\":\"" << config_.base_topic << "/state/ip\","
           << "\"icon\":\"mdi:ip-network\","
           << "\"availability_topic\":\"" << config_.base_topic << "/state/status\","
           << "\"device\":{\"identifiers\":[\"tpp01_panel\"]}"
           << "}";
        publish(disc_topic, ss.str(), true);
    }

    // 4. Reboot Button
    {
        std::string disc_topic = "homeassistant/button/tpp01_reboot/config";
        std::stringstream ss;
        ss << "{"
           << "\"name\":\"Reboot Panel\","
           << "\"unique_id\":\"tpp01_reboot\","
           << "\"cmd_t\":\"" << config_.base_topic << "/cmd/reboot\","
           << "\"payload_press\":\"PRESS\","
           << "\"device_class\":\"restart\","
           << "\"availability_topic\":\"" << config_.base_topic << "/state/status\","
           << "\"device\":{\"identifiers\":[\"tpp01_panel\"]}"
           << "}";
        publish(disc_topic, ss.str(), true);
    }
}

void MqttClient::process_incoming_packet(uint8_t header, const std::vector<uint8_t>& payload) {
    uint8_t pkt_type = header & 0xF0;
    if (pkt_type == 0x30) { // PUBLISH
        if (payload.size() < 2) return;
        size_t topic_len = (payload[0] << 8) | payload[1];
        if (payload.size() < 2 + topic_len) return;

        std::string topic((const char*)payload.data() + 2, topic_len);
        size_t offset = 2 + topic_len;

        uint8_t qos = (header >> 1) & 0x03;
        if (qos > 0) {
            offset += 2; // skip Packet ID
        }

        std::string msg_payload;
        if (payload.size() > offset) {
            msg_payload = std::string((const char*)payload.data() + offset, payload.size() - offset);
        }

        std::cout << "[MQTT 📩] Topic: " << topic << " | Payload: " << msg_payload << std::endl;

        std::string cmd_prefix = config_.base_topic + "/cmd/";
        if (topic.rfind(cmd_prefix, 0) == 0 && command_cb_) {
            std::string cmd_name = topic.substr(cmd_prefix.length());
            command_cb_(cmd_name, msg_payload);
        }
    }
}

void MqttClient::loop() {
    std::cout << "[MQTT ⚡] Loop thread started for " << config_.host << ":" << config_.port << std::endl;

    while (running_) {
        if (!connect_socket()) {
            std::cout << "[MQTT ❌] Socket connection failed. Retrying in 3s..." << std::endl;
            for (int i = 0; i < 30 && running_; i++) usleep(100000);
            continue;
        }

        if (!send_mqtt_connect()) {
            std::cout << "[MQTT ❌] MQTT CONNECT handshake failed. Retrying in 3s..." << std::endl;
            close_socket();
            for (int i = 0; i < 30 && running_; i++) usleep(100000);
            continue;
        }

        connected_ = true;
        std::cout << "[MQTT 🟢] Connected successfully to broker!" << std::endl;

        // Publish LWT status = online
        publish_state("status", "online", true);

        // Auto-Discovery
        publish_ha_discovery();

        // Subscribe to command topics
        std::string cmd_topic = config_.base_topic + "/cmd/#";
        send_mqtt_subscribe(cmd_topic, 1);

        uint32_t last_ping = time(NULL);

        while (running_ && connected_) {
            uint8_t header;
            int r = read_raw(&header, 1, 500);
            if (r < 0) { // Error or closed
                std::cout << "[MQTT ❌] Read socket error. Disconnecting..." << std::endl;
                break;
            } else if (r == 1) {
                bool ok = false;
                auto read_one = [this](uint8_t* b) -> int {
                    return this->read_raw(b, 1, 1000);
                };
                size_t rem_len = decode_remaining_length(read_one, ok);
                if (!ok) break;

                std::vector<uint8_t> pkt_payload(rem_len);
                size_t read_accum = 0;
                while (read_accum < rem_len && running_) {
                    int pr = read_raw(pkt_payload.data() + read_accum, rem_len - read_accum, 1000);
                    if (pr <= 0) break;
                    read_accum += pr;
                }

                if (read_accum == rem_len) {
                    process_incoming_packet(header, pkt_payload);
                }
            }

            // Ping timer (every 20s)
            uint32_t now = time(NULL);
            if (now - last_ping >= 20) {
                if (!send_mqtt_ping()) {
                    std::cout << "[MQTT ❌] Ping failed." << std::endl;
                    break;
                }
                last_ping = now;
            }
        }

        close_socket();
        if (running_) {
            std::cout << "[MQTT 🔄] Reconnecting in 3s..." << std::endl;
            for (int i = 0; i < 30 && running_; i++) usleep(100000);
        }
    }

    std::cout << "[MQTT ⏹️] Loop thread terminated." << std::endl;
}

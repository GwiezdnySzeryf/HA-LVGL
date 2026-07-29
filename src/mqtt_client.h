#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>

struct MqttConfig {
    bool enabled = false;
    std::string host;
    int port = 1883;
    std::string username;
    std::string password;
    std::string client_id = "tpp01_panel";
    std::string base_topic = "panel/tpp01";
    bool discovery = true;
};

class MqttClient {
public:
    using CommandCallback = std::function<void(const std::string& cmd, const std::string& payload)>;

    MqttClient();
    ~MqttClient();

    void configure(const MqttConfig& config);
    void start();
    void stop();

    bool is_connected() const { return connected_; }

    bool publish(const std::string& topic, const std::string& payload, bool retain = false, int qos = 0);
    bool publish_state(const std::string& subtopic, const std::string& payload, bool retain = true);

    void set_command_callback(CommandCallback cb) { command_cb_ = cb; }

    void publish_ha_discovery();

private:
    MqttConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread loop_thread_;
    int sock_fd_{-1};
    uint16_t packet_id_{1};
    std::mutex sock_mutex_;
    CommandCallback command_cb_;

    void loop();
    bool connect_socket();
    void close_socket();
    bool send_mqtt_connect();
    bool send_mqtt_ping();
    bool send_mqtt_subscribe(const std::string& topic_filter, int qos = 0);
    bool send_raw(const uint8_t* data, size_t len);
    int read_raw(uint8_t* buf, size_t len, int timeout_ms);
    void process_incoming_packet(uint8_t header, const std::vector<uint8_t>& payload);
};

#endif // MQTT_CLIENT_H

#ifndef WAKE_WORD_LISTENER_H
#define WAKE_WORD_LISTENER_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <sys/types.h>

class WakeWordListener {
public:
    using WakeHandler = std::function<void()>;

    WakeWordListener();
    ~WakeWordListener();

    bool start(const std::string& worker_path, const std::string& model_path,
               WakeHandler handler);
    void stop();

    bool is_running() const { return running_; }
    std::string last_error() const;

private:
    void monitor_loop(int output_fd, pid_t capture_pid, pid_t worker_pid);
    void set_error(const std::string& error);

    std::atomic<bool> running_;
    std::thread monitor_thread_;
    WakeHandler wake_handler_;
    int output_fd_;
    pid_t capture_pid_;
    pid_t worker_pid_;
    mutable std::mutex state_mutex_;
    std::string last_error_;
};

#endif

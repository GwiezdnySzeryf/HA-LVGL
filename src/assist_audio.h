#ifndef ASSIST_AUDIO_H
#define ASSIST_AUDIO_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <sys/types.h>

class AssistAudioCapture {
public:
    using FrameHandler = std::function<void(const int16_t * samples, size_t count)>;

    AssistAudioCapture();
    ~AssistAudioCapture();

    bool start(FrameHandler handler);
    void stop();

    bool is_running() const { return running_; }
    int level_percent() const { return level_percent_; }
    uint64_t frame_count() const { return frame_count_; }
    std::string last_error() const;

private:
    void capture_loop(int read_fd, pid_t child_pid);
    void set_error(const std::string& error);

    std::atomic<bool> running_;
    std::atomic<int> level_percent_;
    std::atomic<uint64_t> frame_count_;
    std::thread capture_thread_;
    FrameHandler frame_handler_;
    int read_fd_;
    pid_t child_pid_;
    mutable std::mutex state_mutex_;
    std::string last_error_;
};

#endif

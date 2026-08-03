#include "assist_audio.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const size_t kMonoSamplesPerFrame = 320;
const size_t kStereoSamplesPerFrame = kMonoSamplesPerFrame * 2;
}

AssistAudioCapture::AssistAudioCapture()
    : running_(false), level_percent_(0), frame_count_(0), read_fd_(-1), child_pid_(-1) {}

AssistAudioCapture::~AssistAudioCapture() {
    stop();
}

bool AssistAudioCapture::start(FrameHandler handler) {
    stop();
    set_error("");

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        set_error(std::string("Nie można utworzyć potoku audio: ") + strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        set_error(std::string("Nie można uruchomić arecord: ") + strerror(errno));
        return false;
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) _exit(126);
        close(pipe_fds[1]);
        execlp("arecord", "arecord", "-q", "-D", "hw:0,0", "-t", "raw", "-f",
               "S16_LE", "-r", "16000", "-c", "2", static_cast<char *>(NULL));
        _exit(127);
    }

    close(pipe_fds[1]);
    const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        read_fd_ = pipe_fds[0];
        child_pid_ = pid;
        frame_handler_ = handler;
    }
    level_percent_ = 0;
    frame_count_ = 0;
    running_ = true;
    capture_thread_ = std::thread(&AssistAudioCapture::capture_loop, this, pipe_fds[0], pid);
    return true;
}

void AssistAudioCapture::stop() {
    running_ = false;

    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        pid = child_pid_;
    }
    if (pid > 0) kill(pid, SIGTERM);
    if (capture_thread_.joinable()) capture_thread_.join();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        read_fd_ = -1;
        child_pid_ = -1;
        frame_handler_ = FrameHandler();
    }
    level_percent_ = 0;
}

std::string AssistAudioCapture::last_error() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

void AssistAudioCapture::set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_ = error;
}

void AssistAudioCapture::capture_loop(int read_fd, pid_t child_pid) {
    int16_t stereo[kStereoSamplesPerFrame];
    int16_t mono[kMonoSamplesPerFrame];
    size_t bytes_used = 0;
    bool received_audio = false;

    while (running_) {
        struct pollfd descriptor;
        descriptor.fd = read_fd;
        descriptor.events = POLLIN | POLLHUP;
        descriptor.revents = 0;
        const int poll_result = poll(&descriptor, 1, 100);
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            if (running_) set_error(std::string("Błąd oczekiwania na mikrofon: ") + strerror(errno));
            break;
        }
        if (poll_result == 0) continue;

        char * target = reinterpret_cast<char *>(stereo) + bytes_used;
        const size_t bytes_left = sizeof(stereo) - bytes_used;
        ssize_t count = read(read_fd, target, bytes_left);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (running_) set_error(std::string("Błąd odczytu mikrofonu: ") + strerror(errno));
            break;
        }
        if (count == 0) {
            if (running_ && !received_audio) set_error("arecord nie zwrócił danych audio");
            break;
        }

        received_audio = true;
        bytes_used += static_cast<size_t>(count);
        if (bytes_used != sizeof(stereo)) continue;

        int peak = 0;
        for (size_t i = 0; i < kMonoSamplesPerFrame; ++i) {
            const int sample = stereo[i * 2];
            mono[i] = static_cast<int16_t>(sample);
            const int magnitude = sample < 0 ? -sample : sample;
            if (magnitude > peak) peak = magnitude;
        }
        level_percent_ = peak * 100 / 32768;
        ++frame_count_;
        if (frame_handler_) frame_handler_(mono, kMonoSamplesPerFrame);
        bytes_used = 0;
    }

    close(read_fd);
    int status = 0;
    bool child_exited = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const pid_t result = waitpid(child_pid, &status, WNOHANG);
        if (result == child_pid || (result < 0 && errno == ECHILD)) {
            child_exited = true;
            break;
        }
        if (attempt == 0) kill(child_pid, SIGTERM);
        usleep(10000);
    }
    if (!child_exited) {
        kill(child_pid, SIGKILL);
        std::thread([child_pid]() {
            int child_status = 0;
            while (waitpid(child_pid, &child_status, 0) < 0 && errno == EINTR) {}
        }).detach();
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (read_fd_ == read_fd) read_fd_ = -1;
        if (child_pid_ == child_pid) child_pid_ = -1;
    }
    running_ = false;
}

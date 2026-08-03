#include "wake_word_listener.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

WakeWordListener::WakeWordListener()
    : running_(false), output_fd_(-1), capture_pid_(-1), worker_pid_(-1) {}

WakeWordListener::~WakeWordListener() {
    stop();
}

bool WakeWordListener::start(const std::string& worker_path, const std::string& model_path,
                             WakeHandler handler) {
    stop();
    set_error("");

    int audio_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (pipe(audio_pipe) != 0 || pipe(output_pipe) != 0) {
        if (audio_pipe[0] >= 0) {
            close(audio_pipe[0]);
            close(audio_pipe[1]);
        }
        set_error(std::string("Nie można utworzyć potoku wake word: ") + strerror(errno));
        return false;
    }

    const pid_t capture_pid = fork();
    if (capture_pid < 0) {
        close(audio_pipe[0]);
        close(audio_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        set_error(std::string("Nie można uruchomić mikrofonu wake word: ") + strerror(errno));
        return false;
    }
    if (capture_pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(125);
        close(audio_pipe[0]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        if (dup2(audio_pipe[1], STDOUT_FILENO) < 0) _exit(126);
        close(audio_pipe[1]);
        const int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        execlp("arecord", "arecord", "-q", "-D", "hw:0,0", "-t", "raw", "-f",
               "S16_LE", "-r", "16000", "-c", "2", static_cast<char *>(NULL));
        _exit(127);
    }

    const pid_t worker_pid = fork();
    if (worker_pid < 0) {
        kill(capture_pid, SIGTERM);
        close(audio_pipe[0]);
        close(audio_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        while (waitpid(capture_pid, NULL, 0) < 0 && errno == EINTR) {}
        set_error(std::string("Nie można uruchomić workera wake word: ") + strerror(errno));
        return false;
    }
    if (worker_pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(125);
        close(audio_pipe[1]);
        close(output_pipe[0]);
        if (dup2(audio_pipe[0], STDIN_FILENO) < 0 ||
            dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe[1], STDERR_FILENO) < 0) _exit(126);
        close(audio_pipe[0]);
        close(output_pipe[1]);
        execl(worker_path.c_str(), worker_path.c_str(), model_path.c_str(), "-", "2",
              static_cast<char *>(NULL));
        _exit(127);
    }

    close(audio_pipe[0]);
    close(audio_pipe[1]);
    close(output_pipe[1]);
    const int flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (flags >= 0) fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        output_fd_ = output_pipe[0];
        capture_pid_ = capture_pid;
        worker_pid_ = worker_pid;
        wake_handler_ = handler;
    }
    running_ = true;
    monitor_thread_ = std::thread(&WakeWordListener::monitor_loop, this, output_pipe[0],
                                  capture_pid, worker_pid);
    return true;
}

void WakeWordListener::stop() {
    running_ = false;
    pid_t capture_pid = -1;
    pid_t worker_pid = -1;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        capture_pid = capture_pid_;
        worker_pid = worker_pid_;
    }
    if (capture_pid > 0) kill(capture_pid, SIGTERM);
    if (worker_pid > 0) kill(worker_pid, SIGTERM);
    if (monitor_thread_.joinable()) monitor_thread_.join();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        output_fd_ = -1;
        capture_pid_ = -1;
        worker_pid_ = -1;
        wake_handler_ = WakeHandler();
    }
}

std::string WakeWordListener::last_error() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

void WakeWordListener::set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_ = error;
}

void WakeWordListener::monitor_loop(int output_fd, pid_t capture_pid, pid_t worker_pid) {
    std::string output;
    char buffer[512];
    while (running_) {
        struct pollfd descriptor;
        descriptor.fd = output_fd;
        descriptor.events = POLLIN | POLLHUP;
        descriptor.revents = 0;
        const int poll_result = poll(&descriptor, 1, 100);
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            set_error(std::string("Błąd workera wake word: ") + strerror(errno));
            break;
        }
        if (poll_result == 0) continue;
        const ssize_t count = read(output_fd, buffer, sizeof(buffer));
        if (count <= 0) {
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            break;
        }
        output.append(buffer, static_cast<size_t>(count));
        size_t newline = std::string::npos;
        while ((newline = output.find('\n')) != std::string::npos) {
            const std::string line = output.substr(0, newline);
            output.erase(0, newline + 1);
            if (line.compare(0, 5, "WAKE ") == 0) {
                WakeHandler handler;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    handler = wake_handler_;
                }
                if (handler) handler();
            }
        }
    }

    running_ = false;
    kill(capture_pid, SIGTERM);
    kill(worker_pid, SIGTERM);
    close(output_fd);
    while (waitpid(capture_pid, NULL, 0) < 0 && errno == EINTR) {}
    while (waitpid(worker_pid, NULL, 0) < 0 && errno == EINTR) {}
}

#include "assist_pipeline_bridge.h"

#include <algorithm>
#include <cmath>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

namespace {
const int kChildTimeoutSeconds = 240;
volatile sig_atomic_t g_active_process_group = -1;

void child_timeout_handler(int) {
    if (g_active_process_group > 0) kill(-g_active_process_group, SIGKILL);
    _exit(124);
}

struct ParsedUrl {
    std::string host;
    std::string base_path;
    int port;
};

bool parse_ha_url(const std::string& input, ParsedUrl * parsed) {
    std::string url = input;
    while (!url.empty() && static_cast<uint8_t>(url.back()) <= 32) url.pop_back();
    while (!url.empty() && static_cast<uint8_t>(url.front()) <= 32) url.erase(0, 1);
    if (url.compare(0, 8, "https://") != 0) return false;
    url.erase(0, 8);

    const size_t slash = url.find('/');
    const std::string authority = slash == std::string::npos ? url : url.substr(0, slash);
    parsed->base_path = slash == std::string::npos ? "" : url.substr(slash);
    while (!parsed->base_path.empty() && parsed->base_path.back() == '/') parsed->base_path.pop_back();
    const size_t colon = authority.rfind(':');
    parsed->host = colon == std::string::npos ? authority : authority.substr(0, colon);
    parsed->port = colon == std::string::npos ? 443 : atoi(authority.substr(colon + 1).c_str());
    return !parsed->host.empty() && parsed->port > 0 && parsed->port <= 65535;
}

bool ssl_write_all(mbedtls_ssl_context * ssl, const uint8_t * data, size_t size) {
    size_t written = 0;
    const time_t deadline = time(NULL) + 5;
    while (written < size) {
        const int result = mbedtls_ssl_write(ssl, data + written, size - written);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (time(NULL) >= deadline) return false;
            usleep(1000);
            continue;
        }
        if (result <= 0) return false;
        written += static_cast<size_t>(result);
    }
    return true;
}

bool ssl_read_all(mbedtls_ssl_context * ssl, uint8_t * data, size_t size) {
    size_t received = 0;
    const time_t deadline = time(NULL) + 5;
    while (received < size) {
        const int result = mbedtls_ssl_read(ssl, data + received, size - received);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (time(NULL) >= deadline) return false;
            usleep(1000);
            continue;
        }
        if (result <= 0) return false;
        received += static_cast<size_t>(result);
    }
    return true;
}

bool send_ws_frame(mbedtls_ssl_context * ssl, uint8_t opcode, const uint8_t * payload,
                   size_t size, mbedtls_ctr_drbg_context * random) {
    std::vector<uint8_t> frame;
    frame.reserve(size + 14);
    frame.push_back(static_cast<uint8_t>(0x80 | opcode));
    if (size <= 125) {
        frame.push_back(static_cast<uint8_t>(0x80 | size));
    } else if (size <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(size & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(size) >> shift) & 0xFF));
        }
    }

    uint8_t mask[4];
    if (mbedtls_ctr_drbg_random(random, mask, sizeof(mask)) != 0) return false;
    frame.insert(frame.end(), mask, mask + sizeof(mask));
    for (size_t i = 0; i < size; ++i) frame.push_back(payload[i] ^ mask[i % 4]);
    return ssl_write_all(ssl, frame.data(), frame.size());
}

bool send_ws_text(mbedtls_ssl_context * ssl, const std::string& text,
                  mbedtls_ctr_drbg_context * random) {
    return send_ws_frame(ssl, 0x1, reinterpret_cast<const uint8_t *>(text.data()), text.size(), random);
}

bool read_ws_frame(mbedtls_ssl_context * ssl, uint8_t * opcode, std::string * payload) {
    uint8_t header[2];
    if (!ssl_read_all(ssl, header, sizeof(header))) return false;
    *opcode = header[0] & 0x0F;
    uint64_t size = header[1] & 0x7F;
    if (size == 126) {
        uint8_t extended[2];
        if (!ssl_read_all(ssl, extended, sizeof(extended))) return false;
        size = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
    } else if (size == 127) {
        uint8_t extended[8];
        if (!ssl_read_all(ssl, extended, sizeof(extended))) return false;
        size = 0;
        for (size_t i = 0; i < sizeof(extended); ++i) size = (size << 8) | extended[i];
    }
    if (size > 1024 * 1024) return false;
    payload->resize(static_cast<size_t>(size));
    return size == 0 || ssl_read_all(ssl, reinterpret_cast<uint8_t *>(&(*payload)[0]),
                                     static_cast<size_t>(size));
}

bool wait_for_socket(mbedtls_ssl_context * ssl, int fd, int milliseconds) {
    if (mbedtls_ssl_get_bytes_avail(ssl) > 0) return true;
    struct pollfd descriptor;
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    return poll(&descriptor, 1, milliseconds) > 0;
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    for (size_t i = 0; i < value.size(); ++i) {
        const char character = value[i];
        if (character == '"' || character == '\\') {
            escaped.push_back('\\');
            escaped.push_back(character);
        } else if (character == '\n') escaped += "\\n";
        else if (character == '\r') escaped += "\\r";
        else if (static_cast<unsigned char>(character) >= 0x20) escaped.push_back(character);
    }
    return escaped;
}

std::string json_string_after(const std::string& json, const std::string& key,
                              size_t start = 0) {
    const std::string marker = "\"" + key + "\"";
    size_t position = json.find(marker, start);
    if (position == std::string::npos) return "";
    position = json.find(':', position + marker.size());
    if (position == std::string::npos) return "";
    position = json.find('"', position + 1);
    if (position == std::string::npos) return "";
    std::string value;
    bool escaped = false;
    for (++position; position < json.size(); ++position) {
        const char character = json[position];
        if (escaped) {
            if (character == 'n') value.push_back('\n');
            else if (character == 'r') value.push_back('\r');
            else value.push_back(character);
            escaped = false;
        } else if (character == '\\') escaped = true;
        else if (character == '"') break;
        else value.push_back(character);
    }
    return value;
}

std::string json_string_in_object(const std::string& json, const std::string& object_key,
                                  const std::string& value_key) {
    const std::string marker = "\"" + object_key + "\"";
    const size_t marker_position = json.find(marker);
    if (marker_position == std::string::npos) return "";
    const size_t object_start = json.find('{', marker_position + marker.size());
    if (object_start == std::string::npos) return "";

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t position = object_start; position < json.size(); ++position) {
        const char character = json[position];
        if (in_string) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') in_string = false;
            continue;
        }
        if (character == '"') in_string = true;
        else if (character == '{') ++depth;
        else if (character == '}' && --depth == 0) {
            return json_string_after(json.substr(object_start, position - object_start + 1),
                                     value_key);
        }
    }
    return "";
}

int json_int_after(const std::string& json, const std::string& key, int fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t position = json.find(marker);
    if (position == std::string::npos) return fallback;
    position = json.find(':', position + marker.size());
    if (position == std::string::npos) return fallback;
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t')) ++position;
    char * end = NULL;
    const long value = strtol(json.c_str() + position, &end, 10);
    return end == json.c_str() + position ? fallback : static_cast<int>(value);
}

bool json_bool_after(const std::string& json, const std::string& key, bool fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t position = json.find(marker);
    if (position == std::string::npos) return fallback;
    position = json.find(':', position + marker.size());
    if (position == std::string::npos) return fallback;
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t')) ++position;
    if (json.compare(position, 4, "true") == 0) return true;
    if (json.compare(position, 5, "false") == 0) return false;
    return fallback;
}

std::string serialize_result(const AssistPipelineResult& result, bool final_result = true) {
    return "{\"final\":" + std::string(final_result ? "true" : "false") +
           ",\"success\":" + std::string(result.success ? "true" : "false") +
           ",\"stage\":\"" + json_escape(result.stage) +
           "\",\"transcript\":\"" + json_escape(result.transcript) +
           "\",\"response\":\"" + json_escape(result.response) +
           "\",\"error\":\"" + json_escape(result.error) +
           "\",\"conversation_id\":\"" + json_escape(result.conversation_id) +
           "\",\"response_type\":\"" + json_escape(result.response_type) +
           "\",\"continue_conversation\":" +
           std::string(result.continue_conversation ? "true" : "false") +
           ",\"audio_level\":" + std::to_string(result.audio_level) + "}\n";
}

bool write_all_fd(int fd, const uint8_t * data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const ssize_t count = write(fd, data + written, size - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        written += static_cast<size_t>(count);
    }
    return true;
}

bool write_fd_with_deadline(int fd, const uint8_t * data, size_t size, time_t deadline) {
    size_t written = 0;
    while (written < size && time(NULL) < deadline) {
        struct pollfd descriptor;
        descriptor.fd = fd;
        descriptor.events = POLLOUT;
        descriptor.revents = 0;
        if (poll(&descriptor, 1, 100) <= 0) continue;
        const ssize_t count = write(fd, data + written, size - written);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return false;
        written += static_cast<size_t>(count);
    }
    return written == size;
}

bool run_process(const std::vector<std::string>& arguments, int timeout_seconds) {
    if (arguments.empty()) return false;
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setpgid(0, 0);
        const int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        std::vector<char *> argv;
        for (size_t i = 0; i < arguments.size(); ++i) {
            argv.push_back(const_cast<char *>(arguments[i].c_str()));
        }
        argv.push_back(NULL);
        execv(argv[0], argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    g_active_process_group = pid;

    const time_t deadline = time(NULL) + timeout_seconds;
    int status = 0;
    while (time(NULL) < deadline) {
        const pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            g_active_process_group = -1;
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (wait_result < 0 && errno != EINTR) {
            g_active_process_group = -1;
            return false;
        }
        usleep(100000);
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    g_active_process_group = -1;
    return false;
}

bool play_pcm_with_levels(const std::string& filename,
                          const std::function<void(int)>& on_level) {
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setpgid(0, 0);
        const int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        execl("/bin/aplay", "aplay", "-q", "-D", "hw:0,0", "-t", "raw", "-f",
              "S16_LE", "-r", "48000", "-c", "2", filename.c_str(),
              static_cast<char *>(NULL));
        _exit(127);
    }
    setpgid(pid, pid);
    g_active_process_group = pid;

    FILE * pcm = fopen(filename.c_str(), "rb");
    std::vector<int16_t> samples(4800 * 2);
    const time_t deadline = time(NULL) + 90;
    int status = 0;
    while (time(NULL) < deadline) {
        const pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            g_active_process_group = -1;
            if (pcm) fclose(pcm);
            if (on_level) on_level(0);
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (wait_result < 0 && errno != EINTR) break;

        int level = 0;
        if (pcm) {
            const size_t count = fread(samples.data(), sizeof(int16_t), samples.size(), pcm);
            if (count > 0) {
                double sum = 0.0;
                for (size_t i = 0; i < count; ++i) {
                    const double sample = samples[i];
                    sum += sample * sample;
                }
                const double rms = std::sqrt(sum / count);
                if (rms > 1.0) {
                    const double db = 20.0 * std::log10(rms / 32768.0);
                    level = std::max(0, std::min(100, static_cast<int>((db + 50.0) * 2.0)));
                }
            }
        }
        if (on_level) on_level(level);
        usleep(100000);
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    g_active_process_group = -1;
    if (pcm) fclose(pcm);
    if (on_level) on_level(0);
    return false;
}

bool stream_and_play_tts(const ParsedUrl& parsed, const std::string& path,
                         const std::function<void()>& on_playback) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", parsed.port);
    const std::string url = "https://" + parsed.host + ":" + port_str + parsed.base_path + path;

    const std::vector<std::string> stream_cmd = {
        "/bin/sh", "-c",
        "/tuya/data/curl -fskL --max-time 30 \"" + url + "\" | "
        "/usr/bin/gst-launch-1.0 -q fdsrc fd=0 ! decodebin ! "
        "audioconvert ! audioresample ! "
        "audio/x-raw,format=S16LE,rate=48000,channels=2 ! "
        "alsasink device=hw:0,0 sync=false"
    };

    if (on_playback) on_playback();
    return run_process(stream_cmd, 60);
}

bool download_and_play_tts(const ParsedUrl& parsed, const std::string& path,
                           const std::string& mime_type, std::string * error,
                           const std::function<void()>& on_playback,
                           const std::function<void(int)>& on_level) {
    (void)on_level;
    if (path.compare(0, 15, "/api/tts_proxy/") != 0 ||
        mime_type.compare(0, 6, "audio/") != 0) {
        *error = "Home Assistant zwrócił nieprawidłowe audio TTS";
        return false;
    }

    if (stream_and_play_tts(parsed, path, on_playback)) {
        return true;
    }

    char port[16];
    snprintf(port, sizeof(port), "%d", parsed.port);
    const std::string url = "https://" + parsed.host + ":" + port + parsed.base_path + path;
    char filename[96];
    snprintf(filename, sizeof(filename), "/tmp/ha_assist_tts_%ld", static_cast<long>(getpid()));
    unlink(filename);

    const std::vector<std::string> download = {
        "/tuya/data/curl", "-fskL", "--max-time", "15", "--max-filesize", "8388608",
        "-o", filename, url
    };
    if (!run_process(download, 20)) {
        unlink(filename);
        *error = "Nie można pobrać odpowiedzi głosowej";
        return false;
    }

    const std::vector<std::string> fallback_playback = {
        "/usr/bin/gst-launch-1.0", "-q",
        "filesrc", "location=" + std::string(filename), "!",
        "decodebin", "!", "audioconvert", "!", "audioresample", "!",
        "audio/x-raw,format=S16LE,rate=48000,channels=2", "!",
        "alsasink", "device=hw:0,0", "sync=false"
    };
    if (on_playback) on_playback();
    bool played = run_process(fallback_playback, 90);
    unlink(filename);
    if (!played) *error = "Nie można odtworzyć odpowiedzi głosowej";
    return played;
}
}

int assist_pipeline_child_main(const std::string& ha_url, const std::string& token,
                               const std::string& conversation_id) {
    signal(SIGALRM, child_timeout_handler);
    signal(SIGTERM, child_timeout_handler);
    alarm(kChildTimeoutSeconds);
    AssistPipelineResult result;
    std::vector<uint8_t> audio;
    uint8_t input[4096];
    while (true) {
        const ssize_t count = read(STDIN_FILENO, input, sizeof(input));
        if (count > 0) audio.insert(audio.end(), input, input + count);
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    if (audio.empty()) {
        result.error = "Brak danych audio";
        const std::string output = serialize_result(result);
        write_all_fd(STDOUT_FILENO, reinterpret_cast<const uint8_t *>(output.data()), output.size());
        return 2;
    }

    ParsedUrl parsed;
    if (!parse_ha_url(ha_url, &parsed)) {
        result.error = "Assist pipeline wymaga adresu HTTPS";
        const std::string output = serialize_result(result);
        write_all_fd(STDOUT_FILENO, reinterpret_cast<const uint8_t *>(output.data()), output.size());
        return 2;
    }

    mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    int handler_id = -1;
    std::string tts_url;
    std::string tts_mime_type;
    mbedtls_net_init(&server_fd);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);

    const unsigned char personal[] = "tpp01_pipeline";
    if (mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                              personal, sizeof(personal) - 1) != 0) {
        result.error = "Błąd generatora TLS";
        goto cleanup;
    }
    {
        char port[16];
        snprintf(port, sizeof(port), "%d", parsed.port);
        if (mbedtls_net_connect(&server_fd, parsed.host.c_str(), port, MBEDTLS_NET_PROTO_TCP) != 0) {
            result.error = "Nie można połączyć z Home Assistant";
            goto cleanup;
        }
        struct timeval timeout = {5, 0};
        setsockopt(server_fd.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(server_fd.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        result.error = "Błąd konfiguracji TLS";
        goto cleanup;
    }
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    if (mbedtls_ssl_setup(&ssl, &config) != 0) {
        result.error = "Błąd inicjalizacji TLS";
        goto cleanup;
    }
    mbedtls_ssl_set_hostname(&ssl, parsed.host.c_str());
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    while (true) {
        const int status = mbedtls_ssl_handshake(&ssl);
        if (status == 0) break;
        if (status != MBEDTLS_ERR_SSL_WANT_READ && status != MBEDTLS_ERR_SSL_WANT_WRITE) {
            result.error = "Handshake TLS nie powiódł się";
            goto cleanup;
        }
    }
    {
        char port[16];
        snprintf(port, sizeof(port), "%d", parsed.port);
        const std::string request = "GET " + parsed.base_path + "/api/websocket HTTP/1.1\r\n"
                                    "Host: " + parsed.host + ":" + port + "\r\n"
                                    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                    "Sec-WebSocket-Version: 13\r\n\r\n";
        if (!ssl_write_all(&ssl, reinterpret_cast<const uint8_t *>(request.data()), request.size())) {
            result.error = "Nie można rozpocząć WebSocket";
            goto cleanup;
        }
        std::string headers;
        uint8_t character = 0;
        while (headers.size() < 4096 && ssl_read_all(&ssl, &character, 1)) {
            headers.push_back(static_cast<char>(character));
            if (headers.size() >= 4 && headers.compare(headers.size() - 4, 4, "\r\n\r\n") == 0) break;
        }
        if (headers.find(" 101 ") == std::string::npos ||
            headers.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == std::string::npos) {
            result.error = "Home Assistant odrzucił WebSocket";
            goto cleanup;
        }
    }
    {
        uint8_t opcode = 0;
        std::string message;
        if (!read_ws_frame(&ssl, &opcode, &message) || message.find("auth_required") == std::string::npos) {
            result.error = "Brak żądania autoryzacji HA";
            goto cleanup;
        }
        const std::string auth = "{\"type\":\"auth\",\"access_token\":\"" + token + "\"}";
        if (!send_ws_text(&ssl, auth, &random) || !read_ws_frame(&ssl, &opcode, &message) ||
            message.find("auth_ok") == std::string::npos) {
            result.error = "Home Assistant odrzucił autoryzację";
            goto cleanup;
        }
    }
    {
        const std::string conversation = conversation_id.empty() ? "" :
            ",\"conversation_id\":\"" + json_escape(conversation_id) + "\"";
        const std::string command =
            "{\"id\":1,\"type\":\"assist_pipeline/run\",\"start_stage\":\"stt\","
            "\"end_stage\":\"tts\",\"input\":{\"sample_rate\":16000,\"no_vad\":true},"
            "\"timeout\":60" + conversation + "}";
        uint8_t opcode = 0;
        std::string message;
        if (!send_ws_text(&ssl, command, &random) || !read_ws_frame(&ssl, &opcode, &message) ||
            message.find("\"success\":true") == std::string::npos) {
            result.error = json_string_after(message, "message");
            if (result.error.empty()) result.error = "Pipeline Assist nie wystartował";
            goto cleanup;
        }
    }

    {
        const time_t deadline = time(NULL) + 5;
        while (time(NULL) < deadline) {
            if (!wait_for_socket(&ssl, server_fd.fd, 100)) continue;
            uint8_t opcode = 0;
            std::string message;
            if (!read_ws_frame(&ssl, &opcode, &message)) break;
            if (opcode == 0x9) {
                send_ws_frame(&ssl, 0xA, reinterpret_cast<const uint8_t *>(message.data()), message.size(), &random);
                continue;
            }
            if (message.find("\"type\":\"run-start\"") != std::string::npos) {
                handler_id = json_int_after(message, "stt_binary_handler_id", -1);
                result.conversation_id = json_string_after(message, "conversation_id");
                break;
            }
        }
        if (handler_id < 1 || handler_id > 255) {
            result.error = "Pipeline nie zwrócił handlera audio";
            goto cleanup;
        }
    }

    for (size_t offset = 0; offset < audio.size(); offset += 1024) {
        const size_t chunk = std::min<size_t>(1024, audio.size() - offset);
        std::vector<uint8_t> payload(chunk + 1);
        payload[0] = static_cast<uint8_t>(handler_id);
        memcpy(payload.data() + 1, audio.data() + offset, chunk);
        if (!send_ws_frame(&ssl, 0x2, payload.data(), payload.size(), &random)) {
            result.error = "Błąd wysyłania audio do pipeline";
            goto cleanup;
        }
    }
    {
        const uint8_t final_handler_id = static_cast<uint8_t>(handler_id);
        if (!send_ws_frame(&ssl, 0x2, &final_handler_id, 1, &random)) {
            result.error = "Nie można zakończyć audio pipeline";
            goto cleanup;
        }
    }

    {
        const time_t deadline = time(NULL) + 60;
        while (time(NULL) < deadline) {
            if (!wait_for_socket(&ssl, server_fd.fd, 100)) continue;
            uint8_t opcode = 0;
            std::string message;
            if (!read_ws_frame(&ssl, &opcode, &message)) {
                result.error = "Połączenie pipeline zostało zamknięte";
                break;
            }
            if (opcode == 0x9) {
                send_ws_frame(&ssl, 0xA, reinterpret_cast<const uint8_t *>(message.data()), message.size(), &random);
                continue;
            }
            if (opcode == 0x8) {
                result.error = "Home Assistant zakończył połączenie";
                break;
            }
            if (message.find("\"type\":\"stt-end\"") != std::string::npos) {
                result.transcript = json_string_after(message, "text");
                result.stage = "transcript";
                const std::string progress = serialize_result(result, false);
                write_all_fd(STDOUT_FILENO, reinterpret_cast<const uint8_t *>(progress.data()),
                             progress.size());
            } else if (message.find("\"type\":\"error\"") != std::string::npos) {
                result.error = json_string_after(message, "message");
                if (result.error.empty()) result.error = "Błąd pipeline Assist";
                break;
            } else if (message.find("\"type\":\"intent-end\"") != std::string::npos) {
                result.response = json_string_in_object(message, "plain", "speech");
                result.response_type = json_string_after(message, "response_type");
                result.continue_conversation = json_bool_after(message, "continue_conversation", false);
                if (result.response.empty()) result.response = "Polecenie zostało wykonane.";
                result.success = true;
            } else if (message.find("\"type\":\"tts-start\"") != std::string::npos) {
                const std::string tts_input = json_string_after(message, "tts_input");
                if (result.response.empty() && !tts_input.empty()) result.response = tts_input;
            } else if (message.find("\"type\":\"tts-end\"") != std::string::npos) {
                tts_url = json_string_after(message, "url");
                tts_mime_type = json_string_after(message, "mime_type");
                break;
            }
        }
        if (tts_url.empty() && result.error.empty()) result.error = "Pipeline Assist przekroczył limit czasu";
    }

cleanup:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
    if (result.success && !tts_url.empty()) {
        std::string playback_error;
        if (!download_and_play_tts(parsed, tts_url, tts_mime_type, &playback_error, [&]() {
                result.stage = "playback";
                const std::string progress = serialize_result(result, false);
                write_all_fd(STDOUT_FILENO, reinterpret_cast<const uint8_t *>(progress.data()),
                             progress.size());
            }, [&](int level) {
                result.stage = "tts_level";
                result.audio_level = level;
                const std::string progress = serialize_result(result, false);
                write_all_fd(STDOUT_FILENO, reinterpret_cast<const uint8_t *>(progress.data()),
                             progress.size());
            })) {
            result.error = playback_error;
        }
    }
    result.stage = "final";
    const std::string output = serialize_result(result);
    write_all_fd(STDOUT_FILENO, reinterpret_cast<const uint8_t *>(output.data()), output.size());
    return result.success ? 0 : 1;
}

AssistPipelineResult run_assist_pipeline_process(
    const std::vector<int16_t>& samples,
    const std::string& conversation_id,
    const std::function<void(const AssistPipelineResult&)>& on_progress,
    const std::function<bool()>& is_cancelled) {
    AssistPipelineResult result;
    if (samples.empty()) {
        result.error = "Brak danych audio";
        return result;
    }

    int input_pipe[2];
    int output_pipe[2];
    if (pipe(input_pipe) != 0) {
        result.error = "Nie można uruchomić procesu pipeline";
        return result;
    }
    if (pipe(output_pipe) != 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        result.error = "Nie można uruchomić procesu pipeline";
        return result;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        result.error = "Nie można uruchomić procesu pipeline";
        return result;
    }
    if (pid == 0) {
        close(input_pipe[1]);
        close(output_pipe[0]);
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(input_pipe[0]);
        close(output_pipe[1]);
        execl("/proc/self/exe", "ha_panel", "--assist-pipeline", conversation_id.c_str(),
              static_cast<char *>(NULL));
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);
    const int input_flags = fcntl(input_pipe[1], F_GETFL, 0);
    if (input_flags < 0 || fcntl(input_pipe[1], F_SETFL, input_flags | O_NONBLOCK) < 0) {
        kill(pid, SIGKILL);
        close(input_pipe[1]);
        close(output_pipe[0]);
        std::thread([pid]() {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }).detach();
        result.error = "Nie można uruchomić procesu pipeline";
        return result;
    }
    const uint8_t * audio = reinterpret_cast<const uint8_t *>(samples.data());
    const size_t audio_size = samples.size() * sizeof(int16_t);
    const bool input_written = write_fd_with_deadline(input_pipe[1], audio, audio_size, time(NULL) + 5);
    close(input_pipe[1]);
    if (!input_written) {
        kill(pid, SIGKILL);
        close(output_pipe[0]);
        std::thread([pid]() {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }).detach();
        result.error = "Proces pipeline nie odebrał audio";
        return result;
    }

    const int flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (flags >= 0) fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
    std::string output;
    std::string final_output;
    const auto consume_output = [&]() {
        size_t newline = std::string::npos;
        while ((newline = output.find('\n')) != std::string::npos) {
            const std::string line = output.substr(0, newline);
            output.erase(0, newline + 1);
            AssistPipelineResult parsed;
            parsed.success = line.find("\"success\":true") != std::string::npos;
            parsed.stage = json_string_after(line, "stage");
            parsed.transcript = json_string_after(line, "transcript");
            parsed.response = json_string_after(line, "response");
            parsed.error = json_string_after(line, "error");
            parsed.conversation_id = json_string_after(line, "conversation_id");
            parsed.response_type = json_string_after(line, "response_type");
            parsed.continue_conversation = json_bool_after(line, "continue_conversation", false);
            parsed.audio_level = json_int_after(line, "audio_level", 0);
            if (line.find("\"final\":true") != std::string::npos) final_output = line;
            else if (on_progress) on_progress(parsed);
        }
    };
    const time_t deadline = time(NULL) + kChildTimeoutSeconds + 5;
    bool child_exited = false;
    bool cancelled = false;
    while (time(NULL) < deadline) {
        if (is_cancelled && is_cancelled()) {
            cancelled = true;
            kill(pid, SIGTERM);
            break;
        }
        struct pollfd descriptor;
        descriptor.fd = output_pipe[0];
        descriptor.events = POLLIN | POLLHUP;
        descriptor.revents = 0;
        poll(&descriptor, 1, 100);
        char buffer[2048];
        while (true) {
            const ssize_t count = read(output_pipe[0], buffer, sizeof(buffer));
            if (count > 0) {
                output.append(buffer, static_cast<size_t>(count));
                consume_output();
            }
            else break;
        }
        int status = 0;
        const pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            child_exited = true;
            break;
        }
    }
    if (cancelled) {
        const time_t stop_deadline = time(NULL) + 1;
        while (time(NULL) < stop_deadline) {
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) {
                child_exited = true;
                break;
            }
            usleep(50000);
        }
    }
    if (!child_exited) {
        kill(pid, SIGKILL);
        std::thread([pid]() {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }).detach();
    }
    char remaining[2048];
    while (true) {
        const ssize_t count = read(output_pipe[0], remaining, sizeof(remaining));
        if (count > 0) {
            output.append(remaining, static_cast<size_t>(count));
            consume_output();
        }
        else break;
    }
    consume_output();
    close(output_pipe[0]);

    if (cancelled) {
        result.error = "Anulowano Assist";
        return result;
    }

    if (final_output.empty()) {
        result.error = "Proces pipeline nie zwrócił odpowiedzi";
        return result;
    }
    result.success = final_output.find("\"success\":true") != std::string::npos;
    result.stage = json_string_after(final_output, "stage");
    result.transcript = json_string_after(final_output, "transcript");
    result.response = json_string_after(final_output, "response");
    result.error = json_string_after(final_output, "error");
    result.conversation_id = json_string_after(final_output, "conversation_id");
    result.response_type = json_string_after(final_output, "response_type");
    result.continue_conversation = json_bool_after(final_output, "continue_conversation", false);
    result.audio_level = json_int_after(final_output, "audio_level", 0);
    if (!result.success && result.error.empty()) result.error = "Nieznany błąd procesu pipeline";
    return result;
}

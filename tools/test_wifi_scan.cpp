#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

std::string exec_cmd_line(const char * cmd) {
    char buffer[256];
    std::string result = "";
    FILE * pipe = popen(cmd, "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) result.pop_back();
    return result;
}

std::string unescape_wpa_ssid(const std::string & input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 3 < input.size() && input[i+1] == 'x') {
            char hex[3] = { input[i+2], input[i+3], '\0' };
            char * end = NULL;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2) {
                out.push_back((char)val);
                i += 3;
                continue;
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

struct WifiNetworkInfo {
    std::string ssid;
    std::string bssid;
    int rssi;
    std::string flags;
};

int main() {
    std::string current_ssid = exec_cmd_line("wpa_cli -i wlan0 status 2>/dev/null | grep '^ssid=' | cut -d= -f2");
    std::cout << "Current SSID: [" << current_ssid << "]" << std::endl;

    std::cout << "Triggering wpa_cli scan..." << std::endl;
    system("wpa_cli -i wlan0 scan 2>/dev/null");
    sleep(2);

    FILE * pipe = popen("wpa_cli -i wlan0 scan_results 2>/dev/null", "r");
    if (!pipe) {
        std::cerr << "Failed to run popen" << std::endl;
        return 1;
    }

    char line[512];
    bool first = true;
    std::map<std::string, WifiNetworkInfo> unique_ssids;

    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (first) { first = false; continue; }

        char * bssid = strtok(line, "\t");
        char * freq = NULL;
        char * rssi_str = NULL;
        char * flags = NULL;
        char * raw_ssid = NULL;

        if (bssid) freq = strtok(NULL, "\t");
        if (freq) rssi_str = strtok(NULL, "\t");
        if (rssi_str) flags = strtok(NULL, "\t");
        if (flags) raw_ssid = strtok(NULL, "\r\n");

        if (bssid && rssi_str && flags && raw_ssid) {
            int rssi = atoi(rssi_str);
            std::string ssid = unescape_wpa_ssid(raw_ssid);

            while (!ssid.empty() && (ssid.back() == '\r' || ssid.back() == '\n' || ssid.back() == ' ')) {
                ssid.pop_back();
            }

            if (ssid.empty()) continue;

            if (unique_ssids.find(ssid) == unique_ssids.end() || rssi > unique_ssids[ssid].rssi) {
                WifiNetworkInfo net;
                net.ssid = ssid;
                net.bssid = bssid;
                net.rssi = rssi;
                net.flags = flags;
                unique_ssids[ssid] = net;
            }
        }
    }
    pclose(pipe);

    std::vector<WifiNetworkInfo> networks;
    for (auto const & pair : unique_ssids) {
        if (pair.first != current_ssid) {
            networks.push_back(pair.second);
        }
    }

    std::cout << "Final available networks count: " << networks.size() << std::endl;
    for (const auto & net : networks) {
        std::cout << " - " << net.ssid << " (" << net.rssi << " dBm, flags: " << net.flags << ")" << std::endl;
    }

    return 0;
}

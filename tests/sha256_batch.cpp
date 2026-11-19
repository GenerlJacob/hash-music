// sha256_batch.cpp - helper for cross_check.py. Reads one hex-encoded message per line from standard
// input and prints the SHA-256 digest of each, one per line. An empty line is the empty message.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sha256.h"

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    std::vector<std::uint8_t> message;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() % 2 != 0) {
            std::cerr << "sha256_batch: odd number of hex digits on a line\n";
            return 1;
        }
        message.clear();
        for (std::size_t i = 0; i < line.size(); i += 2) {
            const int high = hex_value(line[i]);
            const int low = hex_value(line[i + 1]);
            if (high < 0 || low < 0) {
                std::cerr << "sha256_batch: not a hex digit\n";
                return 1;
            }
            message.push_back(static_cast<std::uint8_t>(high * 16 + low));
        }
        std::cout << hashmusic::to_hex(hashmusic::sha256(message)) << '\n';
    }
    return 0;
}

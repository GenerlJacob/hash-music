// avalanche.cpp - bit-level comparison of two digests for the avalanche demo.
// Owner: Member A (Jacob).
#include "avalanche.h"

#include <algorithm>
#include <bitset>
#include <vector>

namespace hashmusic {

int count_bit_differences(const Digest& a, const Digest& b) {
    int bits = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        bits += static_cast<int>(std::bitset<8>(static_cast<unsigned>(a[i] ^ b[i])).count());
    }
    return bits;
}

int count_hex_differences(const Digest& a, const Digest& b) {
    int digits = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((a[i] >> 4) != (b[i] >> 4)) ++digits;
        if ((a[i] & 0x0F) != (b[i] & 0x0F)) ++digits;
    }
    return digits;
}

std::string hex_difference_marker(const Digest& a, const Digest& b) {
    std::string marker;
    marker.reserve(a.size() * 2);
    for (std::size_t i = 0; i < a.size(); ++i) {
        marker += (a[i] >> 4) != (b[i] >> 4) ? '^' : ' ';
        marker += (a[i] & 0x0F) != (b[i] & 0x0F) ? '^' : ' ';
    }
    return marker;
}

std::string bit_difference_grid(const Digest& a, const Digest& b) {
    std::string grid;
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t byte_in_row = 0; byte_in_row < 4; ++byte_in_row) {
            const std::size_t index = row * 4 + byte_in_row;
            const auto flipped = static_cast<unsigned>(a[index] ^ b[index]);
            if (byte_in_row > 0) grid += ' ';
            for (int bit = 7; bit >= 0; --bit) grid += ((flipped >> bit) & 1u) ? '#' : '.';
        }
        grid += '\n';
    }
    return grid;
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    // Classic dynamic programming, keeping one row of the table at a time.
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            const std::size_t substitute = diagonal + (a[i - 1] == b[j - 1] ? 0 : 1);
            row[j] = std::min({above + 1, row[j - 1] + 1, substitute});
            diagonal = above;
        }
    }
    return row[b.size()];
}

}  // namespace hashmusic

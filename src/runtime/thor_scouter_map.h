// SPDX-License-Identifier: MPL-2.0
// Read-only Scouter presentation. Data layout follows ROM routines 08004B08,
// 0802574C and 080258A4. JCALG1 decoder adapted from buusfury-disasm's
// tools/research/jcalg1.py. No guest code or guest memory is modified.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace thor_scouter {
constexpr int width = 240;
constexpr int height = 160;

class Renderer {
    const uint8_t* rom_ = nullptr;
    size_t size_ = 0;
    uint32_t group_ = 0;
    uint32_t area_ = ~0u;
    uint32_t room_ = 0;
    std::vector<uint8_t> graphics_;
    std::vector<uint8_t> marker_graphics_;
    std::vector<uint8_t> visibility_;
    std::array<uint16_t, 128 * 128> atlas_{};

    uint32_t read(uint32_t address, unsigned bytes = 4) const {
        if (address < 0x08000000u ||
            size_t(address - 0x08000000u) + bytes > size_)
            throw std::out_of_range("Scouter ROM address");
        uint32_t value = 0;
        for (unsigned i = 0; i < bytes; ++i)
            value |= uint32_t(rom_[address - 0x08000000u + i]) << (i * 8);
        return value;
    }

    struct Bits {
        const Renderer& renderer;
        uint32_t address;
        uint32_t word = 0;
        unsigned remaining = 0;
        uint32_t get(unsigned count) {
            if (count > 24) throw std::out_of_range("Scouter bit width");
            uint32_t value = 0;
            for (unsigned i = 0; i < count; ++i) {
                if (!remaining) {
                    word = renderer.read(address);
                    address += 4;
                    remaining = 32;
                }
                value = (value << 1) | (word >> 31);
                word <<= 1;
                --remaining;
            }
            return value;
        }
        uint32_t gamma() {
            uint32_t value = 1;
            do {
                value = (value << 1) | get(1);
                if (value > 0x10003) throw std::out_of_range("Scouter gamma");
            } while (get(1));
            return value;
        }
    };

    std::vector<uint8_t> decompress(uint32_t address) const {
        const uint32_t type = read(address);
        const uint32_t length = read(address + 4);
        if (length > 0xe000) throw std::out_of_range("Scouter graphics length");
        std::vector<uint8_t> out;
        out.reserve(length);
        if (type == 0) {
            for (uint32_t i = 0; i < length; ++i)
                out.push_back(uint8_t(read(address + 8 + i, 1)));
            return out;
        }
        if (type != 1) throw std::out_of_range("Scouter compression type");
        Bits bits{*this, address + 8};
        unsigned literal_bits = 1, literal_base = 0, distance_bits = 8;
        uint32_t previous_distance = 1;
        auto put = [&](uint32_t value) {
            if (out.size() >= length) throw std::out_of_range("Scouter output");
            out.push_back(uint8_t(value));
        };
        auto copy = [&](uint32_t distance, uint32_t count) {
            if (!distance || distance > out.size() || count > length - out.size())
                throw std::out_of_range("Scouter distance");
            for (uint32_t i = 0; i < count; ++i)
                put(out[out.size() - distance]);
        };
        // Every control code consumes input, including codes emitting no output.
        for (unsigned commands = 0; commands < 1000000; ++commands) {
            if (bits.get(1)) {
                put(literal_base + bits.get(literal_bits));
            } else if (bits.get(1)) {
                uint32_t code = bits.gamma();
                if (code == 2) {
                    copy(previous_distance, bits.gamma());
                } else {
                    uint32_t distance = ((code - 3) << distance_bits) + bits.get(distance_bits);
                    previous_distance = distance;
                    unsigned bias = distance >= 0x10000 ? 3 : distance >= 0x37ff ? 2 :
                        distance >= 0x27f ? 1 : distance >= 0x80 ? 0 : 4;
                    copy(distance, bits.gamma() + bias);
                }
            } else if (!bits.get(1)) {
                uint32_t distance = bits.get(7);
                if (distance) {
                    previous_distance = distance;
                    copy(distance, bits.get(2) + 2);
                } else {
                    unsigned escape = bits.get(2);
                    if (!escape) {
                        if (out.size() != length) throw std::out_of_range("Scouter size mismatch");
                        return out;
                    }
                    distance_bits = bits.get(escape + 3);
                    if (distance_bits > 24) throw std::out_of_range("Scouter distance width");
                }
            } else {
                int distance = int(bits.get(4)) - 1;
                if (!distance) put(literal_base);
                else if (distance > 0) copy(unsigned(distance), 1);
                else if (bits.get(1)) {
                    do {
                        for (unsigned i = 0; i < 256; ++i) put(bits.get(8));
                    } while (bits.get(1));
                } else {
                    literal_bits = 7 + bits.get(1);
                    literal_base = literal_bits == 8 ? 0 : bits.get(8);
                }
            }
        }
        throw std::out_of_range("Scouter decode budget");
    }

    uint32_t color(unsigned index) const {
        uint32_t c = read(0x0805652cu + index * 2, 2);
        auto channel = [](uint32_t v) { return (v << 3) | (v >> 2); };
        return 0xff000000u | (channel(c & 31) << 16) |
            (channel((c >> 5) & 31) << 8) | channel((c >> 10) & 31);
    }

public:
    bool render(const uint8_t* rom, size_t size, const uint8_t* save,
                uint16_t area, int32_t x_fixed, int32_t y_fixed,
                int32_t world_width, int32_t world_height,
                std::vector<uint32_t>& pixels, uint64_t frame = 0) {
        try {
            if (!rom || !save || world_width <= 0 || world_height <= 0 ||
                world_width > 32768 || world_height > 32768) return false;
            if (rom != rom_ || size != size_) {
                rom_ = rom; size_ = size; area_ = ~0u; group_ = 0;
            }
            if (area_ != area) {
                group_ = 0; room_ = 0;
                uint32_t count = read(0x0874ca24u);
                uint32_t table = read(0x0874ca28u);
                if (count > 128) return false;
                for (uint32_t i = 0; i < count && !group_; ++i) {
                    uint32_t group = read(table + i * 4);
                    uint32_t rooms = read(group + 20);
                    if (rooms > 512) return false;
                    for (uint32_t j = 0; j < rooms; ++j) {
                        uint32_t room = read(group + 24 + j * 4);
                        if (read(room, 2) == area) { group_ = group; room_ = room; break; }
                    }
                }
                if (!group_) return false;
                graphics_ = decompress(read(group_));
                marker_graphics_.clear();
                if (read(group_ + 8)) marker_graphics_ = decompress(read(group_ + 16));
                visibility_.clear();
                area_ = area;
            }
            uint32_t count = read(group_ + 20);
            std::vector<uint8_t> visible(count);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t room = read(group_ + 24 + i * 4);
                unsigned bit = read(room + 2, 2);
                if (bit >= 768) return false;
                visible[i] = (save[0x8d + bit / 8] & (1u << (bit & 7))) ? 2 :
                    (save[1] & 0x40) ? 1 : 0;
            }
            if (visible != visibility_) {
                atlas_.fill(0);
                for (uint32_t i = 0; i < count; ++i) {
                    if (!visible[i]) continue;
                    uint32_t room = read(group_ + 24 + i * 4);
                    int left = int32_t(read(room + 4)), top = int32_t(read(room + 8));
                    int right = int32_t(read(room + 12)), bottom = int32_t(read(room + 16));
                    if (left < 0 || top < 0 || right < left || bottom < top || right > 128 || bottom > 128)
                        return false;
                    uint32_t data = room + 36;
                    if (visible[i] == 2) {
                        data += unsigned((right - left) * (bottom - top) * 2);
                        left = int32_t(read(room + 20)); top = int32_t(read(room + 24));
                        right = int32_t(read(room + 28)); bottom = int32_t(read(room + 32));
                    }
                    if (left < 0 || top < 0 || right < left || bottom < top || right > 128 || bottom > 128)
                        return false;
                    for (int y = top; y < bottom; ++y) {
                        for (int x = left; x < right; ++x, data += 2) {
                            uint16_t tile = uint16_t(read(data, 2));
                            if (tile) atlas_[y * 128 + x] = tile;
                        }
                    }
                }
                visibility_ = visible;
            }
            int left = int32_t(read(room_ + 4)), top = int32_t(read(room_ + 8));
            int right = int32_t(read(room_ + 12)), bottom = int32_t(read(room_ + 16));
            int px = left * 8 + int(((int64_t(right - left) * 8 * (x_fixed >> 8)) +
                (world_width >> 1)) / world_width);
            int py = top * 8 + int(((int64_t(bottom - top) * 8 * (y_fixed >> 8)) +
                (world_height >> 1)) / world_height);
            const int scroll_x = px - 120, scroll_y = py - 80;
            std::array<uint32_t, 256> palette;
            for (unsigned i = 0; i < palette.size(); ++i) palette[i] = color(i);
            pixels.assign(width * height, palette[0]);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int sx = x + scroll_x, sy = y + scroll_y;
                    if (sx < 0 || sy < 0 || sx >= 1024 || sy >= 1024) continue;
                    uint16_t tile = atlas_[(sy / 8) * 128 + sx / 8];
                    unsigned tx = unsigned(sx) & 7, ty = unsigned(sy) & 7;
                    if (tile & 0x400) tx = 7 - tx;
                    if (tile & 0x800) ty = 7 - ty;
                    size_t index = (tile & 0x3ff) * 64 + ty * 8 + tx;
                    if (index >= graphics_.size()) return false;
                    pixels[y * width + x] = palette[graphics_[index]];
                }
            }
            auto sprite = [&](int left, int top, const uint8_t* data) {
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        int dx = left + x, dy = top + y;
                        unsigned index = data[y * 8 + x];
                        if (index && dx >= 0 && dy >= 0 && dx < width && dy < height)
                            pixels[dy * width + dx] = palette[index];
                    }
                }
            };
            // All 78 descriptors in the supported ROM are 8x8, 8bpp sprites.
            // Validate these attributes instead of interpreting another layout.
            uint32_t markers = read(group_ + 8);
            if (markers > 128) return false;
            for (uint32_t i = markers; i > 0; --i) {
                uint32_t descriptor = read(group_ + 12) + (i - 1) * 24;
                if (!read(descriptor + 22, 1)) continue;
                if (read(descriptor + 16) != 0x2000) return false;
                size_t tile = read(descriptor + 20, 2) * 32;
                if (tile + 64 > marker_graphics_.size()) return false;
                sprite(int32_t(read(descriptor)) - scroll_x,
                    int32_t(read(descriptor + 4)) - scroll_y, marker_graphics_.data() + tile);
            }
            // 08026EE6/08026F2A: original red player sprite, centered as when
            // SELECT first opens the original map, with the same 4px anchor.
            unsigned phase = unsigned((frame & 63) >> 3);
            if (phase >= 4) phase = 7 - phase;
            std::array<uint8_t, 64> player;
            for (unsigned i = 0; i < player.size(); ++i)
                player[i] = uint8_t(read(0x0874cf2cu + phase * 64 + i, 1));
            sprite(116, 76, player.data());
            return true;
        } catch (const std::out_of_range&) {
            area_ = ~0u; group_ = 0; visibility_.clear();
            return false;
        }
    }
};
}

#include "vision/Image.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace undek {

namespace {

uint16_t readU16(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

uint32_t readU32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

void writeU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void writeU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

uint32_t rgb(int r, int g, int b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

} // namespace

int channelDistance(uint32_t a, uint32_t b) {
    int worst = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int da = static_cast<int>((a >> shift) & 0xFF);
        const int db = static_cast<int>((b >> shift) & 0xFF);
        worst = (std::max)(worst, da > db ? da - db : db - da);
    }
    return worst;
}

bool loadBmp(const std::string& path, ImageFrame& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "can't open " + path;
        return false;
    }

    // The whole file is read into memory, so its size is checked first: a stray
    // shortcut to a video shouldn't become a multi-gigabyte allocation before
    // the header has had a chance to say it isn't a BMP.
    constexpr uint64_t kMaxFileBytes = 64ull * 1024ull * 1024ull * 4ull + 4096ull;
    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    if (file_size < 0) {
        error = "can't read " + path;
        return false;
    }
    if (static_cast<uint64_t>(file_size) > kMaxFileBytes) {
        error = "BMP is too large to load: " + std::to_string(file_size) + " bytes";
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    return decodeBmp(bytes, out, error, path);
}

bool decodeBmp(const std::vector<uint8_t>& bytes, ImageFrame& out, std::string& error,
               const std::string& name) {
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
        error = "not a BMP file" + (name.empty() ? std::string() : ": " + name);
        return false;
    }

    const uint32_t data_offset = readU32(bytes, 10);
    const uint32_t dib_size = readU32(bytes, 14);
    if (dib_size < 40) {
        error = "unsupported BMP header";
        return false;
    }
    if (dib_size > bytes.size() - 14) {
        error = "BMP header claims more data than the buffer holds";
        return false;
    }
    const int32_t width = static_cast<int32_t>(readU32(bytes, 18));
    const int32_t height = static_cast<int32_t>(readU32(bytes, 22));
    const uint16_t planes = readU16(bytes, 26);
    const uint16_t bpp = readU16(bytes, 28);
    const uint32_t compression = readU32(bytes, 30);

    if (compression != 0) {
        error = "compressed BMP is not supported";
        return false;
    }
    if (planes != 1) {
        error = "BMP header is malformed: image planes is " + std::to_string(planes) + ", not 1";
        return false;
    }
    if (bpp != 24 && bpp != 32) {
        error = "only 24-bit and 32-bit BMP is supported";
        return false;
    }
    if (width <= 0 || height == 0) {
        error = "BMP has an empty image";
        return false;
    }

    // A template is a small crop, and every scan compares the whole thing, so a
    // ridiculous header gets refused before it can ask for gigabytes.
    constexpr int kMaxSide = 32768;
    constexpr uint64_t kMaxPixels = 64ull * 1024ull * 1024ull;
    const int64_t rows64 = height < 0 ? -static_cast<int64_t>(height) : height;
    if (width > kMaxSide || rows64 > kMaxSide ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(rows64) > kMaxPixels) {
        error = "BMP is too large (limit is 32768 px per side and 64M pixels)";
        return false;
    }
    const int rows = static_cast<int>(rows64);

    const bool top_down = height < 0;
    const int channels = bpp / 8;
    const uint64_t stride =
        ((static_cast<uint64_t>(width) * static_cast<uint64_t>(channels)) + 3ull) & ~3ull;
    // Pixels start after the header that describes them, whatever its size. An
    // offset of 54 only holds for the 40-byte header this writer produces.
    const uint64_t header_end = 14ull + static_cast<uint64_t>(dib_size);
    if (static_cast<uint64_t>(data_offset) < header_end) {
        error = "BMP pixel data starts inside its header";
        return false;
    }
    if (data_offset > bytes.size() ||
        stride * static_cast<uint64_t>(rows) > static_cast<uint64_t>(bytes.size() - data_offset)) {
        error = "BMP is truncated";
        return false;
    }

    ImageFrame frame;
    frame.width = width;
    frame.height = rows;
    frame.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(rows), 0u);

    for (int y = 0; y < rows; ++y) {
        const int src_row = top_down ? y : (rows - 1 - y);
        const size_t row_start = data_offset + static_cast<size_t>(src_row) * stride;
        for (int x = 0; x < width; ++x) {
            const size_t p = row_start + static_cast<size_t>(x) * channels;
            frame.pixels[static_cast<size_t>(y) * width + x] =
                rgb(bytes[p + 2], bytes[p + 1], bytes[p]);
        }
    }

    out = std::move(frame);
    error.clear();
    return true;
}

bool saveBmp(const std::string& path, const ImageFrame& frame, std::string& error) {
    if (!frame.valid()) {
        error = "nothing to save: the frame is empty";
        return false;
    }

    const size_t stride = ((static_cast<size_t>(frame.width) * 3) + 3) & ~static_cast<size_t>(3);
    const size_t image_size = stride * static_cast<size_t>(frame.height);
    // BMP sizes live in 32-bit fields, so a frame this big can't be written.
    if (image_size + 54ull > 0xFFFFFFFFull) {
        error = "image is too large to save as BMP";
        return false;
    }
    const uint32_t data_offset = 54;

    std::vector<uint8_t> bytes;
    bytes.reserve(data_offset + image_size);
    bytes.push_back('B');
    bytes.push_back('M');
    writeU32(bytes, static_cast<uint32_t>(data_offset + image_size));
    writeU16(bytes, 0);
    writeU16(bytes, 0);
    writeU32(bytes, data_offset);
    writeU32(bytes, 40);
    writeU32(bytes, static_cast<uint32_t>(frame.width));
    writeU32(bytes, static_cast<uint32_t>(frame.height));
    writeU16(bytes, 1);
    writeU16(bytes, 24);
    writeU32(bytes, 0);
    writeU32(bytes, static_cast<uint32_t>(image_size));
    writeU32(bytes, 2835);
    writeU32(bytes, 2835);
    writeU32(bytes, 0);
    writeU32(bytes, 0);

    for (int y = frame.height - 1; y >= 0; --y) {
        for (int x = 0; x < frame.width; ++x) {
            const uint32_t c = frame.at(x, y);
            bytes.push_back(static_cast<uint8_t>(c & 0xFF));
            bytes.push_back(static_cast<uint8_t>((c >> 8) & 0xFF));
            bytes.push_back(static_cast<uint8_t>((c >> 16) & 0xFF));
        }
        for (size_t pad = static_cast<size_t>(frame.width) * 3; pad < stride; ++pad) {
            bytes.push_back(0);
        }
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "can't write " + path;
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "write failed for " + path;
        return false;
    }
    error.clear();
    return true;
}

std::optional<ImageMatch> findImage(const ImageFrame& haystack, const ImageFrame& needle,
                                    int tolerance, const std::function<void()>& checkpoint,
                                    bool first_match, const SearchArea& area) {
    if (!haystack.valid() || !needle.valid()) return std::nullopt;
    if (needle.width > haystack.width || needle.height > haystack.height) return std::nullopt;

    const int tol = std::clamp(tolerance, 0, 255);
    const int max_x = haystack.width - needle.width;
    const int max_y = haystack.height - needle.height;

    // Top-left corners to try, in frame coordinates.
    int first_x = 0;
    int first_y = 0;
    int last_x = max_x;
    int last_y = max_y;
    if (area.set()) {
        // A capture that starts left of the primary monitor puts frame (0,0) at a
        // negative desktop coordinate, so the shift is subtracted, not added.
        const int from_x = area.x - haystack.origin_x;
        const int from_y = area.y - haystack.origin_y;
        // Corners where the whole template still fits inside the region...
        const int region_last_x = from_x + area.width - needle.width;
        const int region_last_y = from_y + area.height - needle.height;
        // ...intersected with the corners that exist in this capture.
        first_x = (std::max)(0, from_x);
        first_y = (std::max)(0, from_y);
        last_x = (std::min)(max_x, region_last_x);
        last_y = (std::min)(max_y, region_last_y);
        if (last_x < first_x || last_y < first_y) return std::nullopt;
    }

    bool found = false;
    ImageMatch best;
    const size_t needle_pixels = needle.pixels.size();

    for (int oy = first_y; oy <= last_y; ++oy) {
        if (checkpoint) checkpoint();
        for (int ox = first_x; ox <= last_x; ++ox) {
            // Cheap reject on the first pixel; most positions die here.
            if (channelDistance(haystack.at(ox, oy), needle.at(0, 0)) > tol) continue;

            bool ok = true;
            long long diff_sum = 0;
            for (int ty = 0; ty < needle.height && ok; ++ty) {
                for (int tx = 0; tx < needle.width; ++tx) {
                    const uint32_t a = haystack.at(ox + tx, oy + ty);
                    const uint32_t b = needle.at(tx, ty);
                    if (channelDistance(a, b) > tol) {
                        ok = false;
                        break;
                    }
                    for (int shift = 0; shift <= 16; shift += 8) {
                        const int da = static_cast<int>((a >> shift) & 0xFF);
                        const int db = static_cast<int>((b >> shift) & 0xFF);
                        diff_sum += da > db ? da - db : db - da;
                    }
                }
            }
            if (!ok) continue;

            const double score =
                1.0 - static_cast<double>(diff_sum) / (255.0 * 3.0 * needle_pixels);
            if (first_match) {
                ImageMatch hit;
                hit.x = ox;
                hit.y = oy;
                hit.width = needle.width;
                hit.height = needle.height;
                hit.score = score;
                return hit;
            }
            if (!found || score > best.score) {
                best.x = ox;
                best.y = oy;
                best.width = needle.width;
                best.height = needle.height;
                best.score = score;
                found = true;
                if (score >= 1.0) return best;
            }
        }
    }

    if (!found) return std::nullopt;
    return best;
}

} // namespace undek

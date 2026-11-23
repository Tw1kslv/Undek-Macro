#include "vision/Image.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

undek::ImageFrame makeFrame(int w, int h, uint32_t fill) {
    undek::ImageFrame f;
    f.width = w;
    f.height = h;
    f.pixels.assign(static_cast<size_t>(w) * h, fill);
    return f;
}

// Writes a BMP with whatever header fields the caller wants, so malformed files
// can be tested without shipping fixtures.
void writeRawBmp(const std::string& path, int32_t w, int32_t h, uint16_t bpp, uint32_t data_offset,
                 size_t payload_bytes) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint16_t v) {
        b.push_back(static_cast<uint8_t>(v & 0xFF));
        b.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto u32 = [&](uint32_t v) {
        for (int s = 0; s < 32; s += 8)
            b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
    };
    b.push_back('B');
    b.push_back('M');
    u32(static_cast<uint32_t>(data_offset + payload_bytes));
    u16(0);
    u16(0);
    u32(data_offset);
    u32(40);
    u32(static_cast<uint32_t>(w));
    u32(static_cast<uint32_t>(h));
    u16(1);
    u16(bpp);
    u32(0);
    u32(static_cast<uint32_t>(payload_bytes));
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);
    if (b.size() < data_offset) b.resize(data_offset, 0);
    for (size_t i = 0; i < payload_bytes; ++i)
        b.push_back(0);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

// A 32-bit, top-down BMP - the other layout a real screenshot tool hands us.
void writeTopDown32(const std::string& path, int w, int h, const std::vector<uint32_t>& pixels) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint16_t v) {
        b.push_back(static_cast<uint8_t>(v & 0xFF));
        b.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto u32 = [&](uint32_t v) {
        for (int s = 0; s < 32; s += 8)
            b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
    };
    const uint32_t image_size = static_cast<uint32_t>(w) * h * 4;
    b.push_back('B');
    b.push_back('M');
    u32(54 + image_size);
    u16(0);
    u16(0);
    u32(54);
    u32(40);
    u32(static_cast<uint32_t>(w));
    u32(static_cast<uint32_t>(-h)); // negative height = rows stored top-down
    u16(1);
    u16(32);
    u32(0);
    u32(image_size);
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);
    for (uint32_t c : pixels) {
        b.push_back(static_cast<uint8_t>(c & 0xFF));
        b.push_back(static_cast<uint8_t>((c >> 8) & 0xFF));
        b.push_back(static_cast<uint8_t>((c >> 16) & 0xFF));
        b.push_back(0xFF);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

} // namespace

int main() {
    const std::string bmp = "test_vision_frame.bmp";

    // Round-trip through the writer and reader, including rows that need padding
    {
        undek::ImageFrame f = makeFrame(13, 7, 0x102030);
        f.pixels[0] = 0xFFFFFF;
        f.pixels[f.pixels.size() - 1] = 0x010203;
        std::string err;
        check(undek::saveBmp(bmp, f, err), "bmp saves: " + err);

        undek::ImageFrame loaded;
        check(undek::loadBmp(bmp, loaded, err), "bmp loads back: " + err);
        check(loaded.width == 13 && loaded.height == 7, "size survives the round-trip");
        check(loaded.at(0, 0) == 0xFFFFFF, "top-left pixel survives (row order is right)");
        check(loaded.at(12, 6) == 0x010203, "bottom-right pixel survives");
        check(loaded.at(5, 3) == 0x102030, "padding does not shift the rows");
        std::remove(bmp.c_str());
    }

    // 32-bit top-down files decode the same way
    {
        const std::vector<uint32_t> px = {0x112233, 0x445566, 0x778899, 0xAABBCC,
                                          0x010101, 0x020202, 0x030303, 0x040404};
        writeTopDown32(bmp, 2, 4, px);
        std::string err;
        undek::ImageFrame loaded;
        check(undek::loadBmp(bmp, loaded, err), "32-bit top-down bmp loads: " + err);
        check(loaded.width == 2 && loaded.height == 4, "32-bit size is right");
        check(loaded.at(0, 0) == 0x112233 && loaded.at(1, 0) == 0x445566,
              "top-down rows are not flipped");
        check(loaded.at(1, 3) == 0x040404, "bottom row of a top-down image is right");
        std::remove(bmp.c_str());
    }

    // A bad file is an error, not an empty frame
    {
        {
            std::ofstream out(bmp, std::ios::binary);
            out << "not a bmp at all";
        }
        std::string err;
        undek::ImageFrame loaded;
        check(!undek::loadBmp(bmp, loaded, err) && !err.empty(), "junk file is rejected");
        std::remove(bmp.c_str());
    }

    // Find a patch in a bigger frame
    {
        undek::ImageFrame screen = makeFrame(64, 48, 0x203040);
        undek::ImageFrame patch = makeFrame(5, 4, 0);
        for (int y = 0; y < patch.height; ++y) {
            for (int x = 0; x < patch.width; ++x) {
                patch.pixels[static_cast<size_t>(y) * patch.width + x] =
                    (x == y) ? 0xFFFFFF : 0x778899;
            }
        }
        for (int y = 0; y < patch.height; ++y) {
            for (int x = 0; x < patch.width; ++x) {
                screen.pixels[static_cast<size_t>(13 + y) * screen.width + (21 + x)] =
                    patch.pixels[static_cast<size_t>(y) * patch.width + x];
            }
        }

        const auto hit = undek::findImage(screen, patch);
        check(hit.has_value(), "exact patch is found");
        check(hit && hit->x == 21 && hit->y == 13, "patch is found at the right offset");
        check(hit && hit->score == 1.0, "exact patch scores 1.0");

        check(!undek::findImage(makeFrame(64, 48, 0x203040), patch).has_value(),
              "patch that isn't there is reported as missing");

        undek::ImageFrame tiny = makeFrame(3, 3, 0);
        check(!undek::findImage(tiny, patch).has_value(),
              "a template bigger than the frame never matches");

        // Nudge one patch pixel just outside and then inside the tolerance
        undek::ImageFrame noisy = screen;
        noisy.pixels[static_cast<size_t>(14) * noisy.width + 22] = 0xFCFDF9; // 6 off in blue
        check(!undek::findImage(noisy, patch, 5).has_value(),
              "a pixel outside the tolerance rejects the position");
        const auto loose = undek::findImage(noisy, patch, 8);
        check(loose && loose->x == 21 && loose->y == 13, "a looser tolerance accepts it");
        check(loose && loose->score < 1.0, "an imperfect match scores below 1.0");
    }

    // A region narrows the search to part of the screen
    {
        undek::ImageFrame screen = makeFrame(64, 48, 0x203040);
        undek::ImageFrame patch = makeFrame(5, 4, 0x112233);
        for (int y = 0; y < patch.height; ++y) {
            for (int x = 0; x < patch.width; ++x) {
                screen.pixels[static_cast<size_t>(13 + y) * screen.width + (21 + x)] =
                    patch.pixels[static_cast<size_t>(y) * patch.width + x];
            }
        }

        const undek::SearchArea around{20, 10, 10, 10};
        const auto hit = undek::findImage(screen, patch, 0, nullptr, true, around);
        check(hit && hit->x == 21 && hit->y == 13, "a region holding the patch finds it");

        const undek::SearchArea elsewhere{0, 0, 10, 10};
        check(!undek::findImage(screen, patch, 0, nullptr, true, elsewhere).has_value(),
              "a region that misses the patch finds nothing");

        const undek::SearchArea hanging{16, 8, 400, 400};
        check(undek::findImage(screen, patch, 0, nullptr, true, hanging).has_value(),
              "a region hanging off the edge searches the part still inside");

        const undek::SearchArea off_screen{200, 200, 10, 10};
        check(!undek::findImage(screen, patch, 0, nullptr, true, off_screen).has_value(),
              "a region entirely off the frame matches nothing");

        // Region coordinates are desktop coordinates, and a capture can start
        // left of or above the primary monitor.
        undek::ImageFrame shifted = screen;
        shifted.origin_x = 1920;
        shifted.origin_y = -100;
        const undek::SearchArea on_desktop{1920 + 20, -100 + 10, 10, 10};
        const auto shifted_hit = undek::findImage(shifted, patch, 0, nullptr, true, on_desktop);
        check(shifted_hit && shifted_hit->x == 21 && shifted_hit->y == 13,
              "a desktop-coordinate region lines up with the capture");

        const undek::SearchArea unset{};
        check(undek::findImage(screen, patch, 0, nullptr, true, unset).has_value(),
              "an unset region searches the whole screen");
    }

    // Broken headers are refused with a reason, not an empty frame
    {
        std::string err;
        undek::ImageFrame loaded;

        writeRawBmp(bmp, 0, 4, 24, 54, 0);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("empty image") != std::string::npos,
              "a zero-width BMP is rejected (" + err + ")");

        writeRawBmp(bmp, 200000, 4, 24, 54, 0);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("too large") != std::string::npos,
              "an absurd width is rejected before allocating (" + err + ")");

        writeRawBmp(bmp, 4, 4, 24, 54, 0);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("truncated") != std::string::npos,
              "a header with no pixel data is rejected (" + err + ")");

        writeRawBmp(bmp, 4, 4, 8, 54, 64);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("24-bit") != std::string::npos,
              "an 8-bit BMP is rejected with the supported depths (" + err + ")");

        writeRawBmp(bmp, 4, 4, 16, 54, 64);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("24-bit") != std::string::npos,
              "a 16-bit BMP is rejected with the supported depths (" + err + ")");

        writeRawBmp(bmp, 4, 0, 24, 54, 0);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("empty image") != std::string::npos,
              "a zero-height BMP is rejected (" + err + ")");

        // Both sides are legal on their own, so this has to be caught on the
        // pixel count rather than the per-side limit.
        writeRawBmp(bmp, 20000, 20000, 24, 54, 0);
        check(!undek::loadBmp(bmp, loaded, err) && err.find("too large") != std::string::npos,
              "a BMP with too many pixels is rejected before allocating (" + err + ")");

        writeRawBmp(bmp, 4, 4, 24, 10, 48);
        check(!undek::loadBmp(bmp, loaded, err),
              "a pixel offset pointing into the header is rejected (" + err + ")");

        // A valid file with header fields edited afterwards
        auto rewrite = [&](const std::vector<uint8_t>& bytes) {
            std::ofstream out(bmp, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        };
        auto readAll = [&]() {
            std::ifstream in(bmp, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                        std::istreambuf_iterator<char>());
        };

        {
            undek::saveBmp(bmp, makeFrame(4, 4, 0x112233), err);
            std::vector<uint8_t> bytes = readAll();

            // The same bytes decode without going near the disk, which is what
            // the fuzzer feeds the reader
            undek::ImageFrame decoded;
            check(undek::decodeBmp(bytes, decoded, err) && decoded.at(0, 0) == 0x112233,
                  "bytes decode in memory (" + err + ")");

            // The DIB header says it is far bigger than the file
            std::vector<uint8_t> claiming = bytes;
            claiming[14] = 0xFF;
            claiming[15] = 0xFF;
            claiming[16] = 0xFF;
            claiming[17] = 0x7F;
            rewrite(claiming);
            check(!undek::loadBmp(bmp, loaded, err) &&
                      err.find("claims more data") != std::string::npos,
                  "a DIB header past the end of the file is rejected (" + err + ")");

            // An extended header means the pixels can't start at 54
            undek::saveBmp(bmp, makeFrame(8, 8, 0x445566), err);
            std::vector<uint8_t> extended = readAll();
            extended[14] = 124;
            extended.resize(extended.size() + 200, 0);
            rewrite(extended);
            check(!undek::loadBmp(bmp, loaded, err) &&
                      err.find("inside its header") != std::string::npos,
                  "a pixel offset that overlaps an extended header is rejected (" + err + ")");

            // Planes has to be 1
            undek::saveBmp(bmp, makeFrame(4, 4, 0x112233), err);
            std::vector<uint8_t> planes = bytes;
            planes[26] = 2;
            rewrite(planes);
            check(!undek::loadBmp(bmp, loaded, err) && err.find("planes") != std::string::npos,
                  "a BMP claiming more than one plane is rejected (" + err + ")");

            // Extra bytes after the pixels are ignored, not read as pixels
            std::vector<uint8_t> padded = bytes;
            padded.resize(padded.size() + 1024, 0x5A);
            rewrite(padded);
            check(undek::loadBmp(bmp, loaded, err) && loaded.at(0, 0) == 0x112233,
                  "trailing data after the last row is ignored (" + err + ")");

            // A pixel offset past the end of the file, but still a plausible
            // number rather than something obviously broken
            std::vector<uint8_t> far = bytes;
            far[10] = 0xFF;
            far[11] = 0xFF;
            far[12] = 0;
            far[13] = 0;
            rewrite(far);
            check(!undek::loadBmp(bmp, loaded, err) && err.find("truncated") != std::string::npos,
                  "a pixel offset past the end of the file is rejected (" + err + ")");

            // Compression is refused rather than guessed at
            std::vector<uint8_t> compressed = bytes;
            compressed[30] = 1;
            rewrite(compressed);
            check(!undek::loadBmp(bmp, loaded, err) && err.find("compressed") != std::string::npos,
                  "a compressed BMP is rejected (" + err + ")");
        }

        // Rows are padded out to four bytes, and an odd width is exactly where
        // that padding could be read as pixels.
        {
            undek::ImageFrame odd;
            odd.width = 5;
            odd.height = 3;
            odd.pixels.resize(15);
            for (size_t i = 0; i < odd.pixels.size(); ++i) {
                odd.pixels[i] = (0x010203u * static_cast<uint32_t>(i + 1)) & 0xFFFFFFu;
            }
            check(undek::saveBmp(bmp, odd, err), "an odd-width frame saves: " + err);
            undek::ImageFrame back;
            check(undek::loadBmp(bmp, back, err) && back.width == 5 && back.height == 3 &&
                      back.pixels == odd.pixels,
                  "an odd-width frame round-trips pixel for pixel: " + err);
            std::remove(bmp.c_str());
        }

        // A file too big to be a template is refused on its size alone
        {
            std::ofstream big(bmp, std::ios::binary);
            big.seekp(300ll * 1024 * 1024);
            big.put('B');
            big.close();
            check(!undek::loadBmp(bmp, loaded, err) && err.find("too large") != std::string::npos,
                  "a huge file is refused before it is read into memory (" + err + ")");
        }

        std::remove(bmp.c_str());
    }

    // A checkpoint gets to interrupt a long scan
    {
        undek::ImageFrame screen = makeFrame(60, 40, 0x203040);
        undek::ImageFrame patch = makeFrame(3, 3, 0xFFFFFF);
        int rows_seen = 0;
        bool threw = false;
        try {
            undek::findImage(screen, patch, 0, [&] {
                if (++rows_seen == 2) throw std::runtime_error("stop");
            });
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw && rows_seen == 2, "the checkpoint can abort the scan part-way");
        check(rows_seen > 0, "the checkpoint runs");
    }

    check(undek::channelDistance(0x102030, 0x132333) == 3, "channel distance is the worst channel");
    check(undek::channelDistance(0xFFFFFF, 0x000000) == 255, "channel distance spans 0-255");

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS\n";
    return 0;
}

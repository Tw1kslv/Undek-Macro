// Throws rubbish at the parts that read input and checks they fail politely.
// Not a real fuzzer - just a fixed seed, a few hundred rounds and no hangs.

#include "script/Lexer.h"
#include "script/Parser.h"
#include "vision/Image.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
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

void writeBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    std::mt19937 rng(0xC0FFEEu);
    const std::string bmp = "test_fuzz_frame.bmp";

    // Random bytes through the lexer and parser
    for (int round = 0; round < 400; ++round) {
        std::string source;
        const int len = static_cast<int>(rng() % 200);
        for (int i = 0; i < len; ++i) {
            source.push_back(static_cast<char>(rng() % 256));
        }
        try {
            undek::script::Lexer lexer(source);
            undek::script::Parser parser(lexer.tokenize());
            parser.parse();
        } catch (const std::exception&) {
            // Errors are the expected outcome; the point is that it's an exception
            // and not a crash or a hang.
        } catch (...) {
            check(false, "lexer/parser threw something that isn't a std::exception");
        }
    }

    // Random soup built from real tokens, which gets much deeper into the parser
    static const char* const vocab[] = {
        "if",          "else",     "loop",   "while",       "repeat_until", "break",
        "var",         "Call",     "Click",  "PressKey",    "KeyDown",      "KeyUp",
        "MoveTo",      "Scroll",   "Wait",   "WaitUntil",   "Log",          "include",
        "random_wait", "profile",  "Point",  "ImageExists", "FindImage",    "{",
        "}",           "(",        ")",      "=",           "==",           ",",
        "+",           "-",        "*",      "/",           "not",          "1",
        "0.5",         "ms",       "\"x\"",  "\"y\"",       "true",         "false",
        "\"a.undek\"", "humanize", "radius", "timeout",
    };
    const size_t vocab_size = sizeof(vocab) / sizeof(vocab[0]);
    for (int round = 0; round < 600; ++round) {
        std::string source;
        const int count = 1 + static_cast<int>(rng() % 40);
        for (int i = 0; i < count; ++i) {
            source += vocab[rng() % vocab_size];
            source += (rng() % 3 == 0) ? '\n' : ' ';
        }
        try {
            undek::script::Lexer lexer(source);
            undek::script::Parser parser(lexer.tokenize());
            parser.parse();
        } catch (const std::exception&) {
        } catch (...) {
            check(false, "token soup threw something that isn't a std::exception");
        }
    }

    // Random BMP headers and payloads
    for (int round = 0; round < 300; ++round) {
        std::vector<uint8_t> bytes;
        bytes.reserve(300);
        bytes.push_back('B');
        bytes.push_back('M');
        const int total = static_cast<int>(rng() % 260);
        for (int i = 0; i < total; ++i)
            bytes.push_back(static_cast<uint8_t>(rng() % 256));
        if (rng() % 4 == 0) bytes.resize(static_cast<size_t>(rng() % 8)); // sometimes tiny

        writeBytes(bmp, bytes);
        undek::ImageFrame frame;
        std::string error;
        const bool loaded = undek::loadBmp(bmp, frame, error);
        check(loaded || !error.empty(), "a rejected BMP always explains itself");
        if (loaded) {
            check(frame.valid(), "a loaded BMP is a valid frame");

            undek::ImageFrame fake;
            fake.width = 1 + static_cast<int>(rng() % 10);
            fake.height = 1 + static_cast<int>(rng() % 10);
            fake.pixels.assign(static_cast<size_t>(fake.width) * fake.height, 0x123456);
            undek::findImage(frame, fake, static_cast<int>(rng() % 256));
        }
    }
    std::remove(bmp.c_str());

    // Mutating a valid BMP gets much further than random bytes: the header is
    // real, so the decoder reaches the stride and pixel loop before it can tell
    // something is wrong.
    {
        undek::ImageFrame original;
        original.width = 6;
        original.height = 5;
        original.pixels.resize(30);
        for (size_t i = 0; i < original.pixels.size(); ++i) {
            original.pixels[i] = static_cast<uint32_t>(0x101010u * (i + 1)) & 0xFFFFFFu;
        }

        std::string err;
        const std::string seed_path = "test_fuzz_seed.bmp";
        check(undek::saveBmp(seed_path, original, err), "the mutation seed saves: " + err);
        std::ifstream in(seed_path, std::ios::binary);
        const std::vector<uint8_t> seed((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        std::remove(seed_path.c_str());
        check(seed.size() > 54, "the mutation seed is a real file");

        {
            undek::ImageFrame decoded;
            check(undek::decodeBmp(seed, decoded, err) && decoded.pixels == original.pixels &&
                      decoded.width == original.width && decoded.height == original.height,
                  "the untouched seed decodes back to the same pixels: " + err);
        }

        int still_decoded = 0;
        for (int round = 0; round < 500; ++round) {
            std::vector<uint8_t> bytes = seed;
            const int edits = 1 + static_cast<int>(rng() % 4);
            for (int e = 0; e < edits; ++e) {
                // Mostly the header, where the fields that matter live.
                const size_t at = (rng() % 3 == 0) ? (static_cast<size_t>(rng()) % bytes.size())
                                                   : (static_cast<size_t>(rng()) % 54);
                bytes[at] = static_cast<uint8_t>(rng() % 256);
            }

            undek::ImageFrame frame;
            std::string error;
            if (undek::decodeBmp(bytes, frame, error)) {
                ++still_decoded;
                check(frame.valid(), "a mutated BMP that decodes is still a usable frame");
            } else {
                check(!error.empty(), "a rejected mutated BMP explains itself");
            }
        }
        check(still_decoded > 0, "some mutations still decode, so the seed reaches the pixels");

        // Every way of cutting the file short has to be refused, not read past
        for (size_t cut = 0; cut < seed.size(); cut += 7) {
            const std::vector<uint8_t> part(seed.begin(), seed.begin() + static_cast<long>(cut));
            undek::ImageFrame frame;
            std::string error;
            check(!undek::decodeBmp(part, frame, error) && !error.empty(),
                  "a BMP cut down to " + std::to_string(cut) + " bytes is refused");
        }
    }

    // The same idea for a script: start from something that parses, then break
    // it in the ways an editor or a bad merge would.
    {
        const std::string base =
            "var spot = Point(20, 30)\n"
            "if PixelAt(5, 5) == 0x102030 tolerance 4 { Log(\"hit\") } else { Log(\"miss\") }\n"
            "loop 3 { MoveTo(spot, humanize=false) Wait(2ms) }\n"
            "profile default_sigma=12\n"
            "Click(Point(1, 2), radius=3, humanize=true)\n";
        try {
            undek::script::Lexer lexer(base);
            undek::script::Parser parser(lexer.tokenize());
            check(parser.parse().statements.size() == 5, "the script mutation seed parses");
        } catch (const std::exception& ex) {
            check(false, std::string("the script mutation seed should parse: ") + ex.what());
        }

        for (int round = 0; round < 800; ++round) {
            std::string source = base;
            const int edits = 1 + static_cast<int>(rng() % 5);
            for (int e = 0; e < edits; ++e) {
                const size_t at = static_cast<size_t>(rng()) % source.size();
                switch (rng() % 3) {
                    case 0:
                        source[at] = static_cast<char>(rng() % 256);
                        break;
                    case 1:
                        source.erase(at, 1);
                        break;
                    default:
                        source.insert(at, 1, static_cast<char>(rng() % 256));
                        break;
                }
            }
            try {
                undek::script::Lexer lexer(source);
                undek::script::Parser parser(lexer.tokenize());
                parser.parse();
            } catch (const std::exception&) {
            } catch (...) {
                check(false, "a mutated script threw something that isn't a std::exception");
            }
        }
    }

    // Degenerate frames shouldn't be matched at all
    {
        undek::ImageFrame empty;
        undek::ImageFrame ok;
        ok.width = 3;
        ok.height = 3;
        ok.pixels.assign(9, 0);
        check(!undek::findImage(empty, ok).has_value(), "an empty haystack has no matches");
        check(!undek::findImage(ok, empty).has_value(), "an empty template never matches");
        check(!undek::findImage(undek::ImageFrame{}, undek::ImageFrame{}).has_value(),
              "two empty frames don't match");
    }

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS\n";
    return 0;
}

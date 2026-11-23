#include "vision/Image.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

// Template BMPs are read from disk and can be anything at all, so the decoder
// is handed raw bytes here. Most inputs get refused; the point is that a
// malformed header never gets past the checks and into the pixel loop.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::vector<uint8_t> bytes(data, data + size);
    undek::ImageFrame frame;
    std::string error;
    try {
        if (undek::decodeBmp(bytes, frame, error)) {
            // A frame that loaded should be usable straight away.
            (void)frame.at(0, 0);
        }
    } catch (const std::exception&) {
        // Rejections are reported, not thrown, but a throw here would still be
        // worth knowing about rather than ending the run.
    }
    return 0;
}

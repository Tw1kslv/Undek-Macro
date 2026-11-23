#pragma once

#include <string>

namespace undek {

// A key written as a raw scancode, e.g. "scan:0x1e" or "scan_e:0x4b" for the
// E0-prefixed ones. The recorder writes these for keys it has no name for, so
// a take never loses a press just because the name table missed it.
struct ScanKey {
    unsigned short code = 0;
    bool extended = false;
};

bool parseScanKey(const std::string& key, ScanKey& out);
std::string formatScanKey(unsigned short code, bool extended);

} // namespace undek

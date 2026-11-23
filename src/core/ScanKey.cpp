#include "core/ScanKey.h"

#include <cctype>
#include <cstdio>

namespace undek {
namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    return std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
}

} // namespace

bool parseScanKey(const std::string& key, ScanKey& out) {
    const std::string extended_prefix = "scan_e:";
    const std::string plain_prefix = "scan:";

    bool extended = false;
    std::string rest;
    if (key.compare(0, extended_prefix.size(), extended_prefix) == 0) {
        extended = true;
        rest = key.substr(extended_prefix.size());
    } else if (key.compare(0, plain_prefix.size(), plain_prefix) == 0) {
        rest = key.substr(plain_prefix.size());
    } else {
        return false;
    }

    if (rest.size() < 3) return false;
    if (rest[0] != '0' || (rest[1] != 'x' && rest[1] != 'X')) return false;

    const std::string digits = rest.substr(2);
    if (digits.size() > 2) return false; // scancodes are a byte

    unsigned short code = 0;
    for (char c : digits) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        code = static_cast<unsigned short>(code * 16 + hexDigit(c));
    }
    if (code == 0) return false;

    out.code = code;
    out.extended = extended;
    return true;
}

std::string formatScanKey(unsigned short code, bool extended) {
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%s0x%02x",
                  extended ? "scan_e:" : "scan:", static_cast<unsigned>(code & 0xFFu));
    return buf;
}

} // namespace undek

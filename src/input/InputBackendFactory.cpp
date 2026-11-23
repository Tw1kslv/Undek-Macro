#include "input/InputBackendFactory.h"

#include "input/InterceptionBackend.h"
#include "input/ThrottledInputBackend.h"
#include "input/Win32SendInputBackend.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace undek {

InputBackendResult createInputBackend(const HumanizationProfile& profile) {
    InputBackendResult result;

    std::string want = profile.input_backend;
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool try_interception = (want == "interception" || want == "auto");

    if (try_interception) {
        auto interception = std::make_unique<InterceptionBackend>();
        if (interception->available()) {
            result.name = "Interception driver";
            result.backend = std::move(interception);
        } else {
            result.name = std::string("SendInput (Interception unavailable: ") +
                          interception->statusName() + ")";
            result.backend = std::make_unique<Win32SendInputBackend>();
        }
    } else {
        result.name = "SendInput";
        result.backend = std::make_unique<Win32SendInputBackend>();
    }

    if (profile.min_packet_delay_enabled && profile.min_packet_delay_ms > 0.0) {
        std::ostringstream oss;
        oss << result.name << " + packet spacing " << std::fixed << std::setprecision(0)
            << profile.min_packet_delay_ms << "-"
            << (profile.min_packet_delay_ms + profile.min_packet_delay_jitter_ms) << "ms";
        result.name = oss.str();
        result.backend = std::make_unique<ThrottledInputBackend>(
            std::move(result.backend), profile.min_packet_delay_ms,
            profile.min_packet_delay_jitter_ms);
    }

    return result;
}

} // namespace undek

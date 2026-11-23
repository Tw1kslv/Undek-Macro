#pragma once

#include "core/HumanizationProfile.h"
#include "input/IInputBackend.h"

#include <memory>
#include <string>

namespace undek {

struct InputBackendResult {
    std::unique_ptr<IInputBackend> backend;
    std::string name;
};

InputBackendResult createInputBackend(const HumanizationProfile& profile);

} // namespace undek

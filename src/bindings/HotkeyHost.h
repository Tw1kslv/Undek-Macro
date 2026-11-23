#pragma once

#include "bindings/BindingConfig.h"

#include <string>

namespace undek::bindings {

class HotkeyHost {
public:
    explicit HotkeyHost(BindingConfig config);
    int run();

private:
    BindingConfig config_;
};

} // namespace undek::bindings

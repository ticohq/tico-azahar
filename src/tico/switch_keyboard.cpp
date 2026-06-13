// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "tico/switch_keyboard.h"

#include <array>
#include <string>

#include "tico/switch_libnx.h"
#include "common/logging/log.h"

namespace SwitchFrontend {
namespace {

constexpr std::size_t OutputBufferSize = 0x1000;

u8 OkButtonId(Frontend::ButtonConfig config) {
    return static_cast<u8>(config);
}

u8 CancelButtonId(Frontend::ButtonConfig config) {
    switch (config) {
    case Frontend::ButtonConfig::Dual:
    case Frontend::ButtonConfig::Triple:
        return 0;
    case Frontend::ButtonConfig::Single:
    case Frontend::ButtonConfig::None:
    default:
        return OkButtonId(config);
    }
}

const char* ErrorText(Frontend::ValidationError error) {
    switch (error) {
    case Frontend::ValidationError::None:
        return "";
    case Frontend::ValidationError::MaxDigitsExceeded:
        return "Too many digits";
    case Frontend::ValidationError::AtSignNotAllowed:
        return "@ is not allowed";
    case Frontend::ValidationError::PercentNotAllowed:
        return "% is not allowed";
    case Frontend::ValidationError::BackslashNotAllowed:
        return "\\ is not allowed";
    case Frontend::ValidationError::MaxLengthExceeded:
        return "Text is too long";
    case Frontend::ValidationError::BlankInputNotAllowed:
        return "Blank input is not allowed";
    case Frontend::ValidationError::EmptyInputNotAllowed:
        return "Empty input is not allowed";
    case Frontend::ValidationError::FixedLengthRequired:
        return "Text has the wrong length";
    case Frontend::ValidationError::ButtonOutOfRange:
    case Frontend::ValidationError::ProfanityNotAllowed:
    case Frontend::ValidationError::CallbackFailed:
    default:
        return "Invalid input";
    }
}

u32 GetDisabledKeyMask(const Frontend::KeyboardConfig& config) {
    u32 mask = 0;
    if (config.filters.prevent_digit) {
        mask |= SwkbdKeyDisableBitmask_Numbers;
    }
    if (config.filters.prevent_at) {
        mask |= SwkbdKeyDisableBitmask_At;
    }
    if (config.filters.prevent_percent) {
        mask |= SwkbdKeyDisableBitmask_Percent;
    }
    if (config.filters.prevent_backslash) {
        mask |= SwkbdKeyDisableBitmask_Backslash;
    }
    return mask;
}

u32 GetMinimumLength(const Frontend::KeyboardConfig& config) {
    switch (config.accept_mode) {
    case Frontend::AcceptedInput::FixedLength:
        return config.max_text_length;
    case Frontend::AcceptedInput::NotEmpty:
    case Frontend::AcceptedInput::NotEmptyAndNotBlank:
        return 1;
    case Frontend::AcceptedInput::Anything:
    case Frontend::AcceptedInput::NotBlank:
    default:
        return 0;
    }
}

} // namespace

void SwitchKeyboard::Execute(const Frontend::KeyboardConfig& config_) {
    SoftwareKeyboard::Execute(config_);

    for (;;) {
        SwkbdConfig swkbd{};
        const LibnxResult create_rc = swkbdCreate(&swkbd, 0);
        if (create_rc != 0) {
            LOG_ERROR(Frontend, "swkbdCreate failed: 0x{:08x}", static_cast<unsigned>(create_rc));
            validation_error.clear();
            Finalize({}, CancelButtonId(config.button_config));
            return;
        }

        swkbdConfigMakePresetDefault(&swkbd);
        swkbdConfigSetType(&swkbd, SwkbdType_QWERTY);
        swkbdConfigSetReturnButtonFlag(&swkbd, config.multiline_mode ? 1 : 0);
        swkbdConfigSetKeySetDisableBitmask(&swkbd, GetDisabledKeyMask(config));
        swkbdConfigSetStringLenMin(&swkbd, GetMinimumLength(config));
        if (config.max_text_length > 0) {
            swkbdConfigSetStringLenMax(&swkbd, config.max_text_length);
        }

        if (!config.hint_text.empty()) {
            swkbdConfigSetGuideText(&swkbd, config.hint_text.c_str());
        }
        if (!validation_error.empty()) {
            swkbdConfigSetSubText(&swkbd, validation_error.c_str());
        }

        const u8 ok_button = OkButtonId(config.button_config);
        if (ok_button < config.button_text.size() && !config.button_text[ok_button].empty()) {
            swkbdConfigSetOkButtonText(&swkbd, config.button_text[ok_button].c_str());
        }

        std::array<char, OutputBufferSize> output{};
        const LibnxResult show_rc = swkbdShow(&swkbd, output.data(), output.size());
        swkbdClose(&swkbd);

        if (show_rc != 0) {
            LOG_INFO(Frontend, "Switch swkbd cancelled/failed: 0x{:08x}",
                     static_cast<unsigned>(show_rc));
            validation_error.clear();
            Finalize({}, CancelButtonId(config.button_config));
            return;
        }

        const std::string text{output.data()};
        const Frontend::ValidationError error = Finalize(text, ok_button);
        if (error == Frontend::ValidationError::None) {
            LOG_INFO(Frontend, "Switch swkbd accepted input length={}", text.size());
            validation_error.clear();
            return;
        }

        validation_error = ErrorText(error);
        LOG_INFO(Frontend, "Switch swkbd rejected input: {}", validation_error);
    }
}

void SwitchKeyboard::ShowError(const std::string& error) {
    validation_error = error;
    LOG_ERROR(Frontend, "Switch swkbd callback error: {}", error);
}

} // namespace SwitchFrontend

#pragma once

namespace FileLog {

// Redirects brls::Logger output to <working dir>/log.log when enabled, and
// restores stdout when disabled. Safe to call repeatedly, and can be toggled at
// runtime from the settings UI.
void setEnabled(bool enabled);

[[nodiscard]] bool isEnabled();

} // namespace FileLog

#pragma once

#include "app_types.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::app {

[[nodiscard]] std::string EncodeEntry(const ProgramEntry& entry);
[[nodiscard]] bool DecodeEntry(
    ProgramEntryId id,
    std::string_view text,
    ProgramEntry& entry,
    std::string& error);

[[nodiscard]] std::string EncodeProgramIndex(
    std::span<const ProgramEntryId> order);
[[nodiscard]] bool DecodeProgramIndex(
    std::string_view text,
    std::vector<ProgramEntryId>& order,
    std::string& error);

[[nodiscard]] bool ValidDisplayName(std::string_view name) noexcept;
[[nodiscard]] bool ValidRunConfiguration(
    const RunConfiguration& configuration) noexcept;

} // namespace inputweaver::app

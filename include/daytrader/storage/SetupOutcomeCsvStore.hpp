#pragma once

#include "daytrader/domain/SetupCalibration.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace daytrader::storage {

class SetupOutcomeCsvStore {
public:
    explicit SetupOutcomeCsvStore(std::filesystem::path path);

    [[nodiscard]] std::vector<domain::SetupOutcomeRecord> load() const;
    void save(std::span<const domain::SetupOutcomeRecord> records) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
};

} // namespace daytrader::storage

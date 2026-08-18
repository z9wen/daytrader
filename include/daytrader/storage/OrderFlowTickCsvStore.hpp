#pragma once

#include "daytrader/domain/OrderFlow.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace daytrader::storage {

// Stores the two raw event streams separately. Reclassification can therefore
// evolve without re-downloading the same constrained IBKR tick sample.
class OrderFlowTickCsvStore {
public:
    explicit OrderFlowTickCsvStore(std::filesystem::path directory);

    [[nodiscard]] std::optional<domain::OrderFlowTicks> load(
        const std::string& symbol,
        std::int64_t end_timestamp
    ) const;

    void save(const domain::OrderFlowTicks& ticks) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    [[nodiscard]] std::filesystem::path path_for(
        const std::string& symbol,
        std::int64_t end_timestamp,
        const char* stream
    ) const;

    std::filesystem::path directory_;
};

} // namespace daytrader::storage

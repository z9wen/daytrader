#pragma once

#include "daytrader/domain/MarketScan.hpp"

#include <string>

namespace daytrader::presentation {

// Logical pages rendered by the interactive terminal dashboard.
enum class DashboardTab {
    market,
    sectors,
    industries,
};

// Pure text renderer: formatting is testable without terminal control sequences.
class ConsoleScanPrinter {
public:
    explicit ConsoleScanPrinter(std::string time_zone);

    [[nodiscard]] std::string render(
        const domain::MarketScan& scan,
        DashboardTab tab
    ) const;
    // Non-interactive fallback used when stdout is redirected instead of attached to a TTY.
    [[nodiscard]] std::string render_all(const domain::MarketScan& scan) const;

private:
    std::string time_zone_;
};

} // namespace daytrader::presentation

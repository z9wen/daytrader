#pragma once

#include "daytrader/domain/MarketScan.hpp"

#include <cstddef>
#include <string>

namespace daytrader::presentation {

// Logical pages rendered by the interactive terminal dashboard.
enum class DashboardTab {
    market,
    sectors,
    industries,
};

// Interactive rendering constraints. Page numbers are zero-based internally;
// the terminal UI presents them as one-based values.
struct DashboardViewport {
    std::size_t columns{120};
    std::size_t rows{24};
    std::size_t requested_page{};
};

struct DashboardPage {
    std::string text;
    std::size_t page_index{};
    std::size_t page_count{1};
};

// Pure text renderer: formatting is testable without terminal control sequences.
class ConsoleScanPrinter {
public:
    explicit ConsoleScanPrinter(std::string time_zone);

    [[nodiscard]] std::string render(
        const domain::MarketScan& scan,
        DashboardTab tab
    ) const;
    // Responsive TTY renderer: selects a column set for the available width and
    // paginates rotation rows to fit the available height.
    [[nodiscard]] DashboardPage render_page(
        const domain::MarketScan& scan,
        DashboardTab tab,
        DashboardViewport viewport
    ) const;
    // Non-interactive fallback used when stdout is redirected instead of attached to a TTY.
    [[nodiscard]] std::string render_all(const domain::MarketScan& scan) const;

private:
    std::string time_zone_;
};

} // namespace daytrader::presentation

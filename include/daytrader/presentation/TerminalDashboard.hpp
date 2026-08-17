#pragma once

#include "daytrader/domain/MarketScan.hpp"

#include <memory>
#include <string>

namespace daytrader::presentation {

// Alternate-screen TUI that redraws one cached scan and captures Tab/1/2/3 keys.
// Terminal state is restored by stop() and by the destructor on every exit path.
class TerminalDashboard {
public:
    explicit TerminalDashboard(std::string time_zone);
    ~TerminalDashboard();

    TerminalDashboard(const TerminalDashboard&) = delete;
    TerminalDashboard& operator=(const TerminalDashboard&) = delete;
    TerminalDashboard(TerminalDashboard&&) noexcept;
    TerminalDashboard& operator=(TerminalDashboard&&) noexcept;

    void start();
    // Stores a complete scan so switching tabs never triggers new IBKR requests.
    void update(const domain::MarketScan& scan);
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace daytrader::presentation

#pragma once

#include <optional>

namespace daytrader::presentation {

// Logical terminal actions decoded from raw-mode key bytes. Keeping escape
// sequence parsing outside the dashboard makes split read() calls testable.
enum class TerminalAction {
    next_tab,
    previous_tab,
    market_tab,
    sectors_tab,
    industries_tab,
    leveraged_tab,
    trade_tab,
    previous_page,
    next_page,
};

class TerminalKeyDecoder {
public:
    [[nodiscard]] std::optional<TerminalAction> consume(char character);

private:
    enum class State {
        plain,
        escape,
        control_sequence,
    };

    [[nodiscard]] std::optional<TerminalAction> decode_plain(char character);

    State state_{State::plain};
};

} // namespace daytrader::presentation

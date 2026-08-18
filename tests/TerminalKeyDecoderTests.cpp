#include "daytrader/presentation/TerminalKeyDecoder.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using daytrader::presentation::TerminalAction;
using daytrader::presentation::TerminalKeyDecoder;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_action(
    const std::optional<TerminalAction>& actual,
    TerminalAction expected,
    const std::string& message
)
{
    require(actual.has_value() && *actual == expected, message);
}

void decodes_direct_and_page_keys()
{
    TerminalKeyDecoder decoder;
    require_action(decoder.consume('\t'), TerminalAction::next_tab,
                   "Tab should select the next dashboard tab");
    require_action(decoder.consume('4'), TerminalAction::leveraged_tab,
                   "4 should select the leveraged tab");
    require_action(decoder.consume('5'), TerminalAction::trade_tab,
                   "5 should select the trade tab");
    require_action(decoder.consume('['), TerminalAction::previous_page,
                   "[ should select the previous table page");
    require_action(decoder.consume(']'), TerminalAction::next_page,
                   "] should select the next table page");
}

void decodes_split_arrow_sequences()
{
    TerminalKeyDecoder decoder;
    require(!decoder.consume('\x1b').has_value(),
            "the first escape byte should wait for the rest of the sequence");
    require(!decoder.consume('[').has_value(),
            "the CSI prefix should wait for its final byte");
    require_action(decoder.consume('C'), TerminalAction::next_tab,
                   "right arrow should select the next tab");

    require(!decoder.consume('\x1b').has_value(),
            "left arrow escape should be accepted");
    require(!decoder.consume('[').has_value(),
            "left arrow CSI prefix should be accepted");
    require_action(decoder.consume('D'), TerminalAction::previous_tab,
                   "left arrow should select the previous tab");
}

void decodes_modified_arrows_without_triggering_number_tabs()
{
    TerminalKeyDecoder decoder;
    for (const char character : std::string{"\x1b[1;5"}) {
        require(!decoder.consume(character).has_value(),
                "an incomplete modified arrow should not emit an action");
    }
    require_action(decoder.consume('C'), TerminalAction::next_tab,
                   "a modified right arrow should emit one navigation action");
}

} // namespace

int main()
{
    try {
        decodes_direct_and_page_keys();
        decodes_split_arrow_sequences();
        decodes_modified_arrows_without_triggering_number_tabs();
        std::cout << "TerminalKeyDecoderTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "TerminalKeyDecoderTests failed: " << exception.what() << '\n';
        return 1;
    }
}

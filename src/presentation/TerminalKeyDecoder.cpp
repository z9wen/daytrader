#include "daytrader/presentation/TerminalKeyDecoder.hpp"

namespace daytrader::presentation {

std::optional<TerminalAction> TerminalKeyDecoder::consume(char character)
{
    switch (state_) {
    case State::plain:
        if (character == '\x1b') {
            state_ = State::escape;
            return std::nullopt;
        }
        return decode_plain(character);

    case State::escape:
        state_ = State::plain;
        if (character == '[') {
            state_ = State::control_sequence;
            return std::nullopt;
        }
        // A standalone Escape followed by a normal key must not swallow that
        // key, so decode the second byte as regular input.
        return decode_plain(character);

    case State::control_sequence:
        // CSI sequences terminate with a byte in the range 0x40-0x7e. This
        // also accepts modified arrows such as ESC [ 1 ; 5 C.
        if (character < '@' || character > '~') {
            return std::nullopt;
        }
        state_ = State::plain;
        if (character == 'C') {
            return TerminalAction::next_tab;
        }
        if (character == 'D') {
            return TerminalAction::previous_tab;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<TerminalAction> TerminalKeyDecoder::decode_plain(char character)
{
    switch (character) {
    case '\t':
        return TerminalAction::next_tab;
    case '1':
        return TerminalAction::market_tab;
    case '2':
        return TerminalAction::sectors_tab;
    case '3':
        return TerminalAction::industries_tab;
    case '4':
        return TerminalAction::leveraged_tab;
    case '5':
        return TerminalAction::trade_tab;
    case '[':
        return TerminalAction::previous_page;
    case ']':
        return TerminalAction::next_page;
    default:
        return std::nullopt;
    }
}

} // namespace daytrader::presentation

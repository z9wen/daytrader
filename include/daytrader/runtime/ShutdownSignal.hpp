#pragma once

namespace daytrader::runtime {

// Async-signal-safe SIGINT/SIGTERM bridge queried by normal application code.
class ShutdownSignal {
public:
    static void install();
    [[nodiscard]] static bool requested() noexcept;
};

} // namespace daytrader::runtime

#pragma once

namespace daytrader::app {

// Top-level composition root kept separate from the minimal main.cpp entry point.
class Application {
public:
    [[nodiscard]] int run() const;
};

} // namespace daytrader::app

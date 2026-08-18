#pragma once

namespace daytrader::app {

// Top-level composition root kept separate from the minimal main.cpp entry point.
class Application {
public:
    [[nodiscard]] int run(int argc, char* argv[]) const;
};

} // namespace daytrader::app

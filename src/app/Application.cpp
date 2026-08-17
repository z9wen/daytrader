#include "daytrader/app/Application.hpp"

#include "daytrader/config/AppConfig.hpp"
#include "daytrader/monitoring/MarketMonitor.hpp"
#include "daytrader/runtime/ShutdownSignal.hpp"

#include <exception>
#include <iostream>

namespace daytrader::app {

int Application::run() const
{
    try {
        const auto config = config::AppConfig::from_environment();
        runtime::ShutdownSignal::install();
        monitoring::MarketMonitor{config}.run([] {
            return runtime::ShutdownSignal::requested();
        });
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "daytrader: " << exception.what() << '\n';
        return 1;
    }
}

} // namespace daytrader::app

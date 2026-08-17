#include "daytrader/runtime/ShutdownSignal.hpp"

#include <csignal>
#include <stdexcept>

namespace daytrader::runtime {
namespace {

volatile std::sig_atomic_t shutdown_requested{};

void handle_shutdown_signal(int)
{
    shutdown_requested = 1;
}

} // namespace

void ShutdownSignal::install()
{
    shutdown_requested = 0;
    if (std::signal(SIGINT, handle_shutdown_signal) == SIG_ERR
        || std::signal(SIGTERM, handle_shutdown_signal) == SIG_ERR) {
        throw std::runtime_error("unable to install shutdown signal handlers");
    }
}

bool ShutdownSignal::requested() noexcept
{
    return shutdown_requested != 0;
}

} // namespace daytrader::runtime

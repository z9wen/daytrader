#include "daytrader/presentation/TerminalDashboard.hpp"

#include "daytrader/presentation/ConsoleScanPrinter.hpp"

#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#if defined(__APPLE__) || defined(__unix__)
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace daytrader::presentation {
namespace {

struct TerminalSize {
    std::size_t columns{120};
    std::size_t rows{30};

    bool operator==(const TerminalSize&) const = default;
};

[[nodiscard]] TerminalSize terminal_size()
{
    TerminalSize result;
#if defined(__APPLE__) || defined(__unix__)
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_col > 0) {
            result.columns = size.ws_col;
        }
        if (size.ws_row > 0) {
            result.rows = size.ws_row;
        }
    }
#endif
    return result;
}

[[nodiscard]] DashboardTab next_tab(DashboardTab tab)
{
    switch (tab) {
    case DashboardTab::market:
        return DashboardTab::sectors;
    case DashboardTab::sectors:
        return DashboardTab::industries;
    case DashboardTab::industries:
        return DashboardTab::market;
    }
    return DashboardTab::market;
}

[[nodiscard]] std::string tab_label(
    DashboardTab active,
    DashboardTab tab,
    std::string label
)
{
    if (active == tab) {
        return "\x1b[7m" + std::move(label) + "\x1b[0m";
    }
    return label;
}

[[nodiscard]] std::string dashboard_header(
    DashboardTab active,
    const TerminalSize& size,
    std::size_t page_index,
    std::size_t page_count
)
{
    std::ostringstream output;
    if (size.columns >= 92) {
        output << "DAYTRADER  "
               << tab_label(active, DashboardTab::market, "[1 MARKET]") << ' '
               << tab_label(active, DashboardTab::sectors, "[2 SECTORS]") << ' '
               << tab_label(active, DashboardTab::industries, "[3 INDUSTRIES]")
               << "  Tab switch";
    } else {
        output << "DAYTRADER "
               << tab_label(active, DashboardTab::market, "[1 MKT]") << ' '
               << tab_label(active, DashboardTab::sectors, "[2 SEC]") << ' '
               << tab_label(active, DashboardTab::industries, "[3 IND]")
               << " Tab";
    }
    if (page_count > 1) {
        output << " | [/] page " << page_index + 1 << '/' << page_count;
    }
    output << " | Ctrl+C";
    return output.str();
}

} // namespace

class TerminalDashboard::Impl {
public:
    explicit Impl(std::string time_zone)
        : printer_{std::move(time_zone)}
    {
    }

    ~Impl()
    {
        stop();
    }

    void start()
    {
        if (started_.exchange(true)) {
            return;
        }

#if defined(__APPLE__) || defined(__unix__)
        interactive_ = ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
        if (interactive_ && ::tcgetattr(STDIN_FILENO, &original_terminal_) == 0) {
            auto interactive_terminal = original_terminal_;
            interactive_terminal.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            interactive_terminal.c_cc[VMIN] = 0;
            interactive_terminal.c_cc[VTIME] = 0;
            if (::tcsetattr(STDIN_FILENO, TCSANOW, &interactive_terminal) == 0) {
                terminal_configured_ = true;
                // Alternate screen, hidden cursor, and disabled autowrap are all
                // restored in stop(). Responsive rows should already fit, while
                // disabled autowrap is the final guard against accidental wraps.
                std::cout << "\x1b[?1049h\x1b[?25l\x1b[?7l" << std::flush;
                input_thread_ = std::jthread{
                    [this](std::stop_token stop_token) { input_loop(stop_token); }
                };
            } else {
                interactive_ = false;
            }
        } else {
            interactive_ = false;
        }
#endif
    }

    void update(const domain::MarketScan& scan)
    {
        std::lock_guard lock{mutex_};
        latest_scan_ = scan;
        render_locked();
    }

    void stop()
    {
        if (!started_.exchange(false)) {
            return;
        }

        if (input_thread_.joinable()) {
            input_thread_.request_stop();
            input_thread_.join();
        }

#if defined(__APPLE__) || defined(__unix__)
        if (terminal_configured_) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal_);
            terminal_configured_ = false;
            std::cout << "\x1b[?7h\x1b[?25h\x1b[?1049l" << std::flush;
        }
#endif
    }

private:
    void select_tab(DashboardTab tab)
    {
        std::lock_guard lock{mutex_};
        if (active_tab_ == tab) {
            return;
        }
        active_tab_ = tab;
        active_page_ = 0;
        render_locked();
    }

    void change_page(int direction)
    {
        std::lock_guard lock{mutex_};
        if (direction < 0 && active_page_ > 0) {
            --active_page_;
        } else if (direction > 0 && active_page_ + 1 < page_count_) {
            ++active_page_;
        } else {
            return;
        }
        render_locked();
    }

    void render_locked()
    {
        if (!latest_scan_.has_value()) {
            return;
        }
        if (!interactive_) {
            std::cout << printer_.render_all(*latest_scan_) << std::flush;
            return;
        }

        const auto size = terminal_size();
        const auto page = printer_.render_page(
            *latest_scan_,
            active_tab_,
            DashboardViewport{
                .columns = size.columns,
                // Keep one row for the dashboard header and one spare row so a
                // trailing newline cannot scroll the alternate screen.
                .rows = size.rows > 2 ? size.rows - 2 : 5,
                .requested_page = active_page_,
            }
        );
        active_page_ = page.page_index;
        page_count_ = page.page_count;
        last_terminal_size_ = size;

        std::cout << "\x1b[2J\x1b[H"
                  << dashboard_header(
                         active_tab_,
                         size,
                         active_page_,
                         page_count_
                     )
                  << '\n'
                  << page.text
                  << std::flush;
    }

    void render_if_resized()
    {
        const auto size = terminal_size();
        std::lock_guard lock{mutex_};
        if (interactive_ && latest_scan_.has_value()
            && size != last_terminal_size_) {
            render_locked();
        }
    }

#if defined(__APPLE__) || defined(__unix__)
    void input_loop(std::stop_token stop_token)
    {
        pollfd input{
            .fd = STDIN_FILENO,
            .events = POLLIN,
            .revents = 0,
        };
        while (!stop_token.stop_requested()) {
            const int result = ::poll(&input, 1, 100);
            if (result == 0) {
                render_if_resized();
                continue;
            }
            if (result < 0 || (input.revents & POLLIN) == 0) {
                continue;
            }

            char characters[16];
            const auto count = ::read(STDIN_FILENO, characters, sizeof(characters));
            if (count <= 0) {
                continue;
            }
            for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
                switch (characters[index]) {
                case '\t': {
                    DashboardTab next;
                    {
                        std::lock_guard lock{mutex_};
                        next = next_tab(active_tab_);
                    }
                    select_tab(next);
                    break;
                }
                case '1':
                    select_tab(DashboardTab::market);
                    break;
                case '2':
                    select_tab(DashboardTab::sectors);
                    break;
                case '3':
                    select_tab(DashboardTab::industries);
                    break;
                case '[':
                    change_page(-1);
                    break;
                case ']':
                    change_page(1);
                    break;
                default:
                    break;
                }
            }
        }
    }
#endif

    ConsoleScanPrinter printer_;
    std::mutex mutex_;
    std::optional<domain::MarketScan> latest_scan_;
    DashboardTab active_tab_{DashboardTab::market};
    std::size_t active_page_{};
    std::size_t page_count_{1};
    TerminalSize last_terminal_size_{};
    std::atomic_bool started_{false};
    bool interactive_{};
    std::jthread input_thread_;
#if defined(__APPLE__) || defined(__unix__)
    termios original_terminal_{};
    bool terminal_configured_{};
#endif
};

TerminalDashboard::TerminalDashboard(std::string time_zone)
    : impl_{std::make_unique<Impl>(std::move(time_zone))}
{
}

TerminalDashboard::~TerminalDashboard() = default;
TerminalDashboard::TerminalDashboard(TerminalDashboard&&) noexcept = default;
TerminalDashboard& TerminalDashboard::operator=(TerminalDashboard&&) noexcept = default;

void TerminalDashboard::start()
{
    impl_->start();
}

void TerminalDashboard::update(const domain::MarketScan& scan)
{
    impl_->update(scan);
}

void TerminalDashboard::stop()
{
    impl_->stop();
}

} // namespace daytrader::presentation

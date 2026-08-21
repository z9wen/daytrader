#pragma once

#include <string>
#include <vector>

namespace daytrader::domain {

// One actual exchange session returned by IBKR whatToShow=SCHEDULE.
// market_date is normalized to ISO YYYY-MM-DD while the start/end strings and
// time zone retain IBKR's contract-specific schedule information.
struct TradingSession {
    std::string market_date;
    std::string start_datetime;
    std::string end_datetime;
    std::string time_zone;
};

struct TradingSchedule {
    std::string symbol;
    std::vector<TradingSession> sessions;
};

} // namespace daytrader::domain

# daytrader

A C++20 terminal dashboard for discretionary ETF day trading with the official
Interactive Brokers TWS API. It continuously evaluates market regime, sector
and industry rotation, VWAP entry zones, and long-position management guidance.

The application is decision support only: it does not submit orders.

## Current workflow

The signal ETF determines direction and the leveraged ETF supplies the price
used for manual execution. The first researched pair is `SOXX -> SOXL`; `QQQ`
and `TQQQ` data are also cached for the next strategy.

Each rotation row separates trend from timing:

- `phase`: `BUILDING`, `STRONG`, `NEUTRAL`, `FADING`, or `WEAK`
- `score`: transparent 0-100 bullish-structure score, not a calibrated probability
- `entry`: `WATCH`, `WAIT_VWAP`, `READY`, or `AVOID`
- `if held`: `HOLD`, `PROTECT`, `TRIM`, or `EXIT`
- independent VWAP/ATR entry zones for the signal and leveraged ETFs
- time-of-day Relative Volume (`RVOL`) against the median of up to 20 prior sessions
- VWAP structure (`RECLAIM`, `RISING`, `ABOVE`, `BELOW`, or `LOST`)
- 15/30/60-minute relative strength against both SPY (`S`) and QQQ (`Q`)

Sector and industry phases remain instrument-specific. The SPY/QQQ market
regime contributes to the score as risk context, but a bearish market does not
force an independently strong industry to `WEAK` or every held position to
`EXIT`.

The terminal has four pages: market context (`SPY`, `QQQ`, and optional `VIX`),
the eleven standard sector ETFs, twenty industry ETFs, and a live trade page.
The trade page filters the IBKR account to positions in the configured signal
and long-leveraged ETF universe, so long-term bond or income holdings do not
appear as day trades. It shows average cost, mark, unrealized P&L,
process-observed peak MFE and profit giveback, plus rolling
30/60-second DeltaRatio for QQQ and SOXX, ATR-normalized pressure state, and
evidence quality. Press `Tab` or `1`/`2`/`3`/`4` to switch pages. Interactive
tables automatically select a regular, compact, or minimal column set from the
current terminal width. Rotation rows
are separated into clearly labeled `STRONG`, `NEUTRAL`, and `WEAK` sections and
paginated to the available height; press `[` or `]` to change page. A group that
continues on another page repeats its label and column header. Resizing the
terminal triggers a responsive redraw without requesting new market data.

## Prerequisites

- A running TWS or IB Gateway with socket clients enabled.
- The official Mac/Unix TWS API extracted to `$HOME/IBJts`.
- `protobuf@21`, installed with `brew install protobuf@21`.
- Intel Decimal Floating-Point `libbid.a` at
  `$HOME/IBJts/source/cppclient/client/lib/libbid.a`.
- Appropriate IBKR API market-data subscriptions for the requested US ETFs.

The SDK location can be changed with `-DIBKR_TWS_API_ROOT=/path/to/IBJts`, and
the decimal library with `-DIBKR_LIBBID_PATH=/path/to/libbid.a`. Neither
dependency is committed to this repository.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The first build regenerates and compiles the protocol-buffer sources supplied
by IBKR, so it takes longer than subsequent builds.

## Continuous monitor

The defaults connect to TWS at `127.0.0.1:9972` with client ID `7`, use regular
trading hours and five-minute `TRADES` bars, and display times in
`America/New_York`. A second read-only connection uses client ID `8` for
positions/P&L and four tick-by-tick streams (QQQ/SOXX Last + BidAsk). The
monitor requests only the latest two days for a fast startup, merges them with
the CSV cache, and then consumes incremental updates. Time-of-day RVOL uses the
median of up to 20 cached prior sessions no older than 45 calendar days; it
remains unavailable until at least three comparable sessions have accumulated.

```sh
./build/daytrader
```

The bar analysis refreshes after each synchronized completed bar; live P&L and
Order Flow redraw at roughly one-second cadence. Inverse ETFs are
shown only as bearish references; they are not subscribed or proposed as live
short trades.

## Historical backtest and local data

Run the fixed SOXX-to-SOXL intraday strategy over approximately 240 calendar
days:

```sh
./build/daytrader backtest 240
```

Historical RTH five-minute bars are stored as one CSV per symbol under
`data/ibkr/rth_5m/`:

- `SPY.csv`
- `QQQ.csv`
- `TQQQ.csv`
- `SOXX.csv`
- `SOXL.csv`

The cache is ignored by Git. A complete recent cache avoids TWS requests;
otherwise the program refreshes only recent bars or downloads history only for
missing symbols. Every successful IBKR batch is persisted immediately.

The backtest uses completed five-minute signals, fills at the next bar open,
limits entry to the morning window, allows one trade per session, estimates
transaction costs, and reports win rate, average return, profit factor, maximum
drawdown, holding time, and recent trades. Results are exploratory and do not
promise future performance.

For the first Order Flow experiment, run a 30-calendar-day baseline and fetch
bounded SOXX time-and-sales only around its candidate entries:

```sh
./build/daytrader orderflow-backtest 30
```

Use `ytd` to derive the calendar window from January 1 in the configured New
York time zone. Successful candidate windows are persisted immediately, so an
interrupted or pacing-limited run resumes from the existing cache:

```sh
./build/daytrader orderflow-backtest ytd
```

The classifier combines historical `TRADES` and `BID_ASK`, reports 30-second,
one-minute, and five-minute DeltaRatio, and distinguishes effective pressure
from absorption using the price response in SOXX ATR14 units. `ATRx` is the
completed-bar ATR5/ATR14 ratio, while the signed flow score combines 30-second
and one-minute Delta, Delta acceleration, and ATR response. The score is a
diagnostic, not a probability or an active entry gate. Evidence quality remains
separate and uses classification coverage, direct quote-test coverage, window
completeness, and trade depth. Raw event samples are cached under
`data/ibkr/order_flow_ticks/`, so changes to the classifier can be replayed
without another IBKR request. An asterisk marks a horizon that the bounded
sample did not fully cover.

## Runtime configuration

Supported environment variables:

- `DAYTRADER_IBKR_HOST`
- `DAYTRADER_IBKR_PORT`
- `DAYTRADER_IBKR_CLIENT_ID`
- `DAYTRADER_LIVE_CLIENT_ID`
- `DAYTRADER_REQUEST_TIMEOUT_SECONDS`
- `DAYTRADER_BAR_INTERVAL_SECONDS`
- `DAYTRADER_RECONNECT_DELAY_SECONDS`
- `DAYTRADER_MONITOR_DURATION`
- `DAYTRADER_MONITOR_MAX_BARS`
- `DAYTRADER_MONITOR_TIMEOUT_SECONDS`
- `DAYTRADER_DATA_TYPE`
- `DAYTRADER_DURATION`
- `DAYTRADER_BAR_SIZE`
- `DAYTRADER_HISTORICAL_DELAY_MINUTES`
- `DAYTRADER_TIME_ZONE`
- `DAYTRADER_DATA_DIR`

Example with a separate cache location:

```sh
DAYTRADER_DATA_DIR=/path/to/daytrader-data ./build/daytrader backtest 240
```

Strict compiler warnings are enabled by default and can be disabled with
`-DDAYTRADER_ENABLE_WARNINGS=OFF`.

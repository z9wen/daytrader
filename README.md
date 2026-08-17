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

The terminal has three pages: market context (`SPY`, `QQQ`, and optional `VIX`),
the eleven standard sector ETFs, and twenty industry ETFs. Press `Tab` or
`1`/`2`/`3` to switch pages.

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
`America/New_York`.

```sh
./build/daytrader
```

The dashboard refreshes after each synchronized completed bar. Inverse ETFs are
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

## Runtime configuration

Supported environment variables:

- `DAYTRADER_IBKR_HOST`
- `DAYTRADER_IBKR_PORT`
- `DAYTRADER_IBKR_CLIENT_ID`
- `DAYTRADER_REQUEST_TIMEOUT_SECONDS`
- `DAYTRADER_BAR_INTERVAL_SECONDS`
- `DAYTRADER_RECONNECT_DELAY_SECONDS`
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

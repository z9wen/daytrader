cmake_minimum_required(VERSION 3.20)

project(
    daytrader
    VERSION 0.1.0
    DESCRIPTION "Day-trading long/short signal analysis"
    LANGUAGES CXX
)

option(DAYTRADER_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

set(IBKR_TWS_API_ROOT "" CACHE PATH "Path to the extracted IBKR TWS API SDK")
if(NOT IBKR_TWS_API_ROOT AND DEFINED ENV{IBKR_TWS_API_ROOT})
    set(IBKR_TWS_API_ROOT "$ENV{IBKR_TWS_API_ROOT}")
endif()
if(NOT IBKR_TWS_API_ROOT AND EXISTS "$ENV{HOME}/IBJts")
    set(IBKR_TWS_API_ROOT "$ENV{HOME}/IBJts")
endif()

if(NOT EXISTS "${IBKR_TWS_API_ROOT}/source/cppclient/client/EClientSocket.h")
    message(FATAL_ERROR
        "IBKR TWS API SDK was not found. Set IBKR_TWS_API_ROOT to the extracted IBJts directory."
    )
endif()

set(IBKR_CLIENT_DIR "${IBKR_TWS_API_ROOT}/source/cppclient/client")
set(IBKR_PROTO_DIR "${IBKR_TWS_API_ROOT}/source/proto")
set(
    IBKR_LIBBID_PATH
    "${IBKR_CLIENT_DIR}/lib/libbid.a"
    CACHE FILEPATH
    "Path to Intel Decimal Floating-Point libbid"
)

if(NOT EXISTS "${IBKR_LIBBID_PATH}")
    message(FATAL_ERROR
        "Intel Decimal libbid was not found at ${IBKR_LIBBID_PATH}. "
        "Build it as described by IBKR, or set IBKR_LIBBID_PATH."
    )
endif()

if(APPLE AND EXISTS "/opt/homebrew/opt/protobuf@21")
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/protobuf@21")
endif()

find_package(Protobuf CONFIG REQUIRED)
find_package(Threads REQUIRED)

file(GLOB IBKR_CLIENT_SOURCES CONFIGURE_DEPENDS "${IBKR_CLIENT_DIR}/*.cpp")
file(GLOB IBKR_PROTO_FILES CONFIGURE_DEPENDS "${IBKR_PROTO_DIR}/*.proto")

set(IBKR_PROTO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/ibkr")
file(MAKE_DIRECTORY "${IBKR_PROTO_GENERATED_DIR}")
protobuf_generate(
    LANGUAGE cpp
    OUT_VAR IBKR_PROTO_GENERATED
    PROTOS ${IBKR_PROTO_FILES}
    IMPORT_DIRS "${IBKR_PROTO_DIR}"
    PROTOC_OUT_DIR "${IBKR_PROTO_GENERATED_DIR}"
)

add_library(ibkr_tws_api STATIC
    ${IBKR_CLIENT_SOURCES}
    ${IBKR_PROTO_GENERATED}
)
target_compile_features(ibkr_tws_api PRIVATE cxx_std_17)
target_include_directories(ibkr_tws_api PUBLIC
    "${IBKR_CLIENT_DIR}"
    "${IBKR_PROTO_GENERATED_DIR}"
)
target_link_libraries(ibkr_tws_api PUBLIC
    protobuf::libprotobuf
    Threads::Threads
    "${IBKR_LIBBID_PATH}"
)
set_target_properties(ibkr_tws_api PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(ibkr_tws_api PRIVATE /W0)
else()
    target_compile_options(ibkr_tws_api PRIVATE -w)
endif()

add_library(daytrader_core
    src/analysis/EntryZoneCalculator.cpp
    src/analysis/EtfSnapshotCalculator.cpp
    src/analysis/LongOpportunityAnalyzer.cpp
    src/analysis/LiveTradeContextEnricher.cpp
    src/analysis/LeveragedExecutionAnalyzer.cpp
    src/analysis/MarketRegimeAnalyzer.cpp
    src/analysis/MarketScanner.cpp
    src/analysis/OrderFlowAggregator.cpp
    src/analysis/OrderFlowSignalAnalyzer.cpp
    src/analysis/OrderFlowWindowAnalyzer.cpp
    src/analysis/RelativeStrengthAnalyzer.cpp
    src/analysis/RelativeStrengthRanker.cpp
    src/analysis/RelativeVolumeAnalyzer.cpp
    src/analysis/RotationGrouper.cpp
    src/analysis/TradeClassifier.cpp
    src/analysis/VixAnalyzer.cpp
    src/analysis/VwapStructureAnalyzer.cpp
    src/app/Application.cpp
    src/backtest/DayTradeBacktester.cpp
    src/backtest/IbkrBacktestRunner.cpp
    src/backtest/OrderFlowBacktestRunner.cpp
    src/config/AppConfig.cpp
    src/ibkr/HistoricalRequestPacer.cpp
    src/ibkr/TwsHistoricalTickClient.cpp
    src/ibkr/TwsLiveContextClient.cpp
    src/ibkr/TwsMarketDataClient.cpp
    src/indicators/TechnicalIndicators.cpp
    src/live/LiveOrderFlowTracker.cpp
    src/live/LiveTradeContextStore.cpp
    src/live/PositionTracker.cpp
    src/market_data/BarSeriesAligner.cpp
    src/market_data/CompletedBarSynchronizer.cpp
    src/market_data/InstrumentBarsLookup.cpp
    src/market_data/InstrumentBarsMerger.cpp
    src/monitoring/MarketMonitor.cpp
    src/presentation/ConsoleScanPrinter.cpp
    src/presentation/BacktestReportPrinter.cpp
    src/presentation/OrderFlowBacktestPrinter.cpp
    src/presentation/TerminalDashboard.cpp
    src/presentation/TerminalKeyDecoder.cpp
    src/runtime/ShutdownSignal.cpp
    src/storage/MarketDataCsvStore.cpp
    src/storage/OrderFlowTickCsvStore.cpp
    src/strategy/LeveragedEtfSelector.cpp
    src/time/TimeZoneFormatter.cpp
    src/universe/EtfUniverse.cpp
)
target_compile_features(daytrader_core PUBLIC cxx_std_20)
target_include_directories(daytrader_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(daytrader_core PRIVATE ibkr_tws_api)
set_target_properties(daytrader_core PROPERTIES CXX_EXTENSIONS OFF)

add_executable(daytrader src/main.cpp)
target_compile_features(daytrader PRIVATE cxx_std_20)
target_link_libraries(daytrader PRIVATE daytrader_core)
set_target_properties(daytrader PROPERTIES CXX_EXTENSIONS OFF)

if(DAYTRADER_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(daytrader_core PRIVATE /W4 /permissive-)
        target_compile_options(daytrader PRIVATE /W4 /permissive-)
    else()
        target_compile_options(daytrader_core PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(daytrader PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    function(daytrader_add_test target source)
        add_executable(${target} ${source})
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_link_libraries(${target} PRIVATE daytrader_core)
        set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
        if(DAYTRADER_ENABLE_WARNINGS)
            if(MSVC)
                target_compile_options(${target} PRIVATE /W4 /permissive-)
            else()
                target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
            endif()
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    daytrader_add_test(daytrader_market_scanner_tests tests/MarketScannerTests.cpp)
    daytrader_add_test(daytrader_technical_indicators_tests tests/TechnicalIndicatorsTests.cpp)
    daytrader_add_test(daytrader_market_data_tests tests/MarketDataTests.cpp)
    daytrader_add_test(daytrader_etf_universe_tests tests/EtfUniverseTests.cpp)
    daytrader_add_test(daytrader_industry_rotation_tests tests/IndustryRotationTests.cpp)
    daytrader_add_test(daytrader_entry_zone_tests tests/EntryZoneTests.cpp)
    daytrader_add_test(daytrader_console_printer_tests tests/ConsoleScanPrinterTests.cpp)
    daytrader_add_test(daytrader_long_opportunity_tests tests/LongOpportunityTests.cpp)
    daytrader_add_test(daytrader_backtester_tests tests/DayTradeBacktesterTests.cpp)
    daytrader_add_test(daytrader_market_data_store_tests tests/MarketDataCsvStoreTests.cpp)
    daytrader_add_test(daytrader_order_flow_tests tests/OrderFlowTests.cpp)
    daytrader_add_test(daytrader_live_trade_context_tests tests/LiveTradeContextTests.cpp)
    daytrader_add_test(daytrader_terminal_key_decoder_tests tests/TerminalKeyDecoderTests.cpp)
    daytrader_add_test(daytrader_leveraged_execution_tests tests/LeveragedExecutionTests.cpp)
endif()

#!/bin/zsh

# Read-only terminal view for the durable IBKR one-minute cache. The optional
# first argument controls the refresh interval in seconds.
set -u

readonly script_directory="${0:A:h}"
readonly project_directory="${script_directory:h}"
readonly cache_directory="${DAYTRADER_MINUTE_DATA_DIR:-${project_directory}/data/ibkr/all_1m}"
readonly refresh_seconds="${1:-5}"

if [[ ! "${refresh_seconds}" =~ '^[1-9][0-9]*$' ]]; then
    print -u2 "refresh interval must be a positive integer"
    exit 2
fi

while true; do
    clear
    print "DAYTRADER ONE-MINUTE CACHE"
    print "Updated: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    print "Directory: ${cache_directory}"
    print

    readonly_processes="$(pgrep -fl 'daytrader cache-history' 2>/dev/null)"
    if [[ -n "${readonly_processes}" ]]; then
        print "PROCESS: RUNNING"
        print "${readonly_processes}"
    else
        print "PROCESS: NOT FOUND"
    fi
    print

    if [[ ! -d "${cache_directory}" ]]; then
        print "Cache directory does not exist yet."
        sleep "${refresh_seconds}"
        continue
    fi

    # Count active year partitions only. The recoverable legacy_flat/ copies
    # intentionally do not represent additional usable market-data coverage.
    csv_count="$(find "${cache_directory}" -mindepth 2 -maxdepth 2 \
        -type f -name '[12][0-9][0-9][0-9].csv' | wc -l | tr -d ' ')"
    cache_size="$(du -sh "${cache_directory}" | awk '{print $1}')"
    print "CSV files: ${csv_count}    Directory size: ${cache_size}"

    schedule_directory="${cache_directory:h}/schedules/SPY"
    schedule_files=("${schedule_directory}"/[12][0-9][0-9][0-9].csv(N))
    if (( ${#schedule_files} > 0 )); then
        schedule_sessions="$(awk 'FNR > 1 { rows += 1 } END { print rows + 0 }' "${schedule_files[@]}")"
        print "IBKR SPY schedule: ${#schedule_files} years, ${schedule_sessions} sessions"
    else
        print "IBKR SPY schedule: not cached"
    fi

    manifest="${cache_directory}/.completed_sessions.csv"
    if [[ -f "${manifest}" ]]; then
        marker_count="$(awk 'NR > 1 { count += 1 } END { print count + 0 }' "${manifest}")"
        print "Durable completion markers: ${marker_count}"
        print "Latest completed requests:"
        tail -n 8 "${manifest}"
    else
        print "Durable completion markers: 0"
    fi
    print

    print "Core one-minute rows:"
    for symbol in QQQ SPY SOXX TQQQ SOXL; do
        symbol_files=("${cache_directory}/${symbol}"/*.csv(N))
        legacy_file="${cache_directory}/${symbol}.csv"
        if (( ${#symbol_files} == 0 )) && [[ -f "${legacy_file}" ]]; then
            symbol_files=("${legacy_file}")
        fi
        if (( ${#symbol_files} > 0 )); then
            rows="$(awk 'FNR > 1 { rows += 1 } END { print rows + 0 }' "${symbol_files[@]}")"
            newest_file="$(ls -t "${symbol_files[@]}" | head -n 1)"
            modified="$(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "${newest_file}")"
            printf '  %-5s %9s rows   modified %s\n' "${symbol}" "${rows}" "${modified}"
        else
            printf '  %-5s %9s\n' "${symbol}" "not started"
        fi
    done

    print
    print "Refresh: ${refresh_seconds}s    Ctrl+C: exit viewer"
    sleep "${refresh_seconds}"
done

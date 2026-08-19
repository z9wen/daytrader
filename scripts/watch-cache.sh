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
    print "DAYTRADER YTD CACHE"
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

    csv_count="$(find "${cache_directory}" -maxdepth 1 -type f -name '*.csv' ! -name '.*' | wc -l | tr -d ' ')"
    cache_size="$(du -sh "${cache_directory}" | awk '{print $1}')"
    print "CSV files: ${csv_count}    Directory size: ${cache_size}"

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
    for symbol in QQQ SOXX TQQQ SOXL SPY; do
        symbol_file="${cache_directory}/${symbol}.csv"
        if [[ -f "${symbol_file}" ]]; then
            rows="$(awk 'END { print NR > 0 ? NR - 1 : 0 }' "${symbol_file}")"
            modified="$(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "${symbol_file}")"
            printf '  %-5s %9s rows   modified %s\n' "${symbol}" "${rows}" "${modified}"
        else
            printf '  %-5s %9s\n' "${symbol}" "not started"
        fi
    done

    print
    print "Refresh: ${refresh_seconds}s    Ctrl+C: exit viewer"
    sleep "${refresh_seconds}"
done

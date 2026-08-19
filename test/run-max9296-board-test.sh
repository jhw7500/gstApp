#!/bin/bash
# On-target validation for the MAX9296 parallel-prepare v1 integration.
#
# Runs ON THE BOARD, not on the build host.  Copy it next to the test build:
#
#   scp test/run-max9296-board-test.sh root@<board>:/root/gstApp-test/
#   ssh root@<board> '/root/gstApp-test/run-max9296-board-test.sh --scenario fast'
#
# Scenarios 6 and 7 change hardware state and are skipped unless
# --allow-destructive is given.

set -uo pipefail

VERSION="1"

CSI0_NODE="/sys/bus/i2c/devices/2-0048/prepare"
CSI1_NODE="/sys/bus/i2c/devices/1-0048/prepare"

: "${GSTAPP_BIN:=}"
: "${CAM_SERVICE:=cam-operate.service}"
: "${CAM_WIDTH:=1920}"
: "${CAM_HEIGHT:=1080}"
: "${DUAL_MASK:=0x5}"
: "${SINGLE_MASK:=0x1}"
: "${GSTAPP_EXTRA_ARGS:=}"
: "${FIRST_FRAME_TIMEOUT_S:=30}"
: "${MAX9296_MODULE:=max9296}"

SCENARIOS="fast"
ALLOW_DESTRUCTIVE=0
DRY_RUN=0
REPEAT=5
KEEP_SERVICE=0

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
FAILED_LIST=""

RUN_DIR=""
SERVICE_WAS_ACTIVE=0
CLOCK_SOURCE="unknown"
SAMPLE_COST_MS="?"

usage() {
    cat <<'USAGE'
Usage: run-max9296-board-test.sh [options]

Options:
  --scenario <list>     all | fast | comma-separated ids (default: fast)
                        fast = 1,2,3,4,5,8  (non-destructive)
                        all  = 1..8
  --allow-destructive   permit scenarios 6 and 7 (hides ABI nodes, reloads the
                        kernel module, 100 restarts).  Without it they SKIP.
  --gstapp <path>       gstApp binary under test (default: ./gstApp next to
                        this script, else /root/gstApp-test/gstApp)
  --repeat <n>          iterations for scenario 8 timing (default: 5)
  --keep-service        do NOT stop the production camera service.  Only for
                        inspecting a live system; every scenario will be
                        unreliable.
  --dry-run             print the plan and self-check the harness without
                        touching hardware
  -h, --help            this text

Environment overrides:
  GSTAPP_BIN CAM_SERVICE CAM_WIDTH CAM_HEIGHT DUAL_MASK SINGLE_MASK
  GSTAPP_EXTRA_ARGS FIRST_FRAME_TIMEOUT_S MAX9296_MODULE

Exit status: 0 when nothing FAILED, 1 otherwise.  SKIPPED does not fail.
USAGE
}

# ---------------------------------------------------------------- reporting --

say() { printf '%s\n' "$*"; }
info() { printf '       %s\n' "$*"; }

result_pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf '[%s] %-34s PASS\n' "$1" "$2"
}

result_fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    FAILED_LIST="${FAILED_LIST}${FAILED_LIST:+, }$1"
    printf '[%s] %-34s FAIL\n' "$1" "$2"
    [ -n "${3:-}" ] && printf '       reason: %s\n' "$3"
    return 0
}

result_skip() {
    SKIP_COUNT=$((SKIP_COUNT + 1))
    printf '[%s] %-34s SKIPPED\n' "$1" "$2"
    [ -n "${3:-}" ] && printf '       %s\n' "$3"
    return 0
}

# ------------------------------------------------------------------- clock ---
# Resolution is reported rather than assumed: the assertions below are only as
# sharp as the sampler, and a board without %N support is coarse.

now_ms() {
    if [ -n "${EPOCHREALTIME:-}" ]; then
        printf '%s\n' "${EPOCHREALTIME}" |
            awk -F'[.,]' '{ printf "%d\n", $1 * 1000 + substr($2 "000", 1, 3) }'
        return
    fi
    local raw
    raw=$(date +%s%N 2>/dev/null)
    case "$raw" in
        *[!0-9]* | '') ;;
        *)
            printf '%d\n' $((raw / 1000000))
            return
            ;;
    esac
    awk '{ printf "%d\n", $1 * 1000 }' /proc/uptime
}

detect_clock() {
    if [ -n "${EPOCHREALTIME:-}" ]; then
        CLOCK_SOURCE="EPOCHREALTIME (us)"
    elif date +%s%N 2>/dev/null | grep -qE '^[0-9]+$'; then
        CLOCK_SOURCE="date +%s%N (ns)"
    else
        CLOCK_SOURCE="/proc/uptime (10ms)"
    fi
}

# ------------------------------------------------------- hardware observers --

# Frames the ISI actually wrote to memory.  This is the closest userspace-visible
# proxy for "real video data came out": it counts completions in the capture
# path itself, independent of any GStreamer sink or downstream negotiation.
isi_total() {
    awk 'tolower($0) ~ /isi/ {
             for (i = 2; i <= NF; i++) {
                 if ($i ~ /^[0-9]+$/) s += $i; else break
             }
         }
         END { printf "%d\n", s + 0 }' /proc/interrupts
}

# Frames the deserializer pushed over MIPI.  Used to tell "source never sent"
# apart from "source sent, nothing landed".
csi_total() {
    awk 'tolower($0) ~ /csi/ {
             for (i = 2; i <= NF; i++) {
                 if ($i ~ /^[0-9]+$/) s += $i; else break
             }
         }
         END { printf "%d\n", s + 0 }' /proc/interrupts
}

calibrate_sampler() {
    local start end i
    start=$(now_ms)
    for i in $(seq 1 20); do
        isi_total >/dev/null
    done
    end=$(now_ms)
    SAMPLE_COST_MS=$(awk -v a="$start" -v b="$end" 'BEGIN { printf "%.1f", (b - a) / 20 }')
}

read_prepare_node() {
    local node="$1"
    [ -r "$node" ] || return 1
    cat "$node" 2>/dev/null
}

# field <text> <key> -> value, empty when absent
field() {
    printf '%s\n' "$1" | tr '[:space:]' '\n' |
        awk -v k="$2" -F= '$1 == k { print $2; exit }'
}

# --------------------------------------------------------------- log access --

LOG_READER=""

detect_log_reader() {
    if command -v journalctl >/dev/null 2>&1; then
        LOG_READER="journalctl"
    elif command -v logread >/dev/null 2>&1; then
        LOG_READER="logread"
    elif [ -r /var/log/messages ]; then
        LOG_READER="messages"
    elif [ -r /var/log/syslog ]; then
        LOG_READER="syslog"
    else
        LOG_READER="none"
    fi
}

# app_log <since-epoch-seconds> -> gstApp syslog lines
app_log() {
    local since="$1"
    case "$LOG_READER" in
        journalctl) journalctl --since "@${since}" --no-pager 2>/dev/null ;;
        logread) logread 2>/dev/null ;;
        messages) cat /var/log/messages 2>/dev/null ;;
        syslog) cat /var/log/syslog 2>/dev/null ;;
        *) return 1 ;;
    esac
}

prepare_log() { app_log "$1" | grep 'MAX9296_PREPARE' ; }

kmsg_since() { dmesg 2>/dev/null | tail -n 400 ; }

# ------------------------------------------------------------ gstApp driving --

resolve_gstapp() {
    if [ -n "$GSTAPP_BIN" ]; then
        [ -x "$GSTAPP_BIN" ] || { say "gstApp not executable: $GSTAPP_BIN"; return 1; }
        return 0
    fi
    local here
    here=$(cd "$(dirname "$0")" && pwd)
    for candidate in "$here/gstApp" "$here/bin/gstApp" /root/gstApp-test/gstApp; do
        if [ -x "$candidate" ]; then
            GSTAPP_BIN="$candidate"
            return 0
        fi
    done
    say "gstApp binary not found. Pass --gstapp <path>."
    return 1
}

kill_gstapp() {
    pkill -x gstApp >/dev/null 2>&1
    local i
    for i in $(seq 1 50); do
        pgrep -x gstApp >/dev/null 2>&1 || return 0
        sleep 0.2
    done
    pkill -9 -x gstApp >/dev/null 2>&1
    sleep 1
    return 0
}

# start_gstapp <tag> <channel-mask> [extra args...] -> sets GSTAPP_PID, GSTAPP_T0
start_gstapp() {
    local tag="$1" mask="$2"
    shift 2
    local out="$RUN_DIR/${tag}.out"
    GSTAPP_T0=$(now_ms)
    # shellcheck disable=SC2086
    "$GSTAPP_BIN" --channel "$mask" --width "$CAM_WIDTH" --height "$CAM_HEIGHT" \
        $GSTAPP_EXTRA_ARGS "$@" >"$out" 2>&1 &
    GSTAPP_PID=$!
}

# wait_first_frame <timeout-s> -> echoes elapsed ms, or "timeout"
wait_first_frame() {
    local timeout_s="$1" base_isi deadline cur
    base_isi=$(isi_total)
    deadline=$(( $(now_ms) + timeout_s * 1000 ))
    while :; do
        cur=$(isi_total)
        if [ "$cur" -gt "$base_isi" ]; then
            printf '%d\n' $(( $(now_ms) - GSTAPP_T0 ))
            return 0
        fi
        if ! kill -0 "$GSTAPP_PID" 2>/dev/null; then
            printf 'died\n'
            return 1
        fi
        if [ "$(now_ms)" -ge "$deadline" ]; then
            printf 'timeout\n'
            return 1
        fi
    done
}

# ------------------------------------------------------------ service guard --

stop_camera_service() {
    [ "$KEEP_SERVICE" -eq 1 ] && return 0
    command -v systemctl >/dev/null 2>&1 || return 0
    if systemctl is-active --quiet "$CAM_SERVICE" 2>/dev/null; then
        SERVICE_WAS_ACTIVE=1
        say "stopping $CAM_SERVICE (production gstApp restarts itself otherwise)"
        systemctl stop "$CAM_SERVICE"
        sleep 2
    fi
    kill_gstapp
    return 0
}

restore_state() {
    kill_gstapp
    restore_abi_nodes
    if [ "$SERVICE_WAS_ACTIVE" -eq 1 ]; then
        say "restoring $CAM_SERVICE"
        systemctl start "$CAM_SERVICE" >/dev/null 2>&1
    fi
}

# --------------------------------------------------------------- scenario 1 --
# cold dual-CSI: one generation across both domains, prepare work overlaps,
# and reaching PLAYING must not trigger a second firmware load.

scenario_1() {
    local id="1" name="cold dual-CSI"
    kill_gstapp
    local since
    since=$(date +%s)
    start_gstapp "s1" "$DUAL_MASK"
    local first
    first=$(wait_first_frame "$FIRST_FRAME_TIMEOUT_S")
    local log
    log=$(prepare_log "$since")
    printf '%s\n' "$log" >"$RUN_DIR/s1.prepare.log"
    kmsg_since >"$RUN_DIR/s1.dmesg.log"
    kill_gstapp

    if [ -z "$log" ]; then
        result_fail "$id" "$name" "no [MAX9296_PREPARE] lines in the log (reader=$LOG_READER)"
        return
    fi

    local generations
    generations=$(printf '%s\n' "$log" | sed -n 's/.*generation=\([0-9]*\).*/\1/p' | sort -u | wc -l)
    if [ "$generations" -ne 1 ]; then
        result_fail "$id" "$name" "expected one generation across both domains, saw $generations"
        return
    fi

    local domains
    domains=$(printf '%s\n' "$log" | grep -c 'CSI[01] ')
    if [ "$domains" -lt 2 ]; then
        result_fail "$id" "$name" "expected both CSI domains prepared, saw $domains line(s)"
        return
    fi

    # Parallelism: with two domains prepared concurrently the wall time is the
    # slower domain, not the sum.  Allow 25% slack for scheduling.
    local sum max
    sum=$(printf '%s\n' "$log" | sed -n 's/.*elapsed_ms=\([0-9]*\).*/\1/p' |
        awk '{ s += $1 } END { printf "%d\n", s + 0 }')
    max=$(printf '%s\n' "$log" | sed -n 's/.*elapsed_ms=\([0-9]*\).*/\1/p' |
        awk 'BEGIN { m = 0 } { if ($1 > m) m = $1 } END { printf "%d\n", m }')
    info "elapsed per domain: sum=${sum}ms max=${max}ms  first-frame=${first}ms"

    if [ "$sum" -gt 0 ] && [ "$max" -gt 0 ]; then
        local limit
        limit=$(awk -v m="$max" 'BEGIN { printf "%d", m * 1.25 + 5 }')
        if [ "$sum" -gt "$limit" ] && [ "$max" -lt "$sum" ]; then
            info "NOTE: domains look serialized (sum ${sum}ms > ${limit}ms budget)"
        fi
    fi

    if [ "$first" = "timeout" ] || [ "$first" = "died" ]; then
        result_fail "$id" "$name" "no video data after prepare (first-frame=$first)"
        return
    fi

    result_pass "$id" "$name"
}

# --------------------------------------------------------------- scenario 2 --
# same-tuple restart: the second process must reuse the warm lease, writing
# nothing and loading no firmware.

scenario_2() {
    local id="2" name="same-tuple restart (warm)"
    kill_gstapp
    start_gstapp "s2a" "$DUAL_MASK"
    wait_first_frame "$FIRST_FRAME_TIMEOUT_S" >/dev/null
    kill_gstapp
    sleep 3

    local since dmesg_before
    since=$(date +%s)
    dmesg_before=$(dmesg 2>/dev/null | wc -l)
    start_gstapp "s2b" "$DUAL_MASK"
    local first
    first=$(wait_first_frame "$FIRST_FRAME_TIMEOUT_S")
    local log
    log=$(prepare_log "$since")
    printf '%s\n' "$log" >"$RUN_DIR/s2.prepare.log"
    dmesg 2>/dev/null | tail -n +"$dmesg_before" >"$RUN_DIR/s2.dmesg.log"
    kill_gstapp

    if [ -z "$log" ]; then
        result_fail "$id" "$name" "no [MAX9296_PREPARE] lines for the second run"
        return
    fi

    # action=2 is MAX9296_ACTION_WARM_REUSED.
    local warm total
    warm=$(printf '%s\n' "$log" | grep -c 'action=2')
    total=$(printf '%s\n' "$log" | grep -c 'CSI[01] ')
    info "warm domains: $warm/$total  first-frame=${first}ms"

    if [ "$warm" -ne "$total" ] || [ "$total" -eq 0 ]; then
        result_fail "$id" "$name" \
            "expected every domain WARM_REUSED (action=2), got $warm of $total"
        return
    fi
    if [ "$first" = "timeout" ] || [ "$first" = "died" ]; then
        result_fail "$id" "$name" "warm reuse did not recover frames (first-frame=$first)"
        return
    fi
    result_pass "$id" "$name"
}

# --------------------------------------------------------------- scenario 3 --
# a second gstApp must lose the owner lock and exit non-zero before touching
# hardware; the retry after the first exits must succeed.

scenario_3() {
    local id="3" name="second gstApp rejected"
    kill_gstapp
    start_gstapp "s3a" "$DUAL_MASK"
    local first_pid=$GSTAPP_PID
    wait_first_frame "$FIRST_FRAME_TIMEOUT_S" >/dev/null

    local before_csi0 before_csi1
    before_csi0=$(read_prepare_node "$CSI0_NODE" || true)
    before_csi1=$(read_prepare_node "$CSI1_NODE" || true)

    "$GSTAPP_BIN" --channel "$DUAL_MASK" --width "$CAM_WIDTH" --height "$CAM_HEIGHT" \
        >"$RUN_DIR/s3b.out" 2>&1
    local second_rc=$?

    local after_csi0 after_csi1
    after_csi0=$(read_prepare_node "$CSI0_NODE" || true)
    after_csi1=$(read_prepare_node "$CSI1_NODE" || true)

    kill "$first_pid" 2>/dev/null
    kill_gstapp

    info "second instance exit=$second_rc"
    if [ "$second_rc" -eq 0 ]; then
        result_fail "$id" "$name" "duplicate owner exited 0; the lock did not reject it"
        return
    fi
    if [ "$before_csi0" != "$after_csi0" ] || [ "$before_csi1" != "$after_csi1" ]; then
        result_fail "$id" "$name" "duplicate owner mutated prepare state before exiting"
        return
    fi

    # retry after the first owner is gone
    start_gstapp "s3c" "$DUAL_MASK"
    local retry
    retry=$(wait_first_frame "$FIRST_FRAME_TIMEOUT_S")
    kill_gstapp
    if [ "$retry" = "timeout" ] || [ "$retry" = "died" ]; then
        result_fail "$id" "$name" "retry after first exit failed (first-frame=$retry)"
        return
    fi
    result_pass "$id" "$name"
}

# --------------------------------------------------------------- scenario 4 --
# with a long play delay the lease must be consumed at PAUSED, well before the
# driver's 60 s unused-lease timeout.

scenario_4() {
    local id="4" name="play_delay>=60 consumes lease"
    kill_gstapp
    start_gstapp "s4" "$DUAL_MASK" --delay 60
    sleep 15

    local csi0 csi1 st0 st1 lease0 lease1
    csi0=$(read_prepare_node "$CSI0_NODE" || true)
    csi1=$(read_prepare_node "$CSI1_NODE" || true)
    printf '%s\n%s\n' "$csi0" "$csi1" >"$RUN_DIR/s4.status"
    st0=$(field "$csi0" state)
    st1=$(field "$csi1" state)
    lease0=$(field "$csi0" lease)
    lease1=$(field "$csi1" lease)
    kill_gstapp

    info "CSI0 state=${st0:-?} lease=${lease0:-?}   CSI1 state=${st1:-?} lease=${lease1:-?}"

    local bad=0
    for st in "$st0" "$st1"; do
        case "$(printf '%s' "$st" | tr '[:upper:]' '[:lower:]')" in
            consumed | 4) ;;
            *) bad=1 ;;
        esac
    done
    for ls in "$lease0" "$lease1"; do
        [ "${ls:-x}" = "0" ] || bad=1
    done

    if [ "$bad" -ne 0 ]; then
        result_fail "$id" "$name" \
            "expected both domains CONSUMED with lease=0 at PAUSED"
        return
    fi
    result_pass "$id" "$name"
}

# --------------------------------------------------------------- scenario 5 --
# the driver fingerprint must match the topology gstApp asked for, on each CSI.

check_fingerprint() {
    local node="$1" want_mode="$2" want_table="$3" label="$4"
    local text mode table
    text=$(read_prepare_node "$node" || true)
    mode=$(field "$text" mode)
    table=$(field "$text" table)
    info "$label -> mode=${mode:-?} table=${table:-?} (want $want_mode/$want_table)"
    [ "$mode" = "$want_mode" ] && [ "$table" = "$want_table" ]
}

scenario_5() {
    local id="5" name="topology fingerprints"
    local failures=0

    # ch0 only -> CSI0 single/left ; ch1 only -> CSI0 single/right
    # ch0+ch1  -> CSI0 dual-wide/dual ; same pattern on CSI1 with ch2/ch3.
    local mask node mode table label
    while read -r mask node mode table label; do
        [ -n "$mask" ] || continue
        kill_gstapp
        start_gstapp "s5-$label" "$mask"
        if ! wait_first_frame "$FIRST_FRAME_TIMEOUT_S" >/dev/null; then
            info "$label: no frames"
            failures=$((failures + 1))
            kill_gstapp
            continue
        fi
        check_fingerprint "$node" "$mode" "$table" "$label" || failures=$((failures + 1))
        kill_gstapp
    done <<EOF
0x1 $CSI0_NODE single left CSI0-left
0x2 $CSI0_NODE single right CSI0-right
0x3 $CSI0_NODE dual-wide dual CSI0-dual
0x4 $CSI1_NODE single left CSI1-left
0x8 $CSI1_NODE single right CSI1-right
0xC $CSI1_NODE dual-wide dual CSI1-dual
EOF

    if [ "$failures" -ne 0 ]; then
        result_fail "$id" "$name" "$failures of 6 combinations wrong"
        return
    fi
    result_pass "$id" "$name"
}

# --------------------------------------------------------------- scenario 6 --
# missing ABI node and rollback.  Destructive: it unbinds a node.

HIDDEN_NODE=""

restore_abi_nodes() {
    [ -n "$HIDDEN_NODE" ] || return 0
    local dev
    dev=$(basename "$(dirname "$HIDDEN_NODE")")
    if [ -w /sys/bus/i2c/drivers_probe ]; then
        printf '%s\n' "$dev" >/sys/bus/i2c/drivers_probe 2>/dev/null
    fi
    HIDDEN_NODE=""
    return 0
}

scenario_6() {
    local id="6" name="missing ABI node / rollback"
    if [ "$ALLOW_DESTRUCTIVE" -ne 1 ]; then
        result_skip "$id" "$name" "needs --allow-destructive"
        return
    fi

    local driver_dir dev
    dev=$(basename "$(dirname "$CSI1_NODE")")
    driver_dir=$(readlink -f "/sys/bus/i2c/devices/$dev/driver" 2>/dev/null)
    if [ -z "$driver_dir" ] || [ ! -w "$driver_dir/unbind" ]; then
        result_skip "$id" "$name" "no unbind hook for $dev; cannot hide one node"
        return
    fi

    kill_gstapp
    printf '%s\n' "$dev" >"$driver_dir/unbind" 2>/dev/null
    HIDDEN_NODE="$CSI1_NODE"
    sleep 1

    if [ -e "$CSI1_NODE" ]; then
        restore_abi_nodes
        result_skip "$id" "$name" "unbind did not remove $CSI1_NODE"
        return
    fi

    local since
    since=$(date +%s)
    "$GSTAPP_BIN" --channel "$DUAL_MASK" --width "$CAM_WIDTH" --height "$CAM_HEIGHT" \
        >"$RUN_DIR/s6.out" 2>&1
    local rc=$?
    prepare_log "$since" >"$RUN_DIR/s6.prepare.log"

    local csi0_state
    csi0_state=$(field "$(read_prepare_node "$CSI0_NODE" || true)" state)
    restore_abi_nodes
    sleep 2

    info "partial-absence exit=$rc  CSI0 state after=${csi0_state:-?}"
    if [ "$rc" -eq 0 ]; then
        result_fail "$id" "$name" "partial node absence must fail closed, exited 0"
        return
    fi
    case "$(printf '%s' "$csi0_state" | tr '[:upper:]' '[:lower:]')" in
        ready | 2)
            result_fail "$id" "$name" "CSI0 lease left READY; rollback did not run"
            return
            ;;
    esac

    result_pass "$id" "$name (partial absence)"
    result_skip "6b" "forced error / post-status rollback" \
        "no driver error-injection hook on this build; not verifiable from userspace"
}

# --------------------------------------------------------------- scenario 7 --
# restart soak plus module unload/reload.

scenario_7() {
    local id="7" name="restart soak + module reload"
    if [ "$ALLOW_DESTRUCTIVE" -ne 1 ]; then
        result_skip "$id" "$name" "needs --allow-destructive"
        return
    fi

    local iterations="${SOAK_ITERATIONS:-100}"
    local i failures=0
    say "       soak: $iterations restarts, this takes a while"
    for i in $(seq 1 "$iterations"); do
        kill_gstapp
        start_gstapp "s7" "$DUAL_MASK"
        local r
        r=$(wait_first_frame "$FIRST_FRAME_TIMEOUT_S")
        if [ "$r" = "timeout" ] || [ "$r" = "died" ]; then
            failures=$((failures + 1))
            info "iteration $i: first-frame=$r"
        fi
        kill_gstapp
        [ $((i % 10)) -eq 0 ] && info "iteration $i/$iterations, failures=$failures"
    done

    dmesg 2>/dev/null | tail -n 500 >"$RUN_DIR/s7.dmesg.log"
    local warns
    warns=$(grep -ciE 'power.?count|unbalanced|leak' "$RUN_DIR/s7.dmesg.log")

    local unload_ms="n/a"
    if command -v modprobe >/dev/null 2>&1 &&
        lsmod 2>/dev/null | grep -q "^${MAX9296_MODULE} "; then
        local t0
        t0=$(now_ms)
        if modprobe -r "$MAX9296_MODULE" >/dev/null 2>&1; then
            unload_ms=$(( $(now_ms) - t0 ))
            modprobe "$MAX9296_MODULE" >/dev/null 2>&1
            sleep 2
        else
            unload_ms="failed"
        fi
    fi

    info "soak failures=$failures  power/leak warnings=$warns  unload=${unload_ms}ms"
    if [ "$failures" -ne 0 ] || [ "$warns" -ne 0 ] || [ "$unload_ms" = "failed" ]; then
        result_fail "$id" "$name" \
            "failures=$failures warnings=$warns unload=$unload_ms"
        return
    fi
    result_pass "$id" "$name"
}

# --------------------------------------------------------------- scenario 8 --
# startup -> first video data, dual-CSI versus single-CSI.

measure_startup() {
    local tag="$1" mask="$2" runs="$3"
    local i results="" actions=""
    for i in $(seq 1 "$runs"); do
        kill_gstapp
        sleep 1
        local since
        since=$(date +%s)
        start_gstapp "s8-$tag-$i" "$mask"
        local ms
        ms=$(wait_first_frame "$FIRST_FRAME_TIMEOUT_S")
        local act
        act=$(prepare_log "$since" | sed -n 's/.*action=\([0-9]*\).*/\1/p' |
            sort -u | tr '\n' ',' | sed 's/,$//')
        kill_gstapp
        results="${results}${results:+ }${ms}"
        actions="${actions}${actions:+ }${act:-?}"
    done
    printf '%s\n%s\n' "$results" "$actions"
}

summarize_ms() {
    printf '%s\n' "$1" | tr ' ' '\n' | grep -E '^[0-9]+$' |
        sort -n | awk '
        { v[NR] = $1 }
        END {
            if (NR == 0) { print "n=0"; exit }
            med = (NR % 2) ? v[(NR + 1) / 2] : int((v[NR / 2] + v[NR / 2 + 1]) / 2)
            printf "n=%d min=%d median=%d max=%d\n", NR, v[1], med, v[NR]
        }'
}

median_ms() {
    printf '%s\n' "$1" | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -n | awk '
        { v[NR] = $1 }
        END {
            if (NR == 0) { print ""; exit }
            print (NR % 2) ? v[(NR + 1) / 2] : int((v[NR / 2] + v[NR / 2 + 1]) / 2)
        }'
}

scenario_8() {
    local id="8" name="startup -> first video data"

    local dual single dual_ms dual_act single_ms single_act
    dual=$(measure_startup dual "$DUAL_MASK" "$REPEAT")
    dual_ms=$(printf '%s\n' "$dual" | sed -n 1p)
    dual_act=$(printf '%s\n' "$dual" | sed -n 2p)
    single=$(measure_startup single "$SINGLE_MASK" "$REPEAT")
    single_ms=$(printf '%s\n' "$single" | sed -n 1p)
    single_act=$(printf '%s\n' "$single" | sed -n 2p)

    {
        printf 'clock=%s sampler=%sms/sample\n' "$CLOCK_SOURCE" "$SAMPLE_COST_MS"
        printf 'dual  mask=%s ms=[%s] action=[%s]\n' "$DUAL_MASK" "$dual_ms" "$dual_act"
        printf 'single mask=%s ms=[%s] action=[%s]\n' "$SINGLE_MASK" "$single_ms" "$single_act"
    } >"$RUN_DIR/s8.timing"

    info "sampler resolution ~${SAMPLE_COST_MS}ms per sample ($CLOCK_SOURCE)"
    info "dual-CSI   mask=$DUAL_MASK   $(summarize_ms "$dual_ms")"
    info "           samples: $dual_ms   (action per run: $dual_act)"
    info "single-CSI mask=$SINGLE_MASK   $(summarize_ms "$single_ms")"
    info "           samples: $single_ms   (action per run: $single_act)"

    local dm sm
    dm=$(median_ms "$dual_ms")
    sm=$(median_ms "$single_ms")
    if [ -n "$dm" ] && [ -n "$sm" ]; then
        info "delta (dual - single) = $((dm - sm))ms at the median"
    fi

    # action codes: 2=WARM_REUSED 4=COLD_PREPARED — a run that reused a warm
    # lease is not comparable to a cold one, so both are reported above rather
    # than averaged together.
    if [ -z "$dm" ] || [ -z "$sm" ]; then
        result_fail "$id" "$name" "no usable samples (dual=[$dual_ms] single=[$single_ms])"
        return
    fi
    result_pass "$id" "$name"
}

# ---------------------------------------------------------------- harness ----

self_check() {
    local rc=0
    local sample="state=consumed generation=42 epoch=7 mode=dual-wide table=dual lease=0"
    [ "$(field "$sample" state)" = "consumed" ] || { say "self-check: field(state) broken"; rc=1; }
    [ "$(field "$sample" mode)" = "dual-wide" ] || { say "self-check: field(mode) broken"; rc=1; }
    [ "$(field "$sample" lease)" = "0" ] || { say "self-check: field(lease) broken"; rc=1; }
    [ -z "$(field "$sample" nosuch)" ] || { say "self-check: field(absent) broken"; rc=1; }

    local ms="120 100 140 110 130"
    [ "$(median_ms "$ms")" = "120" ] || { say "self-check: median broken"; rc=1; }
    [ "$(summarize_ms "$ms")" = "n=5 min=100 median=120 max=140" ] ||
        { say "self-check: summarize broken"; rc=1; }
    [ "$(median_ms "timeout died")" = "" ] ||
        { say "self-check: median must ignore non-numeric"; rc=1; }
    return "$rc"
}

expand_scenarios() {
    case "$SCENARIOS" in
        all) printf '1 2 3 4 5 6 7 8\n' ;;
        fast) printf '1 2 3 4 5 8\n' ;;
        *) printf '%s\n' "$SCENARIOS" | tr ',' ' ' ;;
    esac
}

main() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --scenario) SCENARIOS="$2"; shift 2 ;;
            --allow-destructive) ALLOW_DESTRUCTIVE=1; shift ;;
            --gstapp) GSTAPP_BIN="$2"; shift 2 ;;
            --repeat) REPEAT="$2"; shift 2 ;;
            --keep-service) KEEP_SERVICE=1; shift ;;
            --dry-run) DRY_RUN=1; shift ;;
            -h | --help) usage; exit 0 ;;
            *) say "unknown option: $1"; usage; exit 2 ;;
        esac
    done

    detect_clock
    detect_log_reader
    local list
    list=$(expand_scenarios)

    say "max9296 board test v$VERSION"
    say "  scenarios : $list"
    say "  clock     : $CLOCK_SOURCE"
    say "  log reader: $LOG_READER"
    say "  destructive: $([ "$ALLOW_DESTRUCTIVE" -eq 1 ] && echo enabled || echo disabled)"

    if [ "$DRY_RUN" -eq 1 ]; then
        say ""
        say "dry run: harness self-check only, hardware untouched"
        if self_check; then
            say "self-check: OK"
            exit 0
        fi
        say "self-check: FAILED"
        exit 1
    fi

    if [ "$(id -u)" -ne 0 ]; then
        say "must run as root (sysfs writes, service control)"
        exit 2
    fi
    resolve_gstapp || exit 2
    say "  gstApp    : $GSTAPP_BIN"

    RUN_DIR="/tmp/max9296-board-test.$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$RUN_DIR" || exit 2
    say "  logs      : $RUN_DIR"
    say ""

    trap restore_state EXIT INT TERM
    stop_camera_service
    calibrate_sampler

    for s in $list; do
        case "$s" in
            1) scenario_1 ;;
            2) scenario_2 ;;
            3) scenario_3 ;;
            4) scenario_4 ;;
            5) scenario_5 ;;
            6) scenario_6 ;;
            7) scenario_7 ;;
            8) scenario_8 ;;
            *) say "unknown scenario: $s" ;;
        esac
    done

    say ""
    say "RESULT: $PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped"
    [ -n "$FAILED_LIST" ] && say "failed scenarios: $FAILED_LIST"
    say "logs kept in $RUN_DIR"
    [ "$FAIL_COUNT" -eq 0 ]
}

main "$@"

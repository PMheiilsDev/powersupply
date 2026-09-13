#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 && $# != 3 ]]; then
    printf 'Usage: bash %s /dev/ttyACM0 [--voltage 6.50]\n' "$0" >&2
    exit 2
fi
device=$1
if [[ ! -c "$device" ]]; then
    printf 'Not a serial device: %s\n' "$device" >&2
    exit 2
fi
if [[ $# == 3 && ( $2 != --voltage || ! $3 =~ ^[0-9]+([.][0-9]+)?$ ) ]]; then
    printf 'Expected --voltage followed by a nonnegative decimal number\n' >&2
    exit 2
fi

original=$(stty -F "$device" -g)
trap 'stty -F "$device" "$original" 2>/dev/null || true' EXIT
stty -F "$device" 115200 raw -echo -ixon -ixoff
exec 3<>"$device"

query()
{
    local response
    printf '%s\n' "$1" >&3
    if ! IFS= read -r -t 45 -u 3 response; then
        printf 'Timed out waiting for %s; check wiring and SYST:ERR?\n' "$1" >&2
        exit 1
    fi
    printf '%s\n' "${response%$'\r'}"
}

printf '*CLS\n' >&3
identity=$(query '*IDN?')
[[ $identity == Pico,Powersupply,* ]] || { printf 'Unexpected identity: %s\n' "$identity" >&2; exit 1; }
printf 'Identity: %s\n' "$identity"
printf 'Display status: %s\n' "$(query 'SYST:DISP:STAT?')"
printf 'Display timings: %s\n' "$(query 'SYST:DISP:TIM?')"
printf 'Displayed voltage: %s V\n' "$(query 'VOLT?')"
printf 'Displayed current: %s A\n' "$(query 'CURR?')"

if [[ $# == 3 ]]; then
    [[ $(query 'OUTP?') == 0 ]] || { printf 'Turn output off before setting voltage\n' >&2; exit 1; }
    printf 'VOLT %s\n' "$3" >&3
    completion=$(query '*OPC?')
    [[ $completion == 1 ]] || { printf 'Unexpected completion: %s\n' "$completion" >&2; exit 1; }
    error=$(query 'SYST:ERR?')
    [[ $error == '0,"No error"' ]] || { printf '%s\n' "$error" >&2; exit 1; }
    actual=$(query 'VOLT?')
    awk -v actual="$actual" -v target="$3" 'BEGIN {delta = actual - target; exit !(delta >= -0.010001 && delta <= 0.010001)}' || {
        printf 'Target %s V, display %s V\n' "$3" "$actual" >&2
        exit 1
    }
    printf 'Verified voltage: %s V (tolerance 0.01 V)\n' "$actual"
fi
error=$(query 'SYST:ERR?')
[[ $error == '0,"No error"' ]] || { printf '%s\n' "$error" >&2; exit 1; }
printf 'Serial smoke test passed\n'
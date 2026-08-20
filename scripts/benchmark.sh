#!/usr/bin/env bash
# benchmark.sh — Run both algorithms across several configs and print a table
# Usage: ./scripts/benchmark.sh

MS=./external_mergesort
QS=./external_quicksort

if [[ ! -x "$MS" || ! -x "$QS" ]]; then
    echo "Please run 'make' first to build the binaries."
    exit 1
fi

echo ""
echo "╔══════════════════════════════════════════════════════════════════════════╗"
echo "║           External Sorting Benchmark                                    ║"
echo "╠══════════════╦══════╦═════════════════════╦═════════════════════════════╣"
printf "║ %-12s ║ %-4s ║ %-19s ║ %-27s ║\n" "Config" "m" "Ext. Quicksort" "Ext. Merge Sort"
echo "╠══════════════╩══════╩═════════════════════╩═════════════════════════════╣"
printf "║ %-12s   %-4s   %-9s %-9s   %-13s %-13s  ║\n" \
    "n/k/b" "" "Seeks" "Xfers" "Seeks" "Xfers"
echo "╠══════════════════════════════════════════════════════════════════════════╣"

run_algo() {
    local exe=$1; local n=$2; local k=$3; local b=$4; local m=$5
    python3 -c "import random; random.seed(42); [print(random.randint(-100000,100000)) for _ in range($n)]" > /tmp/_bench_in.txt
    local out
    out=$($exe /tmp/_bench_in.txt $n $k $b $m 2>/dev/null)
    local seeks xfers
    seeks=$(echo "$out" | grep "Total disk seeks" | grep -oP '\d+' | tail -1)
    xfers=$(echo "$out" | grep "Total disk transfers" | grep -oP '\d+' | tail -1)
    echo "$seeks $xfers"
}

configs=(
    "100  4  512   5"
    "100  4  512   10"
    "500  4  1024  8"
    "1000 4  1024  10"
    "5000 4  1024  10"
    "10000 4 1024  10"
)

for cfg in "${configs[@]}"; do
    read -r n k b m <<< "$cfg"
    qs_res=$(run_algo $QS $n $k $b $m)
    ms_res=$(run_algo $MS $n $k $b $m)
    qs_s=$(echo $qs_res | awk '{print $1}')
    qs_x=$(echo $qs_res | awk '{print $2}')
    ms_s=$(echo $ms_res | awk '{print $1}')
    ms_x=$(echo $ms_res | awk '{print $2}')
    printf "║ n=%-5s k=%s b=%-4s   %-4s   %-9s %-9s   %-13s %-13s  ║\n" \
        "$n" "$k" "$b" "$m" "$qs_s" "$qs_x" "$ms_s" "$ms_x"
done

echo "╚══════════════════════════════════════════════════════════════════════════╝"
echo ""

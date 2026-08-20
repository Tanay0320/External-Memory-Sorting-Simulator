#!/usr/bin/env bash
# gen_input.sh <count> [seed]
# Generates <count> random integers in [-100000, 100000], one per line
COUNT=${1:-100}
SEED=${2:-42}
python3 -c "
import random
random.seed($SEED)
for _ in range($COUNT):
    print(random.randint(-100000, 100000))
"

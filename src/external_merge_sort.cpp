/*
 * External Merge Sort Simulation
 * ================================
 * CS315 — Principles of Database Systems
 * IIT Kanpur | Assignment: External Sorting
 *
 * Author  : Prashant Shekhar
 * Language: C++17
 *
 * ── Overview ──────────────────────────────────────────────────────────────────
 * I simulate disk-based external merge sort using two core data structures:
 *   - vector<vector<vector<int>>> disk  : disk[file_id][block_idx] is one disk block
 *   - vector<vector<int>>       memory : memory[slot] is one in-memory block slot
 *
 * ── Algorithm (two phases) ────────────────────────────────────────────────────
 *
 *  PHASE 1 — Initial Run Generation (Replacement Selection):
 *   I use all M memory blocks to hold a min-heap. Keys are loaded from the input
 *   file in batches. At each step I emit the smallest *active* heap entry to the
 *   current run's output buffer. A newly loaded key is "frozen" (marked for the
 *   next run) if it is smaller than the last emitted key. When every heap entry
 *   is frozen, I close the current run and start a fresh one.
 *   → Average run length ≈ 2M blocks on uniformly random input.
 *
 *  PHASE 2 — Multi-Way Merge Passes:
 *   With M−1 input buffer slots and 1 output buffer slot I merge up to (M−1)
 *   runs simultaneously using a min-heap of (value, run_index) pairs.
 *   Each pass reduces the run count by factor (M−1).
 *   I repeat until exactly one sorted run remains.
 *
 * ── Memory layout ─────────────────────────────────────────────────────────────
 *   Phase 1:  Slots 0..M-1    → replacement-selection heap/buffer
 *   Phase 2:  Slots 0..M-2   → input buffers (one per run being merged)
 *             Slot  M-1       → output block buffer
 *
 * ── Disk I/O model ────────────────────────────────────────────────────────────
 *   • Sequential read of 'c' blocks from the SAME file → 1 seek + c transfers
 *   • Switching files or seeking to a new position     → 1 new seek
 *   • Writing one block (output flush)                 → 1 seek + 1 transfer
 *
 * ── Pass definition ───────────────────────────────────────────────────────────
 *   Pre-pass : Input file load from OS to simulated disk
 *   Pass 0   : Phase 1 — replacement-selection run generation
 *   Pass 1.. : Phase 2 — each multi-way merge sweep
 *
 * ── Usage ─────────────────────────────────────────────────────────────────────
 *   ./external_mergesort input-file.txt <n> <k> <b> <m>
 *
 *   n = total number of keys
 *   k = key size in bytes
 *   b = disk block size in bytes
 *   m = available memory in number of blocks
 */

#include <bits/stdc++.h>
using namespace std;

// ─── Global parameters (read from command line) ──────────────────────────────
int N, K, B, M;
int KEYS_PER_BLOCK;          // = floor(B / K), minimum 1

// ─── Simulated disk ──────────────────────────────────────────────────────────
// disk[fid][blk_idx] holds a vector of integer keys (one disk block)
vector<vector<vector<int>>> disk;

// ─── Simulated memory ────────────────────────────────────────────────────────
// memory[slot] holds at most KEYS_PER_BLOCK integers
vector<vector<int>> memory;

// ─── I/O statistics ──────────────────────────────────────────────────────────
struct PassStats {
    long long seeks     = 0;
    long long transfers = 0;
    string    label;
};
vector<PassStats> pass_log;   // pass_log[0]=Phase1, pass_log[1..]=merge passes
long long total_seeks     = 0;
long long total_transfers = 0;

// Accumulate I/O cost into the correct pass bucket and running totals
void record_io(int pass_idx, long long s, long long t) {
    while ((int)pass_log.size() <= pass_idx)
        pass_log.push_back({});
    pass_log[pass_idx].seeks     += s;
    pass_log[pass_idx].transfers += t;
    total_seeks                  += s;
    total_transfers              += t;
}

// ─── Disk helper functions ───────────────────────────────────────────────────

// Allocate a new empty file on the simulated disk; return its file ID
int disk_new_file() {
    disk.push_back({});
    return (int)disk.size() - 1;
}

int disk_file_blocks(int fid) { return (int)disk[fid].size(); }

int disk_file_keys(int fid) {
    int c = 0;
    for (auto& blk : disk[fid]) c += (int)blk.size();
    return c;
}

// Read 'count' consecutive blocks from file 'fid' starting at 'start_blk'
// into memory slots [mem_start .. mem_start+count-1].
// I/O cost: 1 seek + count transfers.
void disk_read_seq(int fid, int start_blk, int count, int mem_start, int pass_idx) {
    for (int i = 0; i < count; i++)
        memory[mem_start + i] = disk[fid][start_blk + i];
    record_io(pass_idx, 1, count);
}

// Write one block to the end of file 'fid'.
// I/O cost: 1 seek + 1 transfer.
void disk_write_block(int fid, const vector<int>& blk, int pass_idx) {
    disk[fid].push_back(blk);
    record_io(pass_idx, 1, 1);
}

// ─── Pretty-print helpers ────────────────────────────────────────────────────

void print_sep(char c = '-', int w = 76) {
    cout << string(w, c) << "\n";
}

void print_file(const string& lbl, int fid) {
    cout << lbl
         << " [file=" << fid
         << "  blks=" << disk_file_blocks(fid)
         << "  keys=" << disk_file_keys(fid) << "]: ";
    for (auto& blk : disk[fid]) {
        cout << "| ";
        for (int v : blk) cout << v << " ";
    }
    cout << "|\n";
}

void print_memory_layout_phase1() {
    cout << "\n=== Memory Layout — Phase 1 (Replacement-Selection Run Generation) ===\n";
    cout << "  Total memory blocks         : " << M << "\n";
    cout << "  Keys per block              : " << KEYS_PER_BLOCK << "\n";
    cout << "  Heap / buffer slots         : 0.." << M-1
         << "  (" << M << " slots, all used for heap)\n";
    cout << "  Expected avg run length     : ~" << 2*M
         << " blocks on uniformly random input\n";
    cout << "  Heap capacity               : " << (long long)M * KEYS_PER_BLOCK
         << " keys\n";
    print_sep();
}

void print_memory_layout_phase2(int fan_in) {
    cout << "\n=== Memory Layout — Phase 2 Merge  (fan-in=" << fan_in << ") ===\n";
    cout << "  Total memory blocks         : " << M << "\n";
    cout << "  Input buffer slots          : 0.." << fan_in-1
         << "  (" << fan_in << " slots, one per run)\n";
    cout << "  Output buffer slot          : " << M-1
         << "  (1 slot, flushed when full)\n";
    print_sep();
}

// ═════════════════════════════════════════════════════════════════════════════
// PHASE 1: Replacement-Selection Run Generation
// ─────────────────────────────────────────────
// Returns a list of file IDs, each holding one sorted run.
// ═════════════════════════════════════════════════════════════════════════════
vector<int> phase1_generate_runs(int input_fid) {
    const int PASS_IDX = 0;   // all Phase-1 I/O attributed to pass_log[0]

    cout << "\n╔══════════════════════════════════════════════════════════════════╗\n"
         << "║        PHASE 1 — Initial Run Generation                         ║\n"
         << "║        Strategy : Replacement Selection (min-heap, size M)      ║\n"
         << "╚══════════════════════════════════════════════════════════════════╝\n\n";

    int total_blocks = disk_file_blocks(input_fid);
    int total_keys   = disk_file_keys(input_fid);
    cout << "  Input file=" << input_fid
         << "  total_blocks=" << total_blocks
         << "  total_keys=" << total_keys << "\n";
    cout << "  Memory M=" << M
         << "  KEYS_PER_BLOCK=" << KEYS_PER_BLOCK
         << "  heap_capacity=" << (long long)M * KEYS_PER_BLOCK << "\n\n";

    // ── Read entire input file into flat key array ────────────────────────────
    // I read in batches of M blocks to charge I/O correctly.
    vector<int> all_keys;
    all_keys.reserve(total_keys);
    {
        cout << "  ─── Reading input file (batch size = " << M << " blocks) ─────────\n";
        int num_batches = 0;
        for (int i = 0; i < total_blocks; ) {
            int batch = min(M, total_blocks - i);
            disk_read_seq(input_fid, i, batch, 0, PASS_IDX);
            for (int s = 0; s < batch; s++)
                for (int v : memory[s]) all_keys.push_back(v);
            cout << "  [Read Batch " << ++num_batches << "]"
                 << "  blocks " << i << ".." << i+batch-1
                 << "  (" << batch << " block(s), 1 seek + "
                 << batch << " transfers)\n";
            i += batch;
        }
        // Correction: 'num_batches' batches on the same file still = 1 seek total
        // My disk_read_seq charges 1 seek per call; subtract the extras.
        int extra_seeks = num_batches - 1;
        if (extra_seeks > 0) {
            pass_log[PASS_IDX].seeks -= extra_seeks;
            total_seeks              -= extra_seeks;
            cout << "  [I/O Correction] " << num_batches
                 << " sequential batches on same file → subtract "
                 << extra_seeks << " extra seek(s)\n";
        }
        cout << "  Total keys loaded into memory: " << all_keys.size() << "\n\n";
    }

    // ── Replacement-selection heap ────────────────────────────────────────────
    // Each heap entry carries its key value and a 'frozen' flag.
    // Active entries (frozen=false) belong to the CURRENT run.
    // Frozen entries are deferred to the NEXT run.
    // Ordering: active before frozen; within same class, smaller key first.
    struct HeapEntry {
        int  key;
        bool frozen;
        bool operator>(const HeapEntry& o) const {
            if (frozen != o.frozen) return frozen > o.frozen; // active wins
            return key > o.key;                               // smaller key wins
        }
    };

    long long heap_capacity = (long long)M * KEYS_PER_BLOCK;
    int  read_pos    = 0;
    int  initial_load = (int)min((long long)all_keys.size(), heap_capacity);

    priority_queue<HeapEntry, vector<HeapEntry>, greater<HeapEntry>> heap;
    for (int i = 0; i < initial_load; i++)
        heap.push({all_keys[read_pos++], false});

    cout << "  Heap pre-loaded with " << initial_load << " key(s).\n";
    cout << "  Unloaded keys remaining : "
         << (int)all_keys.size() - read_pos << "\n\n";

    // ── Main replacement-selection loop ───────────────────────────────────────
    vector<int> run_files;
    int  run_num      = 0;
    int  last_output  = INT_MIN;   // last key emitted to current run
    int  cur_fid      = disk_new_file();
    run_files.push_back(cur_fid);
    vector<int> out_buf;           // accumulate keys for current output block
    int  run_key_count = 0;

    cout << "  ─── Replacement-Selection Loop ───────────────────────────────────\n";
    cout << "  Run " << run_num << " opened → file=" << cur_fid << "\n";

    while (!heap.empty()) {

        // If the top is frozen, every remaining entry is frozen → close run
        if (heap.top().frozen) {

            // Flush any partial output block for the closing run
            if (!out_buf.empty()) {
                disk_write_block(cur_fid, out_buf, PASS_IDX);
                cout << "    [Partial Flush] run=" << run_num
                     << "  file=" << cur_fid
                     << "  " << out_buf.size() << " key(s)"
                     << "  (1 seek + 1 transfer)\n";
                out_buf.clear();
            }
            cout << "  Run " << run_num << " closed → file=" << cur_fid
                 << "  blocks=" << disk_file_blocks(cur_fid)
                 << "  keys=" << run_key_count << "\n";

            // Start a new run
            run_num++;
            last_output   = INT_MIN;
            run_key_count = 0;
            cur_fid       = disk_new_file();
            run_files.push_back(cur_fid);

            // Unfreeze every frozen entry for the new run
            vector<HeapEntry> tmp;
            while (!heap.empty()) { auto e = heap.top(); heap.pop(); e.frozen=false; tmp.push_back(e); }
            for (auto& e : tmp) heap.push(e);

            cout << "  Run " << run_num << " opened → file=" << cur_fid
                 << "  (heap unfrozen: " << tmp.size() << " key(s))\n";
        }

        // Pop smallest active key and emit to the current run
        HeapEntry e = heap.top(); heap.pop();
        out_buf.push_back(e.key);
        last_output = e.key;
        run_key_count++;

        // Flush output block when it reaches block capacity
        if ((int)out_buf.size() == KEYS_PER_BLOCK) {
            disk_write_block(cur_fid, out_buf, PASS_IDX);
            int blk_no = disk_file_blocks(cur_fid) - 1;
            cout << "    [Write Block] run=" << run_num
                 << "  file=" << cur_fid
                 << "  block[" << blk_no << "]"
                 << "  keys=[" << out_buf.front() << ".." << out_buf.back() << "]"
                 << "  (1 seek + 1 transfer)\n";
            out_buf.clear();
        }

        // Load next key from remaining input (if any), mark frozen if needed
        if (read_pos < (int)all_keys.size()) {
            int nk    = all_keys[read_pos++];
            bool frz  = (nk < last_output);
            heap.push({nk, frz});
        }
    }

    // Final partial block for the last run
    if (!out_buf.empty()) {
        disk_write_block(cur_fid, out_buf, PASS_IDX);
        cout << "    [Final Write] run=" << run_num
             << "  file=" << cur_fid
             << "  " << out_buf.size() << " key(s)"
             << "  keys=[" << out_buf.front() << ".." << out_buf.back() << "]"
             << "  (1 seek + 1 transfer)\n";
    }
    // Close the last run (or discard if empty)
    if (disk_file_keys(cur_fid) > 0) {
        cout << "  Run " << run_num << " closed → file=" << cur_fid
             << "  blocks=" << disk_file_blocks(cur_fid)
             << "  keys=" << run_key_count << "\n";
    } else {
        run_files.pop_back();
        disk.pop_back();
        cout << "  [Empty run discarded]\n";
    }

    // ── Phase 1 summary ───────────────────────────────────────────────────────
    int R = (int)run_files.size();
    cout << "\n  ─── Phase 1 Summary ─────────────────────────────────────────────\n";
    cout << "  Total runs generated : " << R << "\n";
    for (int i = 0; i < R; i++) {
        int rf = run_files[i];
        cout << "    Run " << i << ": file=" << rf
             << "  blocks=" << disk_file_blocks(rf)
             << "  keys=" << disk_file_keys(rf) << "\n";
        print_file("       Contents", rf);
    }
    if ((int)pass_log.size() > PASS_IDX) {
        cout << "  Phase 1 I/O: "
             << pass_log[PASS_IDX].seeks     << " seek(s)  "
             << pass_log[PASS_IDX].transfers << " transfer(s)\n";
    }
    print_sep('=');

    return run_files;
}

// ═════════════════════════════════════════════════════════════════════════════
// PHASE 2: Multi-Way Merge Passes
// ────────────────────────────────
// Repeatedly merges groups of up to (M-1) runs using a min-heap.
// Returns the file ID of the final single sorted run.
// ═════════════════════════════════════════════════════════════════════════════
int phase2_merge(vector<int> run_files) {
    int fan_in   = max(2, M - 1);   // merge at most (M-1) runs at once
    int pass_num = 1;                // pass_log index (0 = Phase 1)

    cout << "\n╔══════════════════════════════════════════════════════════════════╗\n"
         << "║        PHASE 2 — Multi-Way Merge Passes                         ║\n"
         << "║        Fan-in = M-1 = " << fan_in
         << "  (M=" << M << ", 1 output slot reserved)         ║\n"
         << "╚══════════════════════════════════════════════════════════════════╝\n";

    while ((int)run_files.size() > 1) {

        int cur_fan = min(fan_in, (int)run_files.size());
        print_memory_layout_phase2(cur_fan);

        cout << "\n  ═══ Merge Pass " << pass_num
             << "  (" << run_files.size()
             << " run(s) → groups of up to " << fan_in << ") ═══\n";

        vector<int> next_runs;
        int group = 0;

        for (int start = 0; start < (int)run_files.size(); start += fan_in) {
            int end   = min(start + fan_in, (int)run_files.size());
            int count = end - start;
            group++;

            cout << "\n  ── Group " << group
                 << "  (" << count << " run(s): files ";
            for (int i = start; i < end; i++)
                cout << run_files[i] << (i<end-1 ? "," : "");
            cout << ") ──\n";

            // Single run: pass through — no I/O needed
            if (count == 1) {
                cout << "    [Passthrough] Single run, no merge needed → file="
                     << run_files[start] << "\n";
                next_runs.push_back(run_files[start]);
                continue;
            }

            // ── Per-run state for the merge ───────────────────────────────────
            struct RunState {
                int  fid;
                int  blk_cursor;    // index of next block to read from disk
                int  key_cursor;    // index of next key within loaded memory slot
                int  mem_slot;      // memory slot for this run's input buffer
                bool exhausted;
            };

            int out_fid = disk_new_file();
            next_runs.push_back(out_fid);

            int total_run_keys = 0;
            for (int i = start; i < end; i++)
                total_run_keys += disk_file_keys(run_files[i]);
            cout << "    Output → file=" << out_fid
                 << "  (merging " << total_run_keys << " total keys)\n";

            vector<RunState> states(count);
            for (int i = 0; i < count; i++) {
                int fid = run_files[start + i];
                states[i] = {fid, 0, 0, i, (disk_file_blocks(fid) == 0)};

                if (!states[i].exhausted) {
                    // Load the first block of this run into its memory slot
                    disk_read_seq(fid, 0, 1, i, pass_num);
                    states[i].blk_cursor = 1;
                    cout << "    [Init]  run " << i
                         << "  file=" << fid
                         << "  loaded block 0"
                         << "  (1 seek + 1 transfer)\n";
                }
            }

            // ── Min-heap merge loop ───────────────────────────────────────────
            using HE = pair<int,int>;    // (key_value, run_index)
            priority_queue<HE, vector<HE>, greater<HE>> mheap;

            for (int i = 0; i < count; i++)
                if (!states[i].exhausted)
                    mheap.push({memory[states[i].mem_slot][0], i});

            vector<int> out_block;
            int keys_merged = 0;
            int blocks_out  = 0;

            while (!mheap.empty()) {
                auto [val, ri] = mheap.top(); mheap.pop();

                // Emit this key to the output buffer
                out_block.push_back(val);
                keys_merged++;

                // Flush output block when full
                if ((int)out_block.size() == KEYS_PER_BLOCK) {
                    disk_write_block(out_fid, out_block, pass_num);
                    blocks_out++;
                    cout << "    [Out Block " << blocks_out << "]"
                         << "  keys=[" << out_block.front()
                         << ".." << out_block.back() << "]"
                         << "  (1 seek + 1 transfer)\n";
                    out_block.clear();
                }

                // Advance the run that just contributed a key
                RunState& rs = states[ri];
                rs.key_cursor++;

                if (rs.key_cursor >= (int)memory[rs.mem_slot].size()) {
                    // Current loaded block exhausted — try loading next block
                    if (rs.blk_cursor < disk_file_blocks(rs.fid)) {
                        disk_read_seq(rs.fid, rs.blk_cursor, 1,
                                      rs.mem_slot, pass_num);
                        cout << "      [Refill] run " << ri
                             << "  file=" << rs.fid
                             << "  block " << rs.blk_cursor
                             << "  (1 seek + 1 transfer)\n";
                        rs.blk_cursor++;
                        rs.key_cursor = 0;
                        mheap.push({memory[rs.mem_slot][0], ri});
                    } else {
                        rs.exhausted = true;
                        cout << "      [Exhausted] run " << ri
                             << "  file=" << rs.fid << "\n";
                    }
                } else {
                    mheap.push({memory[rs.mem_slot][rs.key_cursor], ri});
                }
            }

            // Flush final partial output block
            if (!out_block.empty()) {
                disk_write_block(out_fid, out_block, pass_num);
                blocks_out++;
                cout << "    [Final Out Block]"
                     << "  keys=[" << out_block.front()
                     << ".." << out_block.back() << "]"
                     << "  (1 seek + 1 transfer)\n";
            }

            cout << "    ── Group " << group << " done → file=" << out_fid
                 << "  blocks=" << disk_file_blocks(out_fid)
                 << "  keys=" << keys_merged << "\n";
            print_file("       Merged output", out_fid);
        }

        // ── Pass summary ──────────────────────────────────────────────────────
        cout << "\n  ─── Merge Pass " << pass_num << " Summary ────────────────────────────\n";
        cout << "  Input runs this pass  : " << run_files.size() << "\n";
        cout << "  Output runs this pass : " << next_runs.size() << "\n";
        if ((int)pass_log.size() > pass_num)
            cout << "  Pass I/O              : "
                 << pass_log[pass_num].seeks     << " seek(s)  "
                 << pass_log[pass_num].transfers << " transfer(s)\n";
        cout << "  Resulting file(s): ";
        for (int f : next_runs)
            cout << f << "(keys=" << disk_file_keys(f) << ") ";
        cout << "\n";
        print_sep('=');

        run_files = next_runs;
        pass_num++;
    }

    cout << "\n>>> Phase 2 complete. Final sorted file=" << run_files[0]
         << "  keys=" << disk_file_keys(run_files[0]) << " <<<\n";
    return run_files[0];
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN
// ═════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    if (argc < 6) {
        cerr << "Usage: " << argv[0]
             << " input-file.txt <n> <k> <b> <m>\n"
             << "  n = total keys\n"
             << "  k = key size in bytes\n"
             << "  b = disk block size in bytes\n"
             << "  m = available memory in blocks (>= 3)\n";
        return 1;
    }

    string input_file = argv[1];
    N = stoi(argv[2]);
    K = stoi(argv[3]);
    B = stoi(argv[4]);
    M = stoi(argv[5]);
    KEYS_PER_BLOCK = max(1, B / K);

    // ── Banner ────────────────────────────────────────────────────────────────
    cout << "╔══════════════════════════════════════════════════════════════════╗\n"
         << "║           External Merge Sort Simulation                        ║\n"
         << "║           CS315 — IIT Kanpur   Author: Prashant Shekhar        ║\n"
         << "╚══════════════════════════════════════════════════════════════════╝\n\n"
         << "Parameters received:\n"
         << "  Input file     : " << input_file << "\n"
         << "  Keys (n)       : " << N << "\n"
         << "  Key size (k)   : " << K << " bytes\n"
         << "  Block size (b) : " << B << " bytes\n"
         << "  Memory (m)     : " << M << " blocks\n"
         << "  Keys per block : " << KEYS_PER_BLOCK << "\n";

    if (M < 3) {
        cerr << "\nError: Need at least 3 memory blocks "
             << "(2 input + 1 output for merge).\n";
        return 1;
    }

    print_memory_layout_phase1();
    memory.resize(M);

    // ── Pre-pass: load input file into simulated disk ─────────────────────────
    ifstream fin(input_file);
    if (!fin) {
        cerr << "Error: Cannot open input file '" << input_file << "'\n";
        return 1;
    }

    int input_fid  = disk_new_file();
    vector<int> cur_blk;
    int val, load_blocks = 0;
    while (fin >> val) {
        cur_blk.push_back(val);
        if ((int)cur_blk.size() == KEYS_PER_BLOCK) {
            disk[input_fid].push_back(cur_blk);
            cur_blk.clear();
            load_blocks++;
        }
    }
    if (!cur_blk.empty()) {
        disk[input_fid].push_back(cur_blk);
        load_blocks++;
    }
    fin.close();

    // Account for the initial disk load (1 sequential write to simulated disk)
    long long pre_seeks     = 1;
    long long pre_transfers = load_blocks;
    total_seeks     += pre_seeks;
    total_transfers += pre_transfers;

    int actual_keys = disk_file_keys(input_fid);
    cout << "\n[Pre-Pass — Input File Load]\n"
         << "  " << actual_keys << " key(s) loaded into "
         << load_blocks << " disk block(s)\n"
         << "  I/O: 1 seek + " << load_blocks << " transfer(s)\n";
    print_file("  Input file", input_fid);
    print_sep('=');

    // ── Run the algorithm ─────────────────────────────────────────────────────
    vector<int> run_files = phase1_generate_runs(input_fid);

    int sorted_fid;
    if ((int)run_files.size() == 1) {
        cout << "\n>>> Only 1 run produced — no merge needed. <<<\n";
        sorted_fid = run_files[0];
    } else {
        sorted_fid = phase2_merge(run_files);
    }

    // ── Final sorted output ───────────────────────────────────────────────────
    print_sep('=');
    cout << "\n=== FINAL SORTED OUTPUT ===\n";
    print_file("Sorted file", sorted_fid);
    cout << "\nAll keys in sorted order: [";
    bool first = true;
    for (auto& blk : disk[sorted_fid])
        for (int v : blk) { if (!first) cout << ", "; cout << v; first = false; }
    cout << "]\n";

    // ── Pass-wise I/O Summary ─────────────────────────────────────────────────
    print_sep('=');
    cout << "\n=== PASS-WISE I/O SUMMARY ===\n";
    cout << left
         << setw(44) << "Pass"
         << setw(12) << "Seeks"
         << setw(14) << "Transfers" << "\n";
    print_sep('-', 70);

    // Pre-pass
    cout << setw(44) << "Pre-Pass  (Input File Load)"
         << setw(12) << pre_seeks
         << setw(14) << pre_transfers << "\n";

    // Phase 1
    if ((int)pass_log.size() > 0) {
        cout << setw(44) << "Pass 0  (Phase 1: Run Generation)"
             << setw(12) << pass_log[0].seeks
             << setw(14) << pass_log[0].transfers << "\n";
    }

    // Phase 2 merge passes
    for (int p = 1; p < (int)pass_log.size(); p++) {
        string lbl = "Pass " + to_string(p)
                   + "  (Phase 2: Merge pass " + to_string(p) + ")";
        cout << setw(44) << lbl
             << setw(12) << pass_log[p].seeks
             << setw(14) << pass_log[p].transfers << "\n";
    }

    print_sep('-', 70);
    cout << setw(44) << "TOTAL"
         << setw(12) << total_seeks
         << setw(14) << total_transfers << "\n";

    // ── Overall statistics ────────────────────────────────────────────────────
    int fan_in        = max(2, M - 1);
    int merge_passes  = max(0, (int)pass_log.size() - 1);
    int total_passes  = 1 + merge_passes;

    int initial_runs  = (int)run_files.size();   // after Phase 1
    int theory_passes = (initial_runs <= 1) ? 0
        : (int)ceil(log((double)initial_runs) / log((double)fan_in));

    long long total_disk_blocks = 0;
    for (auto& f : disk) total_disk_blocks += (int)f.size();

    cout << "\n=== OVERALL STATISTICS ===\n";
    cout << "  Number of partition passes         : "
         << (int)pass_log.size() << "\n";
    cout << "  Initial runs produced (Phase 1)    : " << initial_runs << "\n";
    cout << "  Fan-in used  (M-1)                 : " << fan_in << "\n";
    cout << "  Phase 2 merge passes               : " << merge_passes << "\n";
    cout << "  Total algorithm passes (P1 + P2)   : " << total_passes << "\n";
    cout << "  Theoretical merge passes needed    : " << theory_passes << "\n";
    cout << "  Total disk seeks                   : " << total_seeks << "\n";
    cout << "  Total disk transfers               : " << total_transfers << "\n";
    cout << "  Disk files created                 : " << disk.size() << "\n";
    cout << "  Total disk blocks used             : " << total_disk_blocks << "\n";

    return 0;
}

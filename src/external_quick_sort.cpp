/*
 * External Quicksort Simulation
 * ==============================
 * I am simulating disk-based external quicksort using two core data structures:
 *   - vector<vector<vector<int>>> disk  : I treat disk[file_id][block_idx] as a block of keys on disk
 *   - vector<vector<int>>       memory : I treat memory[slot] as one in-memory block slot
 *
 * Memory layout I maintain (M blocks total):
 *   Slot 0                  : I reserve this for storing the pivot value
 *   Slots 1 .. 1+IS-1       : I use these as my input read buffer  (IS = floor((M-1)/2) slots)
 *   Slots 1+IS .. 1+IS+LS-1 : I use these as my lo output buffer   (LS slots)
 *   Slots 1+IS+LS .. M-1    : I use these as my hi output buffer   (HS slots)
 *
 * Disk I/O model I follow:
 *   - When I read/write blocks sequentially on the SAME file, I charge:
 *       1 seek  +  (number of blocks) transfers
 *   - When I switch files or reverse direction, I charge a fresh seek.
 *   - So reading IS blocks at once from the input file = 1 seek + IS transfers.
 *   - When I flush lo or hi output buffers, each flush = 1 seek + batch_size transfers.
 *   - The 3 pivot probe reads are random (different positions), so each costs 1 seek.
 *
 * Pass definition I use:
 *   - All partition calls at the SAME recursion depth form ONE pass.
 *   - I count Pass 0 as the initial file-load from the input source.
 *   - Pass 1,2,... correspond to recursion depth 0,1,...
 *   - Base-case I/O is attributed to the same depth that triggered it,
 *     so it rolls into the correct pass row in the summary.
 *
 * Detailed per-node output I emit for each partition call:
 *   - The file I am partitioning and its key range (lo_ptr / hi_ptr positions)
 *   - The pivot I selected and how I chose it (median-of-3)
 *   - Each batch I read, and each flush I perform
 *   - The resulting lo and hi sub-files with their block and key counts
 *   - Running seeks and transfers for that node
 *
 * Usage:
 *   ./external_quicksort input-file.txt <n> <k> <b> <m>
 */

#include <bits/stdc++.h>
using namespace std;

// ─── Parameters I read from command line ────────────────────────────────────
int N, K, B, M;
int KEYS_PER_BLOCK;

// ─── Simulated disk I maintain ───────────────────────────────────────────────
vector<vector<vector<int>>> disk;

// ─── Simulated memory I maintain ─────────────────────────────────────────────
vector<vector<int>> memory;

// ─── Memory layout slots I compute once ──────────────────────────────────────
int MEM_PIVOT_SLOT;
int MEM_INPUT_START, MEM_INPUT_SLOTS;
int MEM_LO_START,    MEM_LO_SLOTS;
int MEM_HI_START,    MEM_HI_SLOTS;

// ─── I/O statistics I track per recursion depth ───────────────────────────────
// I key this map by depth: -1 = initial load, 0..D = partition passes
struct PassStats {
    long long seeks     = 0;
    long long transfers = 0;
    int       nodes     = 0;   // I count how many partition nodes fired at this depth
};

map<int, PassStats> pass_stats;
long long total_seeks     = 0;
long long total_transfers = 0;

// I record one I/O event into the correct depth bucket
void record_io(int depth, long long s, long long t) {
    pass_stats[depth].seeks     += s;
    pass_stats[depth].transfers += t;
    total_seeks                 += s;
    total_transfers             += t;
}

// ─── Disk file helpers I rely on ─────────────────────────────────────────────
int disk_new_file() {
    disk.push_back({});
    return (int)disk.size() - 1;
}
int disk_file_blocks(int fid) { return (int)disk[fid].size(); }
int disk_file_keys(int fid) {
    int c = 0;
    for (auto& b : disk[fid]) c += (int)b.size();
    return c;
}

// I read a sequential batch of 'count' blocks from file fid starting at start_blk.
// I charge: 1 seek + count transfers.
void disk_read_seq(int fid, int start_blk, int count, int mem_start, int depth) {
    for (int i = 0; i < count; i++)
        memory[mem_start + i] = disk[fid][start_blk + i];
    record_io(depth, 1, count);
}

// I write one block to fid with its own seek (I may interleave lo/hi writes).
// I charge: 1 seek + 1 transfer.
void disk_write_block(int fid, const vector<int>& blk, int depth) {
    disk[fid].push_back(blk);
    record_io(depth, 1, 1);
}

// ─── Pretty printing helpers I use throughout ─────────────────────────────────
void print_sep(char c = '-', int w = 76) { cout << string(w, c) << "\n"; }

void print_file(const string& lbl, int fid) {
    cout << lbl << " [file=" << fid
         << "  blks=" << disk_file_blocks(fid)
         << "  keys=" << disk_file_keys(fid) << "]: ";
    for (auto& blk : disk[fid]) {
        cout << "| ";
        for (int v : blk) cout << v << " ";
    }
    cout << "|\n";
}

// I print the current state of every active memory slot
void print_memory(const string& ind, int pivot, const vector<int>& lo_pend,
                  const vector<int>& hi_pend, int lo_ptr, int hi_ptr,
                  int total_keys) {
    cout << ind << "  ┌─ Memory State ─────────────────────────────────\n";
    cout << ind << "  │  Pivot slot       : " << pivot << "\n";
    cout << ind << "  │  Input buf slots  : " << MEM_INPUT_SLOTS << " slot(s)\n";
    cout << ind << "  │  Lo output pending: [";
    for (int i = 0; i < (int)lo_pend.size(); i++) { if(i) cout<<","; cout<<lo_pend[i]; }
    cout << "]  (" << lo_pend.size() << " keys)\n";
    cout << ind << "  │  Hi output pending: [";
    for (int i = 0; i < (int)hi_pend.size(); i++) { if(i) cout<<","; cout<<hi_pend[i]; }
    cout << "]  (" << hi_pend.size() << " keys)\n";
    cout << ind << "  │  lo_ptr (blocks scanned so far)   : " << lo_ptr << "\n";
    cout << ind << "  │  hi_ptr (total blocks in file)    : " << total_keys << " keys total\n";
    cout << ind << "  └───────────────────────────────────────────────\n";
}

// ─── Memory layout I compute once after reading M ────────────────────────────
void compute_memory_layout() {
    MEM_PIVOT_SLOT  = 0;
    MEM_INPUT_START = 1;
    MEM_INPUT_SLOTS = max(1, (M - 1) / 2);
    int out_slots   = (M - 1) - MEM_INPUT_SLOTS;
    MEM_LO_SLOTS    = max(1, out_slots / 2);
    MEM_HI_SLOTS    = max(1, out_slots - MEM_LO_SLOTS);
    MEM_LO_START    = MEM_INPUT_START + MEM_INPUT_SLOTS;
    MEM_HI_START    = MEM_LO_START + MEM_LO_SLOTS;

    cout << "\n=== Memory Layout I am Using ===\n";
    cout << "  Total blocks available  : " << M << "\n";
    cout << "  Keys fitting per block  : " << KEYS_PER_BLOCK << "\n";
    cout << "  Slot 0                  : pivot storage\n";
    cout << "  Slots " << MEM_INPUT_START << ".."
         << MEM_INPUT_START + MEM_INPUT_SLOTS - 1
         << "  (" << MEM_INPUT_SLOTS << " slot(s))  : input read buffer\n";
    cout << "  Slots " << MEM_LO_START << ".."
         << MEM_LO_START + MEM_LO_SLOTS - 1
         << "  (" << MEM_LO_SLOTS << " slot(s))  : lo output buffer\n";
    cout << "  Slots " << MEM_HI_START << ".."
         << MEM_HI_START + MEM_HI_SLOTS - 1
         << "  (" << MEM_HI_SLOTS << " slot(s))  : hi output buffer\n";
    print_sep();
}

// I flush whatever remains in a pending vector to disk as a partial block
void flush_pending(int fid, vector<int>& pending, int depth) {
    if (!pending.empty()) {
        disk_write_block(fid, pending, depth);
        pending.clear();
    }
}

// ─── Forward declaration ──────────────────────────────────────────────────────
int external_quicksort(int fid, int depth);

// ─── Base-case in-memory sort I perform when a sub-file fits in memory ────────
int inmemory_sort(int fid, int depth) {
    string ind(depth * 2, ' ');
    int nb    = disk_file_blocks(fid);
    int nkeys = disk_file_keys(fid);

    cout << ind << "  ╔══ [Base-Case Sort] file=" << fid
         << "  blocks=" << nb << "  keys=" << nkeys << "\n";

    // I read all blocks sequentially: 1 seek + nb transfers total
    vector<int> all;
    for (int i = 0; i < nb; i++) {
        disk_read_seq(fid, i, 1, MEM_INPUT_START, depth);
        for (int v : memory[MEM_INPUT_START]) all.push_back(v);
    }
    // I fix the seek count: nb separate reads on one file = still 1 seek
    if (nb > 1) { pass_stats[depth].seeks -= (nb-1); total_seeks -= (nb-1); }

    sort(all.begin(), all.end());
    cout << ind << "  ║  I/O: 1 seek + " << nb << " transfers  (sequential read)\n";
    cout << ind << "  ║  Sorted keys: [";
    for (int i = 0; i < (int)all.size(); i++) { if (i) cout << ", "; cout << all[i]; }
    cout << "]\n";

    // I write sorted output sequentially: 1 seek + ceil blocks transfers
    int out = disk_new_file();
    vector<vector<int>> out_blks;
    for (int pos = 0; pos < (int)all.size(); ) {
        int end = min(pos + KEYS_PER_BLOCK, (int)all.size());
        out_blks.push_back(vector<int>(all.begin()+pos, all.begin()+end));
        pos = end;
    }
    for (auto& blk : out_blks) disk[out].push_back(blk);
    if (!out_blks.empty()) record_io(depth, 1, (long long)out_blks.size());

    cout << ind << "  ║  I/O: 1 seek + " << out_blks.size()
         << " transfers  (sequential write to file=" << out << ")\n";
    print_file(ind + "  ║  Result", out);
    cout << ind << "  ╚══ Base-Case done → file=" << out << "\n";
    return out;
}

// ─── Core external quicksort node ────────────────────────────────────────────
int external_quicksort(int fid, int depth) {
    string ind(depth * 2, ' ');
    int nblocks = disk_file_blocks(fid);
    int nkeys   = disk_file_keys(fid);

    cout << "\n" << ind
         << "╔════════════════════════════════════════════════════════\n"
         << ind << "║ QSORT NODE  depth=" << depth
         << "  file=" << fid
         << "  blocks=" << nblocks
         << "  keys=" << nkeys << "\n"
         << ind << "║ lo_ptr=block[0]  hi_ptr=block[" << nblocks-1 << "]\n"
         << ind << "╚════════════════════════════════════════════════════════\n";

    // ── Base case: the sub-file fits entirely in memory ───────────────────
    if (nblocks <= M) {
        cout << ind << "  → Sub-file fits in memory (blocks=" << nblocks
             << " ≤ M=" << M << "). Routing to base-case sort.\n";
        int sid = inmemory_sort(fid, depth);
        cout << ind << "  └─ Returning sorted file=" << sid << "\n";
        return sid;
    }

    // ── I record this as a partition node at this depth ────────────────────
    pass_stats[depth].nodes++;

    cout << ind << "  ─── Partition Phase ───────────────────────────────────\n";
    cout << ind << "  File to partition: file=" << fid
         << "  total_blocks=" << nblocks
         << "  total_keys=" << nkeys << "\n";

    // ── Pivot selection: median-of-3 on first, middle, last block ──────────
    // I perform 3 random reads (different positions) → 3 seeks, 3 transfers
    disk_read_seq(fid, 0,         1, MEM_PIVOT_SLOT,  depth); int a = memory[MEM_PIVOT_SLOT][0];
    disk_read_seq(fid, nblocks/2, 1, MEM_INPUT_START, depth); int b = memory[MEM_INPUT_START][0];
    disk_read_seq(fid, nblocks-1, 1, MEM_INPUT_START, depth); int c = memory[MEM_INPUT_START][0];
    int pivot = max(min(a,b), min(max(a,b),c));
    memory[MEM_PIVOT_SLOT] = {pivot};

    cout << ind << "  Pivot selection (median-of-3):\n"
         << ind << "    block[0]=" << a
         << "  block[" << nblocks/2 << "]=" << b
         << "  block[" << nblocks-1 << "]=" << c << "\n"
         << ind << "    → PIVOT = " << pivot
         << "  (I/O: 3 seeks + 3 transfers for 3 random probes)\n";

    int lo_file = disk_new_file();
    int hi_file = disk_new_file();
    vector<int> lo_pend, hi_pend;
    int lo_count = 0, hi_count = 0;

    cout << ind << "  lo_file=" << lo_file << "  hi_file=" << hi_file << "\n";
    cout << ind << "  ─── Scanning input file block by block ───────────────\n";

    // ── I scan the input file in batches of MEM_INPUT_SLOTS blocks ──────────
    int blk_idx   = 0;   // I use this as my current read pointer into the file
    int batch_num = 0;

    while (blk_idx < nblocks) {
        int batch = min(MEM_INPUT_SLOTS, nblocks - blk_idx);
        batch_num++;

        // I read the next batch sequentially: 1 seek + batch transfers
        disk_read_seq(fid, blk_idx, batch, MEM_INPUT_START, depth);

        // I show the state of the pointer before and after this batch
        cout << ind << "  [Batch #" << batch_num << "]"
             << "  read_ptr: block[" << blk_idx << "]→block["
             << blk_idx+batch-1 << "]"
             << "  (" << batch << " block(s), 1 seek + " << batch << " transfers)\n";
        blk_idx += batch;

        // I partition keys from the loaded batch into lo and hi pending buffers
        int batch_lo = 0, batch_hi = 0;
        for (int s = 0; s < batch; s++) {
            for (int v : memory[MEM_INPUT_START + s]) {
                if (v <= pivot) { lo_pend.push_back(v); lo_count++; batch_lo++; }
                else            { hi_pend.push_back(v); hi_count++; batch_hi++; }
            }
            memory[MEM_INPUT_START + s].clear();
        }
        cout << ind << "         → " << batch_lo << " key(s) routed to lo_pending"
             << "  " << batch_hi << " key(s) to hi_pending\n";

        // I flush lo output buffer when I've accumulated MEM_LO_SLOTS full blocks
        while ((int)lo_pend.size() >= KEYS_PER_BLOCK * MEM_LO_SLOTS) {
            vector<vector<int>> out_blks;
            for (int s = 0; s < MEM_LO_SLOTS; s++)
                out_blks.push_back(vector<int>(
                    lo_pend.begin() + s*KEYS_PER_BLOCK,
                    lo_pend.begin() + (s+1)*KEYS_PER_BLOCK));
            for (auto& blk : out_blks) disk[lo_file].push_back(blk);
            record_io(depth, 1, (long long)MEM_LO_SLOTS);
            lo_pend.erase(lo_pend.begin(), lo_pend.begin() + KEYS_PER_BLOCK*MEM_LO_SLOTS);
            cout << ind << "         [Flush LO] wrote " << MEM_LO_SLOTS
                 << " block(s) to file=" << lo_file
                 << "  (1 seek + " << MEM_LO_SLOTS << " transfers)\n";
        }

        // I flush hi output buffer when I've accumulated MEM_HI_SLOTS full blocks
        while ((int)hi_pend.size() >= KEYS_PER_BLOCK * MEM_HI_SLOTS) {
            vector<vector<int>> out_blks;
            for (int s = 0; s < MEM_HI_SLOTS; s++)
                out_blks.push_back(vector<int>(
                    hi_pend.begin() + s*KEYS_PER_BLOCK,
                    hi_pend.begin() + (s+1)*KEYS_PER_BLOCK));
            for (auto& blk : out_blks) disk[hi_file].push_back(blk);
            record_io(depth, 1, (long long)MEM_HI_SLOTS);
            hi_pend.erase(hi_pend.begin(), hi_pend.begin() + KEYS_PER_BLOCK*MEM_HI_SLOTS);
            cout << ind << "         [Flush HI] wrote " << MEM_HI_SLOTS
                 << " block(s) to file=" << hi_file
                 << "  (1 seek + " << MEM_HI_SLOTS << " transfers)\n";
        }

        // I show the current pending buffer state after each batch
        print_memory(ind, pivot, lo_pend, hi_pend, blk_idx, blk_idx, nkeys);
    }

    // I perform the final flush of any remaining partial blocks
    if (!lo_pend.empty()) {
        flush_pending(lo_file, lo_pend, depth);
        cout << ind << "  [Final Flush LO] wrote partial block to file="
             << lo_file << "  (1 seek + 1 transfer)\n";
    }
    if (!hi_pend.empty()) {
        flush_pending(hi_file, hi_pend, depth);
        cout << ind << "  [Final Flush HI] wrote partial block to file="
             << hi_file << "  (1 seek + 1 transfer)\n";
    }

    // ── Anti-infinite-loop guard I apply for all-equal-key inputs ────────────
    if (disk_file_keys(hi_file) == 0 && disk_file_keys(lo_file) == nkeys) {
        cout << ind << "  [Guard] All keys ≤ pivot → no progress possible. Forcing base sort.\n";
        int sid = inmemory_sort(lo_file, depth);
        cout << ind << "  └─ Returning sorted file=" << sid << "\n";
        return sid;
    }

    cout << ind << "  ─── Partition Result ──────────────────────────────────\n"
         << ind << "  lo: " << lo_count << " key(s) in " << disk_file_blocks(lo_file)
         << " block(s) → file=" << lo_file << "\n"
         << ind << "  hi: " << hi_count << " key(s) in " << disk_file_blocks(hi_file)
         << " block(s) → file=" << hi_file << "\n"
         << ind << "  pivot=" << pivot << "  (all lo keys ≤ pivot < all hi keys)\n";
    print_file(ind + "  LO", lo_file);
    print_file(ind + "  HI", hi_file);

    // ── I recurse on lo and hi sub-files at depth+1 ───────────────────────
    int sorted_lo = -1, sorted_hi = -1;
    if (disk_file_keys(lo_file) > 0) sorted_lo = external_quicksort(lo_file, depth+1);
    if (disk_file_keys(hi_file) > 0) sorted_hi = external_quicksort(hi_file, depth+1);

    // ── I concatenate sorted lo and hi into a single output file ─────────
    // I read each sorted sub-file sequentially and write it out:
    //   each copy: 1 seek (read) + nb transfers + 1 seek (write) + nb transfers
    int out_file = disk_new_file();
    cout << "\n" << ind << "  ─── Concatenating Results ────────────────────────────\n"
         << ind << "  lo(file=" << sorted_lo << ") ++ hi(file="
         << sorted_hi << ") → output file=" << out_file << "\n";

    auto copy_seq = [&](int src) {
        if (src < 0) return;
        int nb = disk_file_blocks(src);
        if (nb == 0) return;
        for (auto& blk : disk[src]) disk[out_file].push_back(blk);
        record_io(depth, 1, nb);  // I charge 1 seek + nb transfers to read src
        record_io(depth, 1, nb);  // I charge 1 seek + nb transfers to write out_file
        cout << ind << "  Copied file=" << src << "  " << nb
             << " block(s)  (1 seek + " << nb
             << " transfers read,  1 seek + " << nb << " transfers write)\n";
    };
    copy_seq(sorted_lo);
    copy_seq(sorted_hi);

    print_file(ind + "  Output", out_file);
    cout << ind << "  └─ Returning sorted file=" << out_file << "\n";
    return out_file;
}

// ─── Main entry point ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 6) {
        cerr << "Usage: " << argv[0] << " input-file.txt <n> <k> <b> <m>\n";
        return 1;
    }
    string input_file = argv[1];
    N = stoi(argv[2]); K = stoi(argv[3]);
    B = stoi(argv[4]); M = stoi(argv[5]);
    KEYS_PER_BLOCK = max(1, B / K);

    cout << "╔══════════════════════════════════════════════════════════════════╗\n"
         << "║           External Quicksort Simulation                         ║\n"
         << "╚══════════════════════════════════════════════════════════════════╝\n\n"
         << "Parameters I received:\n"
         << "  Input file     : " << input_file << "\n"
         << "  Keys (n)       : " << N << "\n"
         << "  Key size (k)   : " << K << " bytes\n"
         << "  Block size (b) : " << B << " bytes\n"
         << "  Memory (m)     : " << M << " blocks\n"
         << "  Keys per block : " << KEYS_PER_BLOCK << "\n";

    if (M < 4) { cerr << "Error: I need at least 4 memory blocks.\n"; return 1; }

    compute_memory_layout();
    memory.resize(M);

    // ── I load the input file into disk (Pass 0) ──────────────────────────
    ifstream fin(input_file);
    if (!fin) { cerr << "I cannot open " << input_file << "\n"; return 1; }

    int input_fid = disk_new_file();  // I assign file_id = 0 to the raw input
    vector<int> cur_blk;
    int key, load_blocks = 0;
    while (fin >> key) {
        cur_blk.push_back(key);
        if ((int)cur_blk.size() == KEYS_PER_BLOCK) {
            disk[input_fid].push_back(cur_blk);
            cur_blk.clear(); load_blocks++;
        }
    }
    if (!cur_blk.empty()) { disk[input_fid].push_back(cur_blk); load_blocks++; }
    fin.close();

    // I charge Pass 0 as 1 seek + load_blocks transfers (sequential write to disk)
    record_io(-1, 1, load_blocks);

    int actual_keys = disk_file_keys(input_fid);
    cout << "\n[Pass 0 - Initial Load]  "
         << actual_keys << " keys loaded into "
         << load_blocks << " disk blocks  (1 seek + "
         << load_blocks << " transfers)\n";
    print_file("Input file", input_fid);
    print_sep('=');

    // ── I run external quicksort ──────────────────────────────────────────
    cout << "\n>>> I am starting External Quicksort <<<\n";
    int sorted_fid = external_quicksort(input_fid, 0);

    // ── I print the final sorted output ──────────────────────────────────
    print_sep('=');
    cout << "\n=== FINAL SORTED OUTPUT ===\n";
    print_file("Sorted file", sorted_fid);
    cout << "\nAll keys in order: [";
    bool first = true;
    for (auto& blk : disk[sorted_fid])
        for (int v : blk) { if (!first) cout << ", "; cout << v; first = false; }
    cout << "]\n";

    // ── I print the pass-wise I/O summary ────────────────────────────────
    print_sep('=');
    cout << "\n=== PASS-WISE I/O SUMMARY ===\n";
    cout << left
         << setw(38) << "Pass"
         << setw(8)  << "Nodes"
         << setw(12) << "Seeks"
         << setw(14) << "Transfers" << "\n";
    print_sep('-', 72);

    int max_depth = -1;
    for (auto& [d, _] : pass_stats)
        if (d != INT_MAX && d > max_depth) max_depth = d;

    // I print Pass 0 (initial load)
    if (pass_stats.count(-1)) {
        auto& s = pass_stats[-1];
        cout << setw(38) << "Pass 0  (Initial Load)"
             << setw(8)  << "-"
             << setw(12) << s.seeks
             << setw(14) << s.transfers << "\n";
    }

    // I print each partition depth as one pass
    for (int d = 0; d <= max_depth; d++) {
        if (!pass_stats.count(d)) continue;
        auto& s = pass_stats[d];
        string lbl = "Pass " + to_string(d+1)
                   + "  (depth " + to_string(d) + ", "
                   + to_string(s.nodes) + " partition node(s)"
                   + (s.nodes == 0 ? " - base only)" : ")");
        cout << setw(38) << lbl
             << setw(8)  << s.nodes
             << setw(12) << s.seeks
             << setw(14) << s.transfers << "\n";
    }

    print_sep('-', 72);
    cout << setw(38) << "TOTAL"
         << setw(8)  << "-"
         << setw(12) << total_seeks
         << setw(14) << total_transfers << "\n";

    // ── I print the final overall statistics ─────────────────────────────
    cout << "\n=== OVERALL STATISTICS ===\n";
    cout << "  Total passes incl. initial load  : " << (max_depth + 2) << "\n";
    cout << "  Total partition passes           : " << (max_depth + 1) << "\n";
    cout << "  Total disk seeks                 : " << total_seeks << "\n";
    cout << "  Total disk transfers             : " << total_transfers << "\n";
    cout << "  Disk files created               : " << disk.size() << "\n";
    long long tb = 0;
    for (auto& f : disk) tb += (int)f.size();
    cout << "  Disk blocks used (total)         : " << tb << "\n";

    return 0;
}
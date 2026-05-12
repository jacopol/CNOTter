// Unified CNOT Search Algorithm
// Supports both small (uint64_t, N≤8) and large (Matrix, N>8) matrix representations
// 
// Compile with -DUSE_SMALL_MATRIX or -DUSE_LARGE_MATRIX to select matrix type
// 
// Jaco van de Pol, Aarhus University, September 2024 - 2025

// ============================================================================
// STEP 1: Select matrix representation via compile flags
// ============================================================================

#if !defined(USE_SMALL_MATRIX) && !defined(USE_LARGE_MATRIX)
#error "Define either -DUSE_SMALL_MATRIX or -DUSE_LARGE_MATRIX at compile time"
#endif

static_assert(N > 0 && N <= 20, "N must be between 1 and 20");

#ifdef USE_SMALL_MATRIX
    #include "matrix.h"           // Defines: uint64_t ops, read_matrix(), pretty_matrix()
    #include "repr.h"             // Defines: representative(matrix) -> counter
    #include "trace_back.h"       // Defines: findPath(), equivalent matrix tracing
    using MatrixImpl = uint64_t;
    #define USED_REPRESENTATION "Small Matrix (uint64_t, N≤8)"
    
#else  // USE_LARGE_MATRIX
    #include "matrixN.h"          // Defines: Matrix class with bit-packing
    #include "reprN.h"            // Defines: representative(Matrix) -> counter
    #include "trace_backN.h"      // Defines: Matrix tracing functions
    #include "tree.h"             // Defines: CONTAINS/INSERT/GET wrappers for Matrix storage
    using MatrixImpl = Matrix;
    #define USED_REPRESENTATION "Large Matrix (Matrix class, N>8)"
#endif

// ============================================================================
// STEP 2: Common includes (independent of matrix type)
// ============================================================================

#include <array>
#include <vector>
#include <omp.h>
#include <atomic>
#include <chrono>
#include "options.h"              // Defines: N, E, MAX, SWAP, NAUTY, POLY, BEAT
#include "timing.h"               // Defines: lifeBeat(), passedTime()
#include "hashset.h"              // Defines: thread-safe hash set
#include "matrix_trait.h"          // Defines: MatrixTrait<MatrixImpl>
//#include "matrix_trait_impl.h"    // Defines: template implementations
#include <algorithm>            // std::min, std::max

// ============================================================================
// STEP 4: Global data structures (same for both matrix types)
// ============================================================================

using Trait = MatrixTrait<MatrixImpl>;
using StorageType = typename Trait::StorageType;
using counter = uint64_t;

// Pre-calculated 2-log of orbit level sizes at each search depth
// NOTE: These depend on SWAP setting
#if SWAP == 0
const std::array<std::vector<uint8_t>, 11> levelSizes = {{
    {}, {0}, {0,0,0,0}, {0,3,4,4,3,0,0},
    {0,3,5,7,8,9,8,5,0,0}, {0,3,5,8,11,13,14,15,15,13,8,0,0},
    {0,3,6,8,11,14,17,19,22,23,24,23,20,11,0,0},
    {0,3,6,8,11,15,18,21,24,27,30,32,33,34,33,29,17,0,0},
    {0,3,6,8,11,15,18,22,25,29,32,34,37,38,40,41,40,38,36,34,0,0},
    {0,3,6,8,11,15,18,22,26,30,33,35,37,38,40,41,40,38,36,34,0,0},
    {0,3,6,8,11,15,18,22,26,30,34,36,37,38,40,41,40,38,36,34,0,0}
}};
#else
const std::array<std::vector<uint8_t>, 11> levelSizes = {{
    {}, {0}, {0,0,0,0}, {0,3,4,4,3,0,0},
    {0,3,5,5,3,0,0,0,0,0}, {0,3,5,7,9,9,7,3,0,0,0,0,0},
    {0,3,5,8,10,13,14,15,13,10,3,0,0,0,0,0},
    {0,3,5,8,11,14,16,19,21,22,22,20,13,2,0,0,0,0,0},
    {0,3,5,8,11,14,17,20,23,26,28,30,31,30,28,21,3,0,0,0,0,0},
    {0,3,5,8,11,14,17,21,24,28,31,34,34,34,34,34,34},
    {0,3,5,8,11,14,17,21,24,28,32,34,34,34,34,34,34}
}};
#endif

// Polynomial coefficients at each distance
std::array<std::array<std::atomic<counter>, N+1>, N/2+1> poly;

// BFS level storage
hashset bfs_levels[3*N];        // for one-directional BFS
hashset bfs_fwd[(3*N+2)/2];     // for bi-directional BFS forward
hashset bfs_bwd[(3*N+1)/2];     // for bi-directional BFS backward

// Timing for heartbeat reporting
#if BEAT > 0
std::vector<std::chrono::system_clock::time_point> lifeTime;
#endif

// ============================================================================
// STEP 5: Algorithm functions - unified via MatrixTrait
// ============================================================================

inline MatrixImpl null_matrix() {
#ifdef USE_SMALL_MATRIX
    return 0;
#else
    return Matrix(false);
#endif
}

inline bool level_contains(hashset& level, const MatrixImpl& x) {
#ifdef USE_SMALL_MATRIX
    return level.contains(x);
#else
    return CONTAINS(x, level);
#endif
}

inline bool level_insert(hashset& level, const MatrixImpl& x) {
#ifdef USE_SMALL_MATRIX
    return level.insert(x);
#else
    return INSERT(x, level);
#endif
}

template<typename Fn>
inline void level_for_each_matrix(hashset& level, Fn&& fn) {
#ifdef USE_SMALL_MATRIX
    level.parallelForAll([&](const uint64_t& x) {
        fn(x);
    });
#else
    level.parallelForAll([&](const mat_idx& root) {
        if constexpr (NR == 1) {
            MatrixImpl x;
            x._bits[0] = root;
            fn(x);
        } else {
            fn(GET(root));
        }
    });
#endif
}

// Predict hash table size for a given depth
inline uint8_t predictSize(int depth) {
    uint8_t lookup = levelSizes[std::min(N, 10)][depth - 2];
    return std::min(std::max(lookup + E, 3), MAX);
}

// Compute orbit size from stabilizer size
inline counter compute_orbit_size(counter stabilizer) {
    if constexpr (SWAP == 0) {
        return fac_N / stabilizer;
    } else {
        return (fac_N * fac_N) / stabilizer;
    }
}

// Process a single CNOT operation
// Returns true if a new canonical form was discovered
inline void process_cnot(const MatrixImpl& x, uint64_t row_i, uint8_t j,
                        hashset& prev_level, hashset& curr_level,
                        hashset& next_level,
                        counter& orbit_sum, counter& matrix_count,
                        uint32_t depth) {
    
    MatrixImpl y = Trait::add_row(x, row_i, j);
    counter Stab = representative(y);  // Modifies y in-place to canonical form
    
    // Check if we've seen this canonical form before
    if (!level_contains(prev_level, y) && !level_contains(curr_level, y) &&
        level_insert(next_level, y)) {
        // New canonical form found - update counters
        orbit_sum += compute_orbit_size(Stab);
        matrix_count++;
        
        if constexpr (POLY == 1) {
            if (2 * (depth - 1) <= N) {
                uint8_t ess = countEssential(y);
                poly[depth - 1][ess] += (fac[ess] * fac[N - ess]) / Stab;
            }
        }
    }
}

// Initialize BFS level with starting matrix
counter init_level(hashset levels[], MatrixImpl start) {
    levels[0] = hashset();  // level 0 (previous, empty)
    levels[0].init(3);
    levels[1] = hashset();  // level 1 (current)
    levels[1].init(3);
    
    counter Stab = representative(start);  // Modifies start to canonical form
    level_insert(levels[1], start);
    
    return compute_orbit_size(Stab);
}

// Explore and count all successors of the current level
// Returns total orbit size at this level; outputs matrix count in size reference
counter next_level(counter& size, hashset levels[], uint32_t depth) {
    std::atomic<counter> level(0);
    std::atomic<counter> count(0);
    
    // Padded struct to avoid false sharing between threads
    struct alignas(64) PaddedCounter {
        counter value;
    };
    
    const int max_threads = omp_get_max_threads();
    std::vector<PaddedCounter> thread_levels(max_threads, {0});
    std::vector<PaddedCounter> thread_counts(max_threads, {0});
    
    // Cache layer references
    hashset& prev_level = levels[depth - 2];
    hashset& curr_level = levels[depth - 1];
    hashset& next_level = levels[depth];
    
    // Hoist computation
    const bool compute_poly = (POLY == 1) && (2 * (depth - 1) <= N);
    (void)compute_poly;
    
    // Parallel iteration over all matrices at current level
    level_for_each_matrix(curr_level, [&](const MatrixImpl& x) {
        int tid = omp_get_thread_num();
        counter& orbit_sum = thread_levels[tid].value;
        counter& matrix_count = thread_counts[tid].value;
        
        // Generate all N(N-1) CNOT successors
        for (uint8_t i = 0; i < N; i++) {
            // Extract row i once for all column targets j
            uint64_t row_i = Trait::extract_row(x, i);
            
            for (uint8_t j = 0; j < N; j++) {
                if (i != j) {
                    process_cnot(x, row_i, j, prev_level, curr_level, next_level,
                                orbit_sum, matrix_count, depth);
                }
            }
        }
        
        // Periodic heartbeat reporting
#if BEAT > 0
        if (passedTime(lifeTime[tid]) >= BEAT) {
            #pragma omp critical
            {
                lifeBeat(tid, thread_levels[tid].value, thread_counts[tid].value);
            }
            lifeTime[tid] = std::chrono::system_clock::now();
        }
#endif
    });
    
    // Aggregate results from all threads
    for (int i = 0; i < max_threads; i++) {
        if (thread_levels[i].value > 0) {
            level += thread_levels[i].value;
            count += thread_counts[i].value;
        }
    }
    
    size = count;
    return level;
}

// Find a matrix in a level (used for goal checking)
bool level_has_matrix(const MatrixImpl& goal, hashset& level) {
    MatrixImpl canonical_goal = goal;
    representative(canonical_goal);
    return level_contains(level, canonical_goal);
}

// Forward iterative deepening search
int generate_bfs(const MatrixImpl& start, const MatrixImpl& goal,
                uint8_t limit, hashset bfs_levels[], bool has_goal) {
    
    byte depth = 1;
    counter level, levels, orbit, orbits;
    orbit = orbits = 1;
    
    fprintf(stderr, "[%s]\n", USED_REPRESENTATION);
    fprintf(stderr, "Depth 0 (2^3): ");
    fflush(stderr);
    
    levels = level = init_level(bfs_levels, start);
    
    while (orbit) {
        report(level, orbit);
        
        // Check for goal
        if (has_goal) {
            if (level_has_matrix(goal, bfs_levels[depth]))
                return -depth;
        }
        
        // Cleanup old levels to save memory
        if (!has_goal && depth > 1) bfs_levels[depth - 2].deinit();
        
        // Stopping condition
        if (depth - 1 == limit) return depth;
        
        // Prepare next level
        depth++;
        uint8_t tableSize = predictSize(depth);
        bfs_levels[depth] = hashset();
        bfs_levels[depth].init(tableSize);
        fprintf(stderr, "Depth %u (2^%u): ", depth - 1, tableSize);
        fflush(stderr);
        
        // Explore next level
        levels += level = next_level(orbit, bfs_levels, depth);
        orbits += orbit;
    }
    
    depth--;
    fprintf(stderr, "--\n");
    fprintf(stderr, "Total size: %lu (%lu orbits), completed at depth %u\n",
            levels, orbits, depth - 1);
    
    return depth;
}

// Find a matrix in intersection of two levels
MatrixImpl intersect(hashset& L1, hashset& L2) {
    MatrixImpl joint = null_matrix();
    std::atomic<bool> found(false);
    
    level_for_each_matrix(L1, [&](const MatrixImpl& x) {
        if (!found.load(std::memory_order_relaxed) && level_contains(L2, x)) {
            #pragma omp critical
            {
                if (!found.load(std::memory_order_relaxed)) {
                    joint = x;
                    found.store(true, std::memory_order_relaxed);
                }
            }
        }
    });
    
    return joint;
}

// Bidirectional search
using triple = std::pair<MatrixImpl, std::pair<uint8_t, uint8_t>>;

triple bidirectional(const MatrixImpl& start, const MatrixImpl& goal,
                    uint8_t limit, hashset bfs_fwd[], hashset bfs_bwd[]) {
    
    byte fdepth = 1, bdepth = 1;
    counter level, forbit, borbit, levels, orbits;
    forbit = borbit = 1;
    orbits = 2;
    
    fprintf(stderr, "[%s - Bidirectional Search]\n", USED_REPRESENTATION);
    
    // Initialize forward and backward searches
    levels = level = init_level(bfs_fwd, start);
    fprintf(stderr, "Fwd Depth 0 (2^3): ");
    report(level, forbit);
    
    levels += level = init_level(bfs_bwd, goal);
    fprintf(stderr, "Bwd Depth 0 (2^3): ");
    report(level, borbit);
    
    // Check initial intersection
    MatrixImpl m = intersect(bfs_fwd[fdepth], bfs_bwd[bdepth]);
    if (!Trait::equals(m, null_matrix()))
        return {m, {fdepth, bdepth}};
    
    // Alternating expansion of smallest frontier
    while (fdepth + bdepth - 2 < 3 * (N - 1)) {
        if (fdepth + bdepth - 2 == limit)
            return {m, {fdepth, bdepth}};
        
        if (forbit <= borbit) {
            // Expand forward
            fdepth++;
            uint8_t tableSize = predictSize(fdepth);
            fprintf(stderr, "Fwd Depth %u (2^%u): ", fdepth - 1, tableSize);
            fflush(stderr);
            bfs_fwd[fdepth] = hashset();
            bfs_fwd[fdepth].init(tableSize);
            levels += level = next_level(forbit, bfs_fwd, fdepth);
            orbits += forbit;
            report(level, forbit);
        } else {
            // Expand backward
            bdepth++;
            uint8_t tableSize = predictSize(fdepth + 1);
            fprintf(stderr, "Bwd Depth %u (2^%u): ", bdepth - 1, tableSize);
            fflush(stderr);
            bfs_bwd[bdepth] = hashset();
            bfs_bwd[bdepth].init(tableSize);
            levels += level = next_level(borbit, bfs_bwd, bdepth);
            orbits += borbit;
            report(level, borbit);
        }
        
        // Check intersection at current frontier
        m = intersect(bfs_fwd[fdepth], bfs_bwd[bdepth]);
        if (!Trait::equals(m, null_matrix()))
            return {m, {fdepth, bdepth}};
    }
    
    return {m, {fdepth, bdepth}};
}

// ============================================================================
// STEP 6: Main entry point
// ============================================================================

struct ProgramOptions {
    uint8_t limit;
    MatrixImpl goal;
    bool has_goal;
};

ProgramOptions parse_arguments(int argc, char* argv[]) {
    ProgramOptions opts;
    opts.limit = static_cast<uint8_t>(-1);  // 255 means no limit
    opts.goal = null_matrix();
    opts.has_goal = false;
    
    if (argc > 1 && argv[1][0] == '-') {
        opts.limit = static_cast<uint8_t>(atoi(argv[1] + 1));
        if (opts.limit != static_cast<uint8_t>(-1)) {
            fprintf(stderr, "Cutting off at maximum distance: %u\n", opts.limit);
        }
    }
    
    if (argc > 1 && argv[argc - 1][0] != '-') {
        opts.goal = Trait::read_matrix(argv[argc - 1]);
        fprintf(stderr, "Searching for goal: %s\n", argv[argc - 1]);
        opts.has_goal = true;
#ifdef USE_SMALL_MATRIX
        assert(opts.goal != 0 && "0-matrix cannot be generated");
#endif
    }
    
    return opts;
}

void print_configuration() {
    fprintf(stderr, "[%s]\n", USED_REPRESENTATION);
    fprintf(stderr, "Handling matrices of size N = %u\n", N);
    fprintf(stderr, "Using DTree + %u extra bits, max-size %u\n", E, MAX);
    fprintf(stderr, "Use Nauty: %u. Swaps-for-free: %u. Polynomial: %u\n", NAUTY, SWAP, POLY);
#if defined(_OPENMP)
    fprintf(stderr, "Running with %d OpenMP threads\n", omp_get_max_threads());
#endif
}

void run_bidirectional_search(const MatrixImpl& id, const MatrixImpl& goal, uint8_t limit) {
    triple result = bidirectional(id, goal, limit, bfs_fwd, bfs_bwd);
    MatrixImpl middle = result.first;
    int fdepth = result.second.first;
    int bdepth = result.second.second;
    
    if (!Trait::equals(middle, null_matrix())) {
        fprintf(stderr, "Found at distance %u (%u + %u)\n",
                fdepth + bdepth - 2, fdepth - 1, bdepth - 1);
        perm pi;
#ifdef USE_SMALL_MATRIX
        trace concat = trace_back_middle(id, middle, goal, bfs_fwd, bfs_bwd, fdepth, bdepth, pi);
#else
        MatrixImpl id_copy = id;
        MatrixImpl middle_copy = middle;
        MatrixImpl goal_copy = goal;
        trace concat = trace_back_middle(id_copy, middle_copy, goal_copy,
                                         bfs_fwd, bfs_bwd, fdepth, bdepth, pi);
#endif
        print_trace(id, goal, concat, pi);
    } else {
        fprintf(stderr, "Goal not found after %d steps:\n", fdepth + bdepth - 2);
        Trait::pretty_print(goal);
    }
}

void print_polynomials(int depth) {
    fprintf(stderr, "Polynomial coefficients (N=%u):\n", N);
    for (int d = 1; d <= std::min(N / 2, depth - 1); d++) {
        fprintf(stderr, "d=%u: [", d);
        for (uint8_t i = 0; i <= 2 * d; i++) {
            fprintf(stderr, "%lu%c ",
                    poly[d][i].load(std::memory_order_relaxed),
                    (i < 2 * d ? ',' : ']'));
        }
        fprintf(stderr, "\n");
    }
}

void reconstruct_trace(int depth, const MatrixImpl& goal, const MatrixImpl& id) {
    if (depth < 0) {
        int real_depth = -depth;
        fprintf(stderr, "Goal found at level %d\n", real_depth - 1);
        trace bfs_trace;
        MatrixImpl other = trace_back(goal, bfs_levels, real_depth, bfs_trace);
        assert(Trait::equals(other, id));
        std::reverse(bfs_trace.begin(), bfs_trace.end());
        perm pi;
        id_perm(pi);
        print_trace(other, goal, bfs_trace, pi);
    } else {
        fprintf(stderr, "Goal not found after %d steps:\n", depth - 1);
        Trait::pretty_print(goal);
    }
}

void run_full_bfs(const MatrixImpl& id, const ProgramOptions& opts) {
    int depth = generate_bfs(id, opts.goal, opts.limit, bfs_levels, opts.has_goal);
    if (opts.has_goal) {
        reconstruct_trace(depth, opts.goal, id);
    }
    if constexpr (POLY == 1) {
        print_polynomials(depth);
    }
}

int main(int argc, char* argv[]) {
#if NAUTY == 1
    nauty_check(WORDSIZE, m, n, NAUTYVERSIONID);
    options.getcanon = true;
    options.defaultptn = true;
#endif

#ifdef USE_SMALL_MATRIX
    if (N < 1 || N > 8) {
        fprintf(stderr, "N={%u} not supported, only N=1..8\n", N);
        exit(-1);
    }
#else
    if (N < 1 || N > 20) {
        fprintf(stderr, "N={%u} not supported, only N=1..20\n", N);
        exit(-1);
    }
#endif

    print_configuration();
    ProgramOptions opts = parse_arguments(argc, argv);

#ifdef USE_LARGE_MATRIX
    leaves.init(PairSize);
    intermediate.init(PairSize);
#endif

    MatrixImpl id = Trait::identity();
    if (opts.has_goal) {
        run_bidirectional_search(id, opts.goal, opts.limit);
    } else {
        run_full_bfs(id, opts);
    }
    
    std::cerr << std::setprecision(std::numeric_limits<double>::digits10)
              << "Total time: " << currentTime() << "s" << std::endl;
    
    return 0;
}

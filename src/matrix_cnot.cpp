// Jaco van de Pol, Aarhus University, 
// September 2024, September 2025
// main idea by Jens Emil Christensen

// Compile: 
// Assume nauty is built with "./configure --enable-tls; make" (flag is needed for thread safety)
// g++ -o matrix_cnot matrix_cnot.cpp -fopenmp -DN=6 -DE=1 -DNAUTY=1 -DSWAP=0 -O3 -DNDEBUG -march=native -Inauty/ nauty/nautyW1.a -DWORDSIZE=32 -DMAXN=WORDSIZE

#include <array>
#include <vector>
#include <omp.h>
#include "hashset.h" // thread-safe hash set from dtree project
#include "options.h" // defines N,E,MAX,SWAP,NAUTY,POLY,BEAT, see also matrix_cnot.sh
#include "timing.h"
#include "matrix.h"
#include "repr.h"
#include "trace_back.h"

// precalculated 2-log of the orbit level sizes (0-terminated)
// NOTE: the size depends on if SWAPs are free or not

#if SWAP==0
const std::array<std::vector<byte>,9> levelSizes = {{
    // first number is for level depth=2 (externally: Depth=1)
    {}, //0
    {0}, // 1
    {0,0,0,0}, // 2
    {0,3,4,4,3,0,0}, // 3
    {0,3,5,7,8,9,8,5,0,0}, // 4
    {0,3,5,8,11,13,14,15,15,13,8,0,0}, // 5
    {0,3,6,8,11,14,17,19,22,23,24,23,20,11,0,0}, //6
    {0,3,6,8,11,15,18,21,24,27,30,32,33,34,33,29,17,0,0}, //7
    {0,3,6,8,11,15,18,22,25,29,32,35, /* guess from here on */ 37,38,40,41,40,38,36,34,0,0} //8
}};
#else
const std::array<std::vector<byte>,9> levelSizes = {{
    // first number is for level depth=2 (externally: Depth=1)
    {}, //0
    {0}, // 1
    {0,0,0,0}, // 2
    {0,3,4,4,3,0,0}, // 3
    {0,3,5,5,3,0,0,0,0,0}, // 4
    {0,3,5,7,9,9,7,3,0,0,0,0,0}, // 5
    {0,3,5,8,10,13,14,15,13,10,3,0,0,0,0,0}, //6
    {0,3,5,8,11,14,16,19,21,22,22,20,13,2,0,0,0,0,0}, //7
    {0,3,5,8,11,14,17,20,23,26,28,30,31,30,28,21,3,0,0,0,0,0} //8
}};
#endif

std::array<std::array<std::atomic<counter>,N+1>,N/2+1> poly; // coefficients of the polynomial at distances up to N/2

hashset bfs_levels[3*N];    // for one-directional BFS
hashset bfs_fwd[(3*N+2)/2]; // for bi-directional BFS
hashset bfs_bwd[(3*N+1)/2];

// Compute orbit size from stabilizer size
inline counter compute_orbit_size(counter stabilizer) {
    if (SWAP == 0) {
        return fac_N / stabilizer;
    } else {
        return (fac_N * fac_N) / stabilizer;  // Note: stabilizer divides fac_N
    }
}

counter init_level(hashset levels[], matrix start) {
    levels[0] = hashset(); // level 0 (prev)
    levels[0].init(3);
    levels[1] = hashset(); // level 1 (current)
    levels[1].init(3);
    counter Stab = representative(start); // modifies start
    levels[1].insert(start);
    return compute_orbit_size(Stab);
}

// Process a single CNOT operation: add row i to row j
// Returns true if a new canonical form was discovered
inline void __attribute__((always_inline))
process_cnot(matrix x, uint64_t row_i, byte j,
             hashset &prev_level, hashset &curr_level, hashset &next_level,
             counter &orbit_sum, counter &matrix_count,
             uint32_t depth) {
    matrix y = x ^ (row_i << j*N);
    counter Stab = representative(y);
    
    // Check if we've seen this canonical form before
    if (!prev_level.contains(y) && !curr_level.contains(y) && next_level.insert(y)) {
        // New canonical form found - update counters
        if (SWAP == 0)
            orbit_sum += fac_N / Stab;
        else
            orbit_sum += (fac_N * fac_N) / Stab;
        matrix_count++;
        
        if (POLY == 1) {
            if (2*(depth-1) <= N) {
                byte ess = countEssential(y);
                poly[depth-1][ess] += (fac[ess] * fac[N-ess]) / Stab;
            }
        }
    }
}

// explore and count all successors of the current level
counter next_level(counter &size, hashset levels[], uint32_t depth) { 
    std::atomic<counter> level(0);
    std::atomic<counter> count(0);

    // current and prev are accessed read-only
    // next is modified (extended) concurrently

    // Padded struct to avoid false sharing
    struct alignas(64) PaddedCounter {
        counter value;
    };

    const int max_threads = omp_get_max_threads();
    std::vector<PaddedCounter> thread_levels(max_threads, {0});
    std::vector<PaddedCounter> thread_counts(max_threads, {0});

    // Hoist loop-invariant references and computations
    hashset &prev_level = levels[depth-2];
    hashset &curr_level = levels[depth-1];
    hashset &next_level = levels[depth];

    curr_level.parallelForAll(
        [&](matrix x){
            int tid = omp_get_thread_num();
            counter &orbit_sum = thread_levels[tid].value;
            counter &matrix_count = thread_counts[tid].value;
            
            // Generate all N(N-1) successor matrices by applying CNOT(i,j) operations
            for (byte i=0; i<N; i++) {
                // Extract row i once for all j destinations
                uint64_t mask = (1UL<<N*(i+1)) - (1UL<<N*i);
                uint64_t row_i = (x & mask) >> i*N;
                
                for (byte j=0; j<N; j++) {
                    if (i != j) {
                        process_cnot(x, row_i, j, prev_level, curr_level, next_level,
                                     orbit_sum, matrix_count, depth);
                    }
                }
            }
#if BEAT>0
            size_t worker = omp_get_thread_num();
            if (passedTime(lifeTime[worker]) >= BEAT) { // every minute
                # pragma omp critical
                {
                    lifeBeat(worker, orbit_sum, matrix_count);
                }
                lifeTime[worker] = system_clock::now();
            }
#endif
        });

    // Aggregate thread-local results
    for (int i = 0; i < max_threads; i++) {
        if (thread_levels[i].value > 0) {
            level += thread_levels[i].value;
            count += thread_counts[i].value;
        }
    }
    size = count;
    return level;
}

int generate_bfs(matrix start, matrix goal, byte limit, hashset bfs_levels[]) {

    // initialize Breadth-First Search
    byte depth = 1, tableSize = 3;
    counter level, levels, orbit, orbits;
    orbit = orbits = 1;

    fprintf(stderr,"Depth 0 (2^3): "); fflush(stderr);
    levels = level = init_level(bfs_levels, start);

    while (orbit) {
        report(level, orbit);
        if (goal)
            { if (find_level(goal, bfs_levels[depth])) return -depth; }
        else 
            { if (depth > 1) bfs_levels[depth-2].deinit(); }
        if (depth-1 == limit) return depth;
        depth++;
        tableSize = std::min(std::max(levelSizes[N][depth-2] + E, 3), MAX);
        bfs_levels[depth] = hashset();
        bfs_levels[depth].init(tableSize);
        fprintf(stderr,"Depth %u (2^%u): ", depth-1, tableSize); fflush(stderr);
        levels += level = next_level(orbit, bfs_levels, depth);
        orbits += orbit;
    }
    depth--;
    fprintf(stderr,"--\n");
/*  // print matrices at the last level (out of curiosity)
    bfs_levels[depth].forAll(
        [](matrix c) { pretty_matrix(c); }
    );
*/
    fprintf(stderr,"Total size: %lu (%lu orbits), completed at depth %u\n", levels, orbits, depth-1);
    return depth;
}

matrix intersect(hashset &L1, hashset &L2) {
    std::atomic<matrix> joint(0);
    L1.parallelForAll([&](matrix x){
        if (L2.contains(x)) joint=x; // How to terminate when found?
    });
    return joint;
}

// Bidirectional search yields a matrix in the intersection of Fwd(start) and Bwd(goal)
// We also return the depths of the fwd and bwd search (fdepth,bdepth)
// We return (0,fdepth,bdepth) if start and goal are not connected

using triple = std::pair<matrix,std::pair<byte,byte>>;
inline triple Triple(matrix m, byte d1, byte d2) {
    return std::pair<matrix,std::pair<byte,byte>>(m, std::pair<byte,byte>(d1, d2));
}

triple bidirectional(matrix start, matrix goal, byte limit, hashset bfs_fwd[], hashset bfs_bwd[]) {

    // initialize Bidirectional fwd/bwd Search
    byte fdepth = 1, bdepth=1, tableSize;
    counter level, forbit, borbit, levels, orbits;
    forbit = borbit = 1; orbits = 2;
    levels = level = init_level(bfs_fwd, start);
    fprintf(stderr,"Fwd Depth 0 (2^3): "); report(level, forbit);
    levels += level = init_level(bfs_bwd, goal);
    fprintf(stderr,"Bwd Depth 0 (2^3): "); report(level, borbit);
    matrix m = intersect(bfs_fwd[fdepth], bfs_bwd[bdepth]);
    if (m) return Triple(m, fdepth, bdepth);

    while (fdepth + bdepth - 2 < 3*(N-1)) { // expand the smallest level
        if (fdepth+bdepth-2 == limit) return Triple(m, fdepth, bdepth);
        if (forbit <= borbit) {
            fdepth++; 
            tableSize = std::min(std::max(levelSizes[N][fdepth-2] + E, 3), MAX);
            fprintf(stderr,"Fwd Depth %u (2^%u): ", fdepth-1, tableSize); fflush(stderr);
            bfs_fwd[fdepth] = hashset();
            bfs_fwd[fdepth].init(tableSize);
            levels += level = next_level(forbit, bfs_fwd, fdepth);
            orbits += forbit;
            report(level, forbit);
        }
        else {
            bdepth++;
            // Note: this Bwd level is smaller than next Fwd one
            // Problem: Bwd's successor can still be larger than Fwd's successor (hence 10)
            tableSize = std::min(std::max(levelSizes[N][fdepth-1] + E, 10), MAX); 
            fprintf(stderr,"Bwd Depth %u (2^%u): ", bdepth-1, tableSize); fflush(stderr);
            bfs_bwd[bdepth] = hashset();
            bfs_bwd[bdepth].init(tableSize);
            levels += level = next_level(borbit, bfs_bwd, bdepth);
            orbits += borbit;
            report(level, borbit);
        }
        m = intersect(bfs_fwd[fdepth], bfs_bwd[bdepth]);
        if (m) return Triple(m, fdepth, bdepth);
    }
    fprintf(stderr,"Not found at distance %u+%u (%lu, %lu)\n", fdepth-1, bdepth-1, levels, orbits);
    return Triple(0, fdepth, bdepth);
}

// Program configuration parsed from command line
struct ProgramOptions {
    byte limit;
    matrix goal;
};

// Parse command line arguments
ProgramOptions parse_arguments(int argc, char const *argv[]) {
    ProgramOptions opts;
    opts.limit = (byte)-1;  // -1 (255) means no limit
    opts.goal = 0;          // 0 means no specific goal
    
    // Check for limit argument "-K"
    if (argc > 1 && argv[1][0] == '-') {
        opts.limit = atoi(argv[1] + 1);
        if (opts.limit != (byte)-1) {
            fprintf(stderr, "Cutting off at maximum distance: %d\n", opts.limit);
        }
    }
    
    // Check for goal matrix argument (last argument not starting with '-')
    if (argc > 1 && argv[argc-1][0] != '-') {
        opts.goal = read_matrix(argv[argc-1]);
        assert(opts.goal != 0 && "0-matrix cannot be generated");
    }
    
    return opts;
}

// Print program configuration
void print_configuration() {
    fprintf(stderr, "Handling matrices of size N = %u\n", N);
    fprintf(stderr, "Using DTree + %u extra bits, max-size %u\n", E, MAX);
    fprintf(stderr, "Use Nauty: %u. Swaps-for-free: %u. Polynomial: %u\n", NAUTY, SWAP, POLY);
#if defined(_OPENMP)
    fprintf(stderr, "Running with %d OpenMP threads\n", omp_get_max_threads());
#endif
}

// Compute identity matrix for size N
matrix compute_identity_matrix() {
    matrix id = 1;
    for (byte i = 1; i < N; i++) {
        id = (id << (N + 1)) | 1;
    }
    return id;
}

// Run bidirectional search and print results
void run_bidirectional_search(matrix id, matrix goal, byte limit) {
    triple result = bidirectional(id, goal, limit, bfs_fwd, bfs_bwd);
    matrix middle = result.first;
    int fdepth = result.second.first;
    int bdepth = result.second.second;
    
    if (middle) {
        fprintf(stderr, "Found at distance %u (%u + %u)\n", 
                fdepth + bdepth - 2, fdepth - 1, bdepth - 1);
        perm pi;
        trace concat = trace_back_middle(id, middle, goal, bfs_fwd, bfs_bwd, 
                                        fdepth, bdepth, pi);
        print_trace(id, goal, concat, pi);
    } else {
        fprintf(stderr, "Goal not found after %d steps: \n", fdepth + bdepth - 2);
        pretty_matrix(goal);
    }
}

// Run full BFS and print results (including polynomial coefficients if enabled)
void print_polynomials(int depth) {
    fprintf(stderr, "Polynomial coefficients (N=%u):\n", N);
    for (int d = 1; d <= std::min(N/2, depth-1); d++) {
        fprintf(stderr, "d=%u: [", d);
        for (byte i = 0; i <= 2*d; i++) {
            fprintf(stderr, "%lu%c ", 
                    poly[d][i].load(std::memory_order_relaxed), 
                    (i < 2*d ? ',' : ']'));
        }
        fprintf(stderr, "\n");
    }
}

/*
 * Main: if first arg is -K, set K as limit. If last argument is not -*, set as goal
 */

int main(int argc, char const *argv[]) {
#if NAUTY==1
    nauty_check(WORDSIZE,m,n,NAUTYVERSIONID);
    options.getcanon=true;   // we want the canonical graph
    options.defaultptn=true; // default coloring
#endif
    
    // Validate matrix size
    if (N < 1 || N > 8) {
        fprintf(stderr, "N={%u} not supported, only N=1..8\n", N);
        exit(-1);
    }
    
    // Print configuration and parse arguments
    print_configuration();
    ProgramOptions opts = parse_arguments(argc, argv);
    
    // Compute identity matrix
    matrix id = compute_identity_matrix();
    
    // Run appropriate search algorithm
    if (opts.goal) {
        run_bidirectional_search(id, opts.goal, opts.limit);
    } else {
        int depth = generate_bfs(id, opts.goal, opts.limit, bfs_levels);
        // assume depth>=0, since we don't call generate_bfs if there is a goal
        if (POLY==1) print_polynomials(depth);
    }
    
    // Print total execution time
    std::cerr << std::setprecision(std::numeric_limits<double>::digits10)
              << "Total time: " << currentTime() << "s" << std::endl;
}

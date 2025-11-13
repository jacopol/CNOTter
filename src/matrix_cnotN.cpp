// Jaco van de Pol, Aarhus University, 
// September 2024, September 2025
// main idea by Jens Emil Christensen

// Compile: 
// Assume nauty is built with "./configure --enable-tls; make" (flag is needed for thread safety)
// g++ -o matrix_cnot matrix_cnot.cpp -fopenmp -DN=6 -DE=1 -DNAUTY=1 -DSWAP=0 -O3 -DNDEBUG -march=native -Inauty/ nauty/nautyW1.a -DWORDSIZE=32 -DMAXN=WORDSIZE

#include <array>
#include <vector>
#include <omp.h>
#include <algorithm> // for permutations
#include "options.h" // defines N,E,MAX,SWAP,NAUTY,POLY,BEAT, see also matrix_cnot.sh
#include "timing.h"
#include "matrixN.h"
#include "reprN.h"
#include "trace_backN.h"
#include "hashset.h" // thread-safe hash set from dtree project
#include "tree.h"

// precalculated 2-log of the orbit level sizes (0-terminated)
// NOTE: the size depends on if SWAPs are free or not

#if SWAP==0
const std::array<std::vector<byte>,11> levelSizes = {{
    // first number is for level depth=2 (externally: Depth=1)
    {}, //0
    {0}, // 1
    {0,0,0,0}, // 2
    {0,3,4,4,3,0,0}, // 3
    {0,3,5,7,8,9,8,5,0,0}, // 4
    {0,3,5,8,11,13,14,15,15,13,8,0,0}, // 5
    {0,3,6,8,11,14,17,19,22,23,24,23,20,11,0,0}, //6
    {0,3,6,8,11,15,18,21,24,27,30,32,33,34,33,29,17,0,0}, //7
    {0,3,6,8,11,15,18,22,25,29,32,34 /* +1 */, /* guess */ 37,38,40,41,40,38,36,34,0,0}, //8
    // NOTE: we subtracted one for (8,10) in order to manage cycle8 on qfat
    {0,3,6,8,11,15,18,22,26,30,33, /* guess */ 35,37,38,40,41,40,38,36,34,0,0}, //9
    {0,3,6,8,11,15,18,22,26,30,34, /* guess */ 36,37,38,40,41,40,38,36,34,0,0} //10
    // For 11 and larger, we stick to the numbers for 10.
}};
#else
const std::array<std::vector<byte>,11> levelSizes = {{
    // first number is for level depth=2 (externally: Depth=1)
    {}, //0
    {0}, // 1
    {0,0,0,0}, // 2
    {0,3,4,4,3,0,0}, // 3
    {0,3,5,5,3,0,0,0,0,0}, // 4
    {0,3,5,7,9,9,7,3,0,0,0,0,0}, // 5
    {0,3,5,8,10,13,14,15,13,10,3,0,0,0,0,0}, //6
    {0,3,5,8,11,14,16,19,21,22,22,20,13,2,0,0,0,0,0}, //7
    {0,3,5,8,11,14,17,20,23,26,28,30,31,30,28,21,3,0,0,0,0,0}, //8
    {0,3,5,8,11,14,17,21,24,28,31, /* guess */ 34, 34, 34, 34, 34, 34}, //9
    {0,3,5,8,11,14,17,21,24,28,32, /* guess */ 34, 34, 34, 34, 34, 34} //10
}};
#endif

inline byte predictSize(int depth) {
    byte lookup = levelSizes[std::min(N, 10)][depth-2]  ; // we only have lookup tables up to size 10
    return std::min(std::max(lookup + E, 3), MAX); // add E and ensure result is in [3,MAX]
}

#if SWAP==0
#define Orbit(stab) (fac[N] / stab)
#else
#define Orbit(stab) (fac[N] * (fac[N] / stab)) // Note: stab divides fac[N]
#endif

#if POLY==1
std::array<std::array<std::atomic<counter>,N+1>,N/2+1> poly; // coefficients of the polynomial at distances up to N/2
#endif

rootset bfs_levels[3*N];    // for one-directional BFS

void Add(const Matrix &x, byte i, byte j, 
                rootset *prev, rootset *current, rootset *next, int depth,
                counter &level, counter &count) {
    Matrix y = x.addrow(i,j);
    counter Stab = representative(y);
    Matrix y2 = y.inverse().transpose();
    counter Stab2 = representative(y2);
    if (y == y2) Stab2=0; 
    if (y2 < y) y=y2;  
    if (!CONTAINS(y,*prev) && !CONTAINS(y,*current) && INSERT(y,*next)) {
        // only insert and count if new; 
        level += Orbit(Stab);
        if (Stab2>0) level+= Orbit(Stab2);
        count++;
#if POLY==1
        if (2*(depth-1)<=N) {
            byte ess = countEssential(y);
            poly[depth-1][ess] += (fac[ess] * fac[N-ess]) / Stab;
        }
#endif
    }
}

counter init_level(hashset levels[], Matrix &start) {
    levels[0] = hashset(); // level 0 (prev)
    levels[0].init(3);
    levels[1] = hashset(); // level 1 (current)
    levels[1].init(3);
    counter Stab = representative(start); // modifies start
    INSERT(start, levels[1]);
    return Orbit(Stab);
}

// explore and count all successors of the current level
counter next_level(counter &size, hashset levels[], uint32_t depth) { 
    std::atomic<mat_idx> level(0);
    std::atomic<mat_idx> count(0);

    // current and prev are accessed read-only
    // next is modified (extended) concurrently

    auto prev = &levels[depth-2];
    auto current = &levels[depth-1];
    auto next = &levels[depth];

    current->parallelForAll(
        [&](mat_idx r){
            Matrix x = GET(r);
            counter loc_level=0, loc_count=0;
            for (byte i=0; i<N; i++)
                for (byte j=0; j<N; j++) // add to row j
                    if (i != j) Add(x, i, j, prev, current, next, depth, loc_level, loc_count);
        if (loc_level > 0) {
            level += loc_level;
            count += loc_count;
        }
#if BEAT>0
        size_t worker = omp_get_thread_num();
        if (passedTime(lifeTime[worker]) >= BEAT) { // every minute
            # pragma omp critical
            {
                lifeBeat(worker, loc_level, loc_count);
            }
            lifeTime[worker] = system_clock::now();
        }
#endif
    });
    size = count;
    return level;
}

// Bidirectional search yields a matrix in the intersection of Fwd(start) and Bwd(goal)
// We also return the depths of the fwd and bwd search (fdepth,bdepth)
// We return (0,fdepth,bdepth) if start and goal are not connected

using triple = std::pair<std::pair<Matrix,Matrix>,std::pair<byte,byte>>;
inline triple Triple(const Matrix &x, const Matrix &y, byte d1, byte d2) {
    return std::pair<std::pair<Matrix,Matrix>,std::pair<byte,byte>>
                (std::pair<Matrix,Matrix>(x,y), 
                 std::pair<byte,byte>(d1, d2));
}

triple check_backwards(const Matrix &goal, byte depth) {
    std::atomic<Matrix> X(false), Y(false);
    std::atomic<int> fwd(-1);
    bfs_levels[depth].parallelForAll([&](mat_idx idx){
        // TODO: How to terminate when found?
        Matrix x = GET(idx);
        perm pi; id_perm(pi);
        do {
            Matrix z = x.permute(pi);
            Matrix y = goal.multiply(z);
            representative(y);
            // y = goal.z
            // goal = y.z^{-1}
            if (CONTAINS(y, bfs_levels[depth-1])) {
                X = x;
                Y = y;
                fwd = depth-1;
                break;
            }
            else if (CONTAINS(y, bfs_levels[depth])) {
                X = x;
                Y = y;
                fwd = depth;
                break;
            }
        } while (std::next_permutation(pi,pi+N));
    });
    return Triple(X, Y, depth, fwd.load());
}

triple generate_bfs(Matrix start, Matrix goal, byte limit, hashset bfs_levels[]) {

    // initialize Breadth-First Search
    byte depth = 1, tableSize = 3;
    counter level, levels, orbit, orbits;
    orbit = orbits = 1;

    printf("Depth 0 (2^3): "); fflush(stdout);
    levels = level = init_level(bfs_levels, start);

    if (!(goal==Matrix(false))) {
        if (find_level(goal, bfs_levels[depth])) 
            return Triple(start, goal, depth, 0);
    }

    while (orbit) {
        report(level, orbit);
        if (!(goal==Matrix(false))) { // TODO: define neq
            triple t = check_backwards(goal, depth);
            if (!(t.first.first==Matrix(false))) return t;
        }
        else if (depth > 1) bfs_levels[depth-2].deinit();
        if (depth-1 == limit) return Triple(0, 0, depth, 0); // not found
        depth++;
        tableSize = predictSize(depth);
        bfs_levels[depth] = hashset();
        bfs_levels[depth].init(tableSize);
        printf("Depth %u (2^%u): ", depth-1, tableSize); fflush(stdout);
        levels += level = next_level(orbit, bfs_levels, depth);
        orbits += orbit;
    }
    depth--;
    printf("--\n");
/*  // print matrices at the last level (out of curiosity)
    bfs_levels[depth].forAll(
        [](matrix c) { pretty_matrix(c); }
    );
*/
    printf("Total size: %lu (%lu orbits), completed at depth %u\n", levels, orbits, depth-1);
    return Triple(0,0,depth,0);
}

mat_idx intersect(hashset &L1, hashset &L2) {
    std::atomic<mat_idx> joint(0);
    L1.parallelForAll([&](mat_idx x){
        if (L2.contains(x)) joint=x; // How to terminate when found?
    });
    return joint;
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
    if (N<1 || N>20) {
        printf("N={%u} not supported, only N=1..20\n", N);
        exit(-1);
    }
    printf("Handling matrices of size N = %u\n", N);
    printf("Using DTree + %u extra bits, max-size %u\n", E, MAX);
    printf("Use Nauty: %u. Swaps-for-free: %u. Polynomial: %u\n", NAUTY, SWAP, POLY);
    #if defined(_OPENMP)
        printf("Running with %d OpenMP threads\n",omp_get_max_threads());
    #endif

    leaves.init(PairSize);
    intermediate.init(PairSize);

    Matrix id(1);
    Matrix goal(0);

    byte  limit=-1; // search limit when >=0: set with argument "-<limit>"

    if (argc>1 && argv[1][0]=='-') {
        limit = atoi(argv[1]+1); // skip the leading '-'
        if (limit!=(byte)-1)     // unsigned, so this is 255
            printf("Cutting off at maximum distance: %d\n", limit);
    }
    if (argc>1 && argv[argc-1][0]!='-') {
        goal = Matrix::read(argv[argc-1]);
        if (true) {
            // testing inverse:
            goal.print();
            Matrix goal1 = goal.inverse();
            goal1.print();
            goal1 = goal.multiply(goal1);
            goal1.print();
            goal1 = goal1.transpose();
            goal1.print();
            exit(-1);
            //investigate(goal);
        }
    }
    triple m = generate_bfs(id, goal, limit, bfs_levels);
    if (!(goal==Matrix(false))) {
        Matrix X = m.first.first;
        Matrix Y = m.first.second;
        int fdepth = m.second.first;
        int bdepth = m.second.second;
        if (!(X == Matrix(false))) {
            printf("Found at distance %u (%u + %u)\n", fdepth + bdepth - 2, fdepth-1, bdepth-1);
            perm pi;
            trace concat = trace_back_middle(id, X, Y, goal, bfs_levels, bfs_levels, fdepth, bdepth, pi);
            print_trace(id, goal, concat, pi);
        } else {
            printf("Goal not reachable in %d steps: \n", fdepth+bdepth-2);
            goal.print();
        }
    }
#if POLY==1
        printf("Polynomial coefficients (N=%u):\n", N);
        for (int d=1; d<=std::min(N/2,depth-1); d++) {
            printf("d=%u: [", d);
            for (byte i=0; i<=2*d; i++)
                printf("%lu%c ", poly[d][i].load(std::memory_order_relaxed), (i<2*d ? ',' : ']'));
            printf("\n");
        }
#endif

    std::cout << std::setprecision(std::numeric_limits<double>::digits10)
              << "Total time: " << currentTime() << "s" << std::endl;
}

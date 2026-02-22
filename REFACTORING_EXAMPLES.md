# Concrete Refactoring Examples

This document shows side-by-side comparisons of how specific functions are unified from the two versions into a single template-based implementation.

---

## Example 1: Row Extraction Loop

### Original `matrix_cnot.cpp`
```cpp
counter next_level(counter &size, hashset levels[], uint32_t depth) { 
    // ... setup code ...
    
    hashset &prev_level = levels[depth-2];
    hashset &curr_level = levels[depth-1];
    hashset &next_level = levels[depth];

    curr_level.parallelForAll(
        [&](matrix x){
            int tid = omp_get_thread_num();
            counter &orbit_sum = thread_levels[tid].value;
            counter &matrix_count = thread_counts[tid].value;
            
            // Generate N(N-1) CNOT operations
            for (byte i=0; i<N; i++) {
                // Extract row i once
                uint64_t mask = (1UL<<N*(i+1)) - (1UL<<N*i);
                uint64_t row_i = (x & mask) >> i*N;
                
                for (byte j=0; j<N; j++) {
                    if (i != j) {
                        process_cnot(x, row_i, j, prev_level, curr_level, 
                                   next_level, orbit_sum, matrix_count, depth);
                    }
                }
            }
        });
    
    // ... aggregation code ...
}
```

### Original `matrix_cnotN.cpp`
```cpp
counter next_level(counter &size, hashset levels[], uint32_t depth) { 
    // ... setup code ...
    
    auto prev = &levels[depth-2];
    auto current = &levels[depth-1];
    auto next = &levels[depth];
    
    const bool compute_poly = (POLY == 1) && (2*(depth-1) <= N);

    current->parallelForAll(
        [&](mat_idx r){
            Matrix x = GET(r);
            int tid = omp_get_thread_num();
            counter &loc_level = thread_levels[tid].value;
            counter &loc_count = thread_counts[tid].value;
            
            // Generate N(N-1) CNOT operations
            for (byte i=0; i<N; i++) {
                // Extract row i once
                for (byte j=0; j<N; j++)
                    if (i!=j)
                        Add(x, x.addrow1(i), j, prev, current, next, depth, 
                            loc_level, loc_count, compute_poly);
            }
        });
    
    // ... aggregation code ...
}
```

### Unified Template
```cpp
template<typename MatrixImpl>
counter next_level(counter &size, hashset levels[], uint32_t depth) { 
    using Trait = MatrixTrait<MatrixImpl>;
    
    // ... setup code identical for both ...
    
    hashset &prev_level = levels[depth-2];
    hashset &curr_level = levels[depth-1];
    hashset &next_level = levels[depth];
    
    const bool compute_poly = (POLY == 1) && (2*(depth-1) <= N);

    curr_level.parallelForAll(
        [&](const MatrixImpl& x){
            int tid = omp_get_thread_num();
            counter &orbit_sum = thread_levels[tid].value;
            counter &matrix_count = thread_counts[tid].value;
            
            // Generate N(N-1) CNOT operations
            for (byte i=0; i<N; i++) {
                // Extract row i once (works for both representations)
                uint64_t row_i = Trait::extract_row(x, i);
                
                for (byte j=0; j<N; j++) {
                    if (i != j) {
                        process_cnot(x, row_i, j, prev_level, curr_level, 
                                   next_level, orbit_sum, matrix_count, depth);
                    }
                }
            }
        });
    
    // ... aggregation code identical for both ...
}
```

**Key observations:**
- `Trait::extract_row(x, i)` returns `uint64_t` for both types
- For `uint64_t`: extracts bits directly
- For `Matrix`: calls `x.addrow1(i)` and masks to uint64_t
- Rest of algorithm unchanged
- No conditional compilation needed at algorithm level

---

## Example 2: CNOT Processing Function

### Original `matrix_cnot.cpp`
```cpp
inline void __attribute__((always_inline))
process_cnot(matrix x, uint64_t row_i, byte j,
             hashset &prev_level, hashset &curr_level, hashset &next_level,
             counter &orbit_sum, counter &matrix_count,
             uint32_t depth) {
    matrix y = x ^ (row_i << j*N);          // Apply CNOT: XOR operation
    counter Stab = representative(y);       // Canonicalize
    
    // Check if we've seen this canonical form before
    if (!prev_level.contains(y) && !curr_level.contains(y) && 
        next_level.insert(y)) {
        // New canonical form found
        if constexpr (SWAP == 0)
            orbit_sum += fac_N / Stab;
        else
            orbit_sum += (fac_N * fac_N) / Stab;
        matrix_count++;
        
        if constexpr (POLY == 1) {
            if (2*(depth-1) <= N) {
                byte ess = countEssential(y);
                poly[depth-1][ess] += (fac[ess] * fac[N-ess]) / Stab;
            }
        }
    }
}
```

### Original `matrix_cnotN.cpp`
```cpp
inline void Add(const Matrix &x, uint64_t row_i, byte j, 
                rootset *prev, rootset *current, rootset *next, int depth,
                counter &level, counter &count, bool compute_poly) {
    Matrix y = x.addrow2(row_i, j);         // Apply CNOT: method call
    counter Stab = representative(y);       // Canonicalize
    
    if (!CONTAINS(y,*prev) && !CONTAINS(y,*current) && INSERT(y,*next)) {
        // only insert and count if new
        level += Orbit(Stab);
        count++;
        if (compute_poly) {
            byte ess = countEssential(y);
            poly[depth-1][ess] += (fac[ess] * fac[N-ess]) / Stab;
        }
    }
}
```

### Unified Template
```cpp
template<typename MatrixImpl>
inline void process_cnot(const MatrixImpl& x, uint64_t row_i, uint8_t j,
                        hashset &prev_level, hashset &curr_level,
                        hashset &next_level,
                        counter &orbit_sum, counter &matrix_count,
                        uint32_t depth) {
    using Trait = MatrixTrait<MatrixImpl>;
    
    // Apply CNOT(i,j): Works identically for both representations
    MatrixImpl y = Trait::add_row(x, row_i, j);
    counter Stab = representative(y);       // Canonicalize
    
    // Check membership in levels (works for both)
    if (!prev_level.contains(y) && !curr_level.contains(y) && 
        next_level.insert(y)) {
        
        // Compute orbit size (same for both)
        orbit_sum += compute_orbit_size(Stab);
        matrix_count++;
        
        if constexpr (POLY == 1) {
            if (2*(depth-1) <= N) {
                uint8_t ess = countEssential(y);
                poly[depth-1][ess] += (fac[ess] * fac[N-ess]) / Stab;
            }
        }
    }
}
```

**Key observations:**
- No conditional logic on matrix type needed
- `Trait::add_row()` is the polymorphic operation
  - For uint64_t: `y = x ^ (row_i << j*N)`
  - For Matrix: `y = x.addrow2(row_i, j)`
- Both produce identical canonical form via same `representative()`
- Completely unified algorithm

---

## Example 3: Matrix Comparison

### Original `matrix_cnot.cpp`
```cpp
bool find_level(matrix goal, hashset& level) {
    bool found = false;
    level.forAll(
        [](matrix x) {
            if (x == goal) found = true;  // Direct comparison
        }
    );
    return found;
}

// Check if goal is "null"
if (goal) { /* non-trivial goal */ }
```

### Original `matrix_cnotN.cpp`
```cpp
bool find_level(const Matrix& goal, hashset& level) {
    bool found = false;
    level.forAll(
        [](Matrix x) {
            if (x == goal) found = true;  // Delegates to Matrix::operator==
        }
    );
    return found;
}

// Check if goal is "null"
if (!(goal==Matrix(false))) { /* non-trivial goal */ }
```

### Unified Template
```cpp
template<typename MatrixImpl>
bool find_level(const MatrixImpl& goal, hashset& level) {
    using Trait = MatrixTrait<MatrixImpl>;
    
    bool found = false;
    level.parallelForAll(
        [&](const MatrixImpl& x) {
            if (Trait::equals(x, goal)) found = true;  // Polymorphic comparison
        }
    );
    return found;
}

// In main function:
MatrixImpl null_matrix = 
#ifdef USE_SMALL_MATRIX
    0
#else
    Matrix(false)
#endif
;

// Check if goal is "null"
if (!Trait::equals(goal, null_matrix)) { /* non-trivial goal */ }
```

**Key observations:**
- `Trait::equals()` handles both `==` operator and `.addrow1()` method call
- Null matrix creation handled via preprocessor (executed once at startup)
- No runtime performance cost

---

## Example 4: Permutation Operations

### Original `matrix.h`
```cpp
inline matrix permute(matrix x, const perm pi) {
    matrix y = 0;
    for (byte i=N-1; i<N; i--)
        for (byte j=N-1; j<N; j--) {
            y <<= 1;
            y |= (x >> (pi[i]*N + pi[j])) & 1;
        }
    return y;
}

inline matrix permute2(matrix x, const perm pi1, const perm pi2) {
    matrix y = 0;
    for (byte i=N-1; i<N; i--)
        for (byte j=N-1; j<N; j--) {
            y <<= 1;
            y |= (x >> (pi1[i]*N + pi2[j])) & 1;
        }
    return y;
}
```

### Original `matrixN.h`
```cpp
Matrix permute(const uint8_t pi[N]) const {
    Matrix other;
    for (uint8_t i=0; i<N; i++) {
        for (uint8_t j=0; j<N; j++) {
            other.set(i,j, get(pi[i],pi[j]));
        }
    }
    return other;
}

Matrix permute2(const uint8_t pi1[N], const uint8_t pi2[N]) const {
    Matrix other;
    for (uint8_t i=0; i<N; i++) {
        for (uint8_t j=0; j<N; j++) {
            other.set(i,j, get(pi1[i],pi2[j]));
        }
    }
    return other;
}
```

### Unified Trait
```cpp
template<>
struct MatrixTrait<uint64_t> {
    // For small matrices: bitwise operations
    static inline uint64_t permute(const uint64_t& x, const uint8_t pi[N]) {
        uint64_t y = 0;
        for (uint8_t i=N-1; i<N; i--)
            for (uint8_t j=N-1; j<N; j--) {
                y <<= 1;
                y |= (x >> (pi[i]*N + pi[j])) & 1;
            }
        return y;
    }
    
    static inline uint64_t permute2(const uint64_t& x,
                                    const uint8_t pi1[N],
                                    const uint8_t pi2[N]) {
        uint64_t y = 0;
        for (uint8_t i=N-1; i<N; i--)
            for (uint8_t j=N-1; j<N; j--) {
                y <<= 1;
                y |= (x >> (pi1[i]*N + pi2[j])) & 1;
            }
        return y;
    }
};

template<>
struct MatrixTrait<Matrix> {
    // For large matrices: method delegation
    static inline Matrix permute(const Matrix& x, const uint8_t pi[N]) {
        return x.permute(pi);
    }
    
    static inline Matrix permute2(const Matrix& x,
                                  const uint8_t pi1[N],
                                  const uint8_t pi2[N]) {
        return x.permute2(pi1, pi2);
    }
};

// Generic function not needed—always call via trait in user code:
template<typename MatrixImpl>
void some_algorithm(const MatrixImpl& m, const perm pi) {
    using Trait = MatrixTrait<MatrixImpl>;
    MatrixImpl m_perm = Trait::permute(m, pi);
    // ...
}
```

**Key observations:**
- Trait provides identical interface for both operations
- Small matrix uses bitwise loop-shift
- Large matrix delegates to method calls
- Generic algorithms use `Trait::permute()` uniformly

---

## Example 5: Level Size Prediction

### Original `matrix_cnot.cpp`
```cpp
// Direct lookup table access
tableSize = std::min(std::max(levelSizes[N][depth-2] + E, 3), MAX);
```

### Original `matrix_cnotN.cpp`
```cpp
// Wrapped in function with safety checks
inline byte predictSize(int depth) {
    byte lookup = levelSizes[std::min(N, 10)][depth-2];
    return std::min(std::max(lookup + E, 3), MAX);
}

tableSize = predictSize(depth);
```

### Unified Version
```cpp
// Single function works for both (slightly defensive programming)
inline uint8_t predictSize(int depth) {
    uint8_t lookup = levelSizes[std::min((size_t)N, size_t(10))][depth-2];
    return std::min(std::max(lookup + E, 3), MAX);
}

// Use in template function:
template<typename MatrixImpl>
void generate_bfs(...) {
    // ... 
    uint8_t tableSize = predictSize(depth);  // Same for all matrix types
    // ...
}
```

**Key observations:**
- Abstraction is at algorithm level, not trait level
- Both versions already handle this similarly
- Unified version is slightly more robust

---

## Summary of Refactoring Pattern

| Operation | Pattern |
|-----------|---------|
| **Basic get/set** | `Trait::get()`, `Trait::set()` |
| **CNOT application** | `Trait::apply_cnot()` or `Trait::add_row()` |
| **Permutation** | `Trait::permute()`, `Trait::permute2()` |
| **Comparison** | `Trait::equals()`, `Trait::less()` |
| **Row extraction** | `Trait::extract_row()` |
| **Identity** | `Trait::identity()` |
| **Algorithm control** | Use `if constexpr(SWAP==...)` as before |
| **Type selection** | Use `#ifdef USE_SMALL_MATRIX` exactly once at compile time |

Every polymorphic operation goes through the trait, but since all trait methods are `inline`, the compiler generates identical code as the original hand-written versions.


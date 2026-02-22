# Merging matrix_cnot.cpp and matrix_cnotN.cpp - Design Document

## Problem Analysis

The two files implement the same CNOT search algorithm but use **incompatible matrix representations**:

| Aspect | `matrix_cnot.cpp` | `matrix_cnotN.cpp` |
|--------|-------------------|-------------------|
| **Matrix Type** | `uint64_t` (single word) | `Matrix` class (array of words) |
| **Capacity** | Up to 8×8 | Arbitrary size (N can be large) |
| **Row Addition** | Bitwise: `x ^ (row_i << j*N)` | Methods: `addrow1()` + `addrow2()` |
| **Storage** | Direct scalar | Bit-packed array (NR = ⌈N/NC⌉ words) |
| **Hashing** | Direct scalar value | Multiple-word data structure |
| **API Calls** | Free functions | Class instance methods |

## Proposed Solution: Template-Based Polymorphism

### Overview

Instead of duplicating algorithm code, create:

1. **Matrix Trait/Interface** (`matrix_trait.h`)
   - Abstract operations: `get()`, `set()`, `apply_cnot()`, `permute()`, etc.
   - Works with both small and large matrices via template specialization

2. **Unified Algorithm** (`matrix_cnot_unified.cpp`)
   - Generic BFS, search, and processing using template parameters
   - Single `next_level()`, `bidirectional()`, etc. function

3. **Trait Specializations** 
   - `MatrixTrait<uint64_t>` for small matrices
   - `MatrixTrait<Matrix>` for large matrices
   - Encapsulates all differences

### Key Design Principles

- **Zero overhead abstraction**: All trait operations are `inline` and using concepts/SFINAE
- **Cache efficiency**: Small matrices stay scalar; large matrices use optimized bit-packing
- **Minimal changes**: Legacy repr/hash logic encapsulated in traits
- **Selective templating**: Only template what differs; keep algorithm code DRY

---

## Detailed Structure

### 1. Matrix Trait Interface (`src/matrix_trait.h`)

```cpp
#ifndef MATRIX_TRAIT_H
#define MATRIX_TRAIT_H

#include <cstdint>
#include "options.h"

// Base trait template - specializations for uint64_t and Matrix
template<typename MatrixType>
struct MatrixTrait;

// Common utility functions (same for both types)
template<typename MatrixType>
inline bool testEssential(const MatrixType& x, uint8_t i);

template<typename MatrixType>
inline uint8_t countEssential(const MatrixType& x);

template<typename MatrixType>
inline MatrixType read_matrix(const std::string& filename);

template<typename MatrixType>
inline void pretty_print(const MatrixType& x);

// ============ Specialization for uint64_t ============
template<>
struct MatrixTrait<uint64_t> {
    using MatrixType = uint64_t;
    using StorageType = uint64_t;  // For hashset storage
    
    // Element access
    static inline bool get(const MatrixType& x, uint8_t i, uint8_t j) {
        return (x >> (N * i + j)) & 1;
    }
    
    static inline void set(MatrixType& x, uint8_t i, uint8_t j, bool val) {
        if (val)
            x |= (1UL << (N * i + j));
        else
            x &= ~(1UL << (N * i + j));
    }
    
    // CNOT operation: add row i to row j (inlined for performance)
    static inline MatrixType apply_cnot(const MatrixType& x, uint8_t i, uint8_t j) {
        uint64_t row_i = (x >> (N * i)) & ((1UL << N) - 1);
        return x ^ (row_i << (N * j));
    }
    
    // Decomposed row addition for better loop optimization
    static inline uint64_t extract_row(const MatrixType& x, uint8_t i) {
        return (x >> (N * i)) & ((1UL << N) - 1);
    }
    
    static inline MatrixType add_row(const MatrixType& x, uint64_t row_i, uint8_t j) {
        return x ^ (row_i << (N * j));
    }
    
    // Permutation: y[i][j] := x[pi[i]][pi[j]]
    static inline MatrixType permute(const MatrixType& x, const uint8_t pi[N]) {
        MatrixType y = 0;
        for (uint8_t i = N - 1; i < N; i--)
            for (uint8_t j = N - 1; j < N; j--) {
                y <<= 1;
                y |= (x >> (pi[i] * N + pi[j])) & 1;
            }
        return y;
    }
    
    static inline MatrixType permute2(const MatrixType& x, 
                                      const uint8_t pi1[N], 
                                      const uint8_t pi2[N]) {
        MatrixType y = 0;
        for (uint8_t i = N - 1; i < N; i--)
            for (uint8_t j = N - 1; j < N; j--) {
                y <<= 1;
                y |= (x >> (pi1[i] * N + pi2[j])) & 1;
            }
        return y;
    }
    
    // Identity matrix
    static inline MatrixType identity() {
        MatrixType x = 0;
        for (uint8_t i = 0; i < N; i++)
            x |= (1UL << (N * i + i));
        return x;
    }
    
    // Comparison for hashset
    static inline bool equals(const MatrixType& a, const MatrixType& b) {
        return a == b;
    }
    
    static inline bool less(const MatrixType& a, const MatrixType& b) {
        return a < b;
    }
};

// ============ Specialization for Matrix ============
template<>
struct MatrixTrait<Matrix> {
    using MatrixType = Matrix;
    using StorageType = Matrix;  // Matrix is stored directly
    
    static inline bool get(const MatrixType& x, uint8_t i, uint8_t j) {
        return x.get(i, j);
    }
    
    static inline void set(MatrixType& x, uint8_t i, uint8_t j, bool val) {
        x.set(i, j, val);
    }
    
    static inline MatrixType apply_cnot(const MatrixType& x, uint8_t i, uint8_t j) {
        return x.addrow(i, j);
    }
    
    static inline uint64_t extract_row(const MatrixType& x, uint8_t i) {
        return x.addrow1(i);
    }
    
    static inline MatrixType add_row(const MatrixType& x, uint64_t row_i, uint8_t j) {
        return x.addrow2(row_i, j);
    }
    
    static inline MatrixType permute(const MatrixType& x, const uint8_t pi[N]) {
        return x.permute(pi);
    }
    
    static inline MatrixType permute2(const MatrixType& x, 
                                      const uint8_t pi1[N], 
                                      const uint8_t pi2[N]) {
        return x.permute2(pi1, pi2);
    }
    
    static inline MatrixType identity() {
        return Matrix(true);  // Matrix constructor with true = identity
    }
    
    static inline bool equals(const MatrixType& a, const MatrixType& b) {
        return a == b;
    }
    
    static inline bool less(const MatrixType& a, const MatrixType& b) {
        return a < b;
    }
};

#endif
```

### 2. Unified Algorithm (`src/matrix_cnot_unified.cpp`)

The main algorithm becomes a template function taking `MatrixTrait`:

```cpp
template<typename MatrixType>
inline void process_cnot(const MatrixType& x, uint64_t row_i, uint8_t j,
                         hashset& prev_level, hashset& curr_level, 
                         hashset& next_level,
                         counter& orbit_sum, counter& matrix_count,
                         uint32_t depth) {
    
    using Trait = MatrixTrait<MatrixType>;
    
    MatrixType y = Trait::add_row(x, row_i, j);
    counter Stab = representative(y);
    
    if (!prev_level.contains(y) && !curr_level.contains(y) && 
        next_level.insert(y)) {
        if constexpr (SWAP == 0)
            orbit_sum += fac_N / Stab;
        else
            orbit_sum += (fac_N * fac_N) / Stab;
        matrix_count++;
        
        if constexpr (POLY == 1) {
            if (2 * (depth - 1) <= N) {
                uint8_t ess = countEssential(y);
                poly[depth - 1][ess] += (fac[ess] * fac[N - ess]) / Stab;
            }
        }
    }
}

template<typename MatrixType>
counter next_level(counter& size, hashset levels[], uint32_t depth) {
    using Trait = MatrixTrait<MatrixType>;
    // [same structure as before, using Trait:: to access operations]
}

template<typename MatrixType>
int generate_bfs(const MatrixType& start, const MatrixType& goal, 
                 uint8_t limit, hashset bfs_levels[]) {
    // [same as before]
}

// ... other functions templated on MatrixType
```

### 3. Compilation Options (`src/matrix_cnot_unified.cpp` beginning)

```cpp
// At compile time, instantiate for the desired matrix type:
// For small matrices (N ≤ 8):
//   g++ ... -DUSE_SMALL_MATRIX ...
//   → instantiates: matrix_cnot_unified<uint64_t>()
//
// For large matrices (N > 8):
//   g++ ... -DUSE_LARGE_MATRIX ...
//   → instantiates: matrix_cnot_unified<Matrix>()

#ifdef USE_SMALL_MATRIX
  using MatrixImpl = uint64_t;
  #include "matrix.h"
  #include "repr.h"
  #include "trace_back.h"
#elif defined USE_LARGE_MATRIX
  using MatrixImpl = Matrix;
  #include "matrixN.h"
  #include "reprN.h"
  #include "trace_backN.h"
#else
  #error "Define either USE_SMALL_MATRIX or USE_LARGE_MATRIX"
#endif

// ... all template functions instantiated with MatrixImpl
```

---

## Migration Path

### Phase 1: Add Trait Layer (Non-Breaking)
1. Create `src/matrix_trait.h` with specializations
2. Keep original files unchanged
3. Verify trait implementations match current behavior

### Phase 2: Create Unified Entry Point
1. Write `src/matrix_cnot_unified.cpp` with template functions
2. Add compilation flags: `-DUSE_SMALL_MATRIX` or `-DUSE_LARGE_MATRIX`
3. Link cleanly against existing repr/hashset infrastructure

### Phase 3: Consolidate (Optional)
1. Remove old `matrix_cnot.cpp` / `matrix_cnotN.cpp`
2. Use single entry point with compile-time selection
3. Maintain backward compatibility in build scripts

---

## Benefits

| Aspect | Before | After |
|--------|--------|-------|
| **Code Duplication** | ~400 lines × 2 | Single template |
| **Maintenance** | Fix bugs in two places | Single fix everywhere |
| **Adding Features** | Implement twice | Template + specializations |
| **Performance** | N/A | No overhead (inline traits) |
| **Flexibility** | Fixed at build time | Easy to add new matrix types |

---

## Challenges & Solutions

| Challenge | Solution |
|-----------|----------|
| **Hashset storage differences** | Store `StorageType` in hashset; trait provides conversion |
| **repr() returns type mismatch** | Use wrapper function that returns trait-appropriate type |
| **countEssential() implementation** | Keep algorithm identical, call via trait |
| **Algorithm-level differences** | Unify at algorithm level; dispatch via trait methods |

---

## Example: Post-Merge Build Commands

```bash
# Compile for small matrices (N ≤ 8)
g++ -o matrix_cnot_small matrix_cnot_unified.cpp -fopenmp \
  -DN=6 -DE=1 -DNAUTY=1 -DSWAP=0 -DUSE_SMALL_MATRIX \
  -O3 -DNDEBUG -march=native -Inauty/ nauty/nautyW1.a

# Compile for large matrices (N > 8)
g++ -o matrix_cnot_large matrix_cnot_unified.cpp -fopenmp \
  -DN=10 -DE=1 -DNAUTY=1 -DSWAP=0 -DUSE_LARGE_MATRIX \
  -O3 -DNDEBUG -march=native -Inauty/ nauty/nautyW1.a
```

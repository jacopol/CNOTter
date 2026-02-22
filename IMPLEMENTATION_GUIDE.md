# Trait-Based Merge: Implementation Guide

## Overview

This document provides a step-by-step guide to refactor `matrix_cnot.cpp` and `matrix_cnotN.cpp` into a single, unified codebase using the `MatrixTrait` abstraction layer.

## Why This Approach?

| Problem | Solution |
|---------|----------|
| Algorithm duplication | Template functions parameterized by `MatrixImpl` |
| Different APIs | `MatrixTrait` provides uniform interface |
| Performance loss | All trait operations are `inline` - zero runtime overhead |
| Maintainability | Single copy of algorithm logic |

---

## Key Files

| File | Purpose |
|------|---------|
| `matrix_trait.h` | Abstract interface for matrix operations |
| `matrix_cnot_unified.cpp` | Main algorithm (template-based) |
| `matrix.h` | Small matrix (uint64_t) - existing, unchanged |
| `matrixN.h` | Large matrix (Matrix class) - existing, unchanged |
| `repr.h` / `reprN.h` | Canonicalization - existing, unchanged |

---

## Architecture

### Compilation Flow

```
Compiler flags: -DUSE_SMALL_MATRIX or -DUSE_LARGE_MATRIX
                            ↓
                    Select MatrixImpl
                            ↓
                 Include appropriate headers:
                 (matrix.h + repr.h) OR (matrixN.h + reprN.h)
                            ↓
                 Instantiate templates with MatrixImpl
                            ↓
           Link against repr_nauty.o or repr_nautyN.o
```

### Runtime Behavior

For small matrices (uint64_t):
- Matrix stored in single 64-bit register
- Operations: bitwise AND/OR/XOR
- Hashing: direct value

For large matrices (Matrix class):
- Matrix stored in byte array
- Operations: method calls (inlined)
- Hashing: multi-word comparison

**Both paths execute identical algorithm logic.**

---

## Implementation Details

### 1. The MatrixTrait Pattern

Each `MatrixTrait<T>` specialization provides:

```cpp
struct MatrixTrait<uint64_t> {
    using MatrixType = uint64_t;
    using StorageType = uint64_t;
    
    static inline bool get(const MatrixType& x, i, j);
    static inline void set(MatrixType& x, i, j, val);
    static inline MatrixType apply_cnot(const MatrixType& x, i, j);
    // ... more methods
};

struct MatrixTrait<Matrix> {
    using MatrixType = Matrix;
    using StorageType = Matrix;
    
    static inline bool get(const MatrixType& x, i, j) {
        return x.get(i, j);
    }
    // ... implementations delegating to Matrix methods
};
```

### 2. Using the Trait in Algorithms

**Before (duplicated code):**
```cpp
// In matrix_cnot.cpp:
inline void process_cnot(matrix x, uint64_t row_i, byte j, ...) {
    matrix y = x ^ (row_i << j*N);
    counter Stab = representative(y);
    // ...
}

// In matrix_cnotN.cpp:
inline void Add(const Matrix &x, uint64_t row_i, byte j, ...) {
    Matrix y = x.addrow2(row_i, j);
    counter Stab = representative(y);
    // ...
}
```

**After (single implementation):**
```cpp
// In matrix_cnot_unified.cpp:
template<typename MatrixImpl>
inline void process_cnot(const MatrixImpl& x, uint64_t row_i, uint8_t j, ...) {
    using Trait = MatrixTrait<MatrixImpl>;
    
    MatrixImpl y = Trait::add_row(x, row_i, j);
    counter Stab = representative(y);  // Same representative() call!
    // ... identical algorithm
}
```

### 3. Row Extraction Optimization

The small-matrix version extracts a row once for reuse:

```cpp
// Before: duplicated
// In matrix_cnot.cpp:
uint64_t mask = (1UL<<N*(i+1)) - (1UL<<N*i);
uint64_t row_i = (x & mask) >> i*N;
// used for all j in inner loop

// In matrix_cnotN.cpp:
uint64_t row_i = x.addrow1(i);
// used for all j in inner loop

// After: unified
for (uint8_t i = 0; i < N; i++) {
    uint64_t row_i = Trait::extract_row(x, i);
    for (uint8_t j = 0; j < N; j++) {
        if (i != j) {
            process_cnot(x, row_i, j, ...);
        }
    }
}
```

---

## Migration Strategy

### Phase 1: Create Trait Layer (Safe, Non-Breaking)

1. **Create `matrix_trait.h`**
   - Define `MatrixTrait<uint64_t>` specialization
   - Define `MatrixTrait<Matrix>` specialization
   - Add generic helper functions: `countEssential()`, `testEssential()`
   - Keep `matrix.h` and `matrixN.h` unchanged

2. **Verify trait implementations**
   - Compile both `matrix_cnot.cpp` and `matrix_cnotN.cpp` as-is
   - Write unit tests for trait operations:
     ```cpp
     // Test countEssential matches original
     matrix x = read_matrix("Inputs/cycle3.txt");
     assert(countEssential(x) == /* expected value */);
     ```

### Phase 2: Create Unified Template Functions

1. **Extract core algorithm into template functions**
   - `process_cnot<MatrixImpl>()` - no return, void
   - `next_level<MatrixImpl>()` - explore all neighbors
   - `init_level<MatrixImpl>()` - initialize BFS
   - `find_level<MatrixImpl>()` - check if goal present
   - `intersect<MatrixImpl>()` - find intersection for bidirectional search

2. **Keep traits inline**
   - All `MatrixTrait::` method calls become `inline` candidates
   - No branch misprediction (compilation provides the right version)

### Phase 3: Instantiate for Both Matrix Types

Create a minimal bridge in `matrix_cnot_unified.cpp`:

```cpp
#ifdef USE_SMALL_MATRIX
    #include "matrix.h"
    #include "repr.h"
    #include "trace_back.h"
    using MatrixImpl = uint64_t;
#else
    #include "matrixN.h"
    #include "reprN.h"
    #include "trace_backN.h"
    using MatrixImpl = Matrix;
#endif

#include "matrix_trait.h"

// Now all functions are defined as templates
// but instantiated with the selected MatrixImpl type
```

---

## Handling Differences Between Versions

### Difference 1: Identity Check

**In matrix_cnotN.cpp:**
```cpp
if (!(goal==Matrix(false))) { ... }
```

**In matrix_cnot.cpp:**
```cpp
if (goal) { ... }
```

**Unified version:**
```cpp
MatrixImpl zero_matrix = 
#ifdef USE_SMALL_MATRIX
    0
#else
    Matrix(false)
#endif
;

if (!Trait::equals(goal, zero_matrix)) { ... }
```

### Difference 2: Level Size Prediction

**Two approaches:**
- `matrix_cnot.cpp`: Looks up `levelSizes[N][depth-2]`
- `matrix_cnotN.cpp`: Calls `predictSize(depth)` which wraps the lookup

**Unified version:** Use `predictSize()` function for both:
```cpp
inline uint8_t predictSize(int depth) {
    uint8_t lookup = levelSizes[std::min(N, 10UL)][depth - 2];
    return std::min(std::max(lookup + E, 3), MAX);
}
```

---

## Compilation Examples

### Old Method (Separate Executables)
```bash
# Small matrices
g++ -o matrix_cnot matrix_cnot.cpp -fopenmp -DN=6 -DE=1 ... -O3

# Large matrices
g++ -o matrix_cnotN matrix_cnotN.cpp -fopenmp -DN=10 -DE=1 ... -O3
```

### New Method (Single Source)
```bash
# Small matrices
g++ -o matrix_cnot_small matrix_cnot_unified.cpp \
    -DUSE_SMALL_MATRIX -DN=6 -DE=1 ... -O3

# Large matrices
g++ -o matrix_cnot_large matrix_cnot_unified.cpp \
    -DUSE_LARGE_MATRIX -DN=10 -DE=1 ... -O3
```

Both produce identical binaries in terms of performance—the trait layer adds zero overhead due to inlining.

---

## Testing Strategy

### Test 1: Verify Trait Operations
```cpp
#include "matrix_trait.h"

void test_trait_small() {
    uint64_t x = 0;
    MatrixTrait<uint64_t>::set(x, 0, 1, true);
    assert(MatrixTrait<uint64_t>::get(x, 0, 1) == true);
    
    uint64_t y = MatrixTrait<uint64_t>::apply_cnot(x, 0, 1);
    // Verify y has correct structure
}

void test_trait_large() {
    Matrix x;
    MatrixTrait<Matrix>::set(x, 0, 1, true);
    assert(MatrixTrait<Matrix>::get(x, 0, 1) == true);
    
    Matrix y = MatrixTrait<Matrix>::apply_cnot(x, 0, 1);
    // Verify y has correct structure
}
```

### Test 2: Verify Algorithm Equivalence
```cpp
// Compile with -DUSE_SMALL_MATRIX and run
int result_small = generate_bfs(start, goal, limit, levels);
fprintf(stderr, "Result (small): %d\n", result_small);

// Compile with -DUSE_LARGE_MATRIX and run on same input
int result_large = generate_bfs(start, goal, limit, levels);
fprintf(stderr, "Result (large): %d\n", result_large);

// Should match for matrices that fit in both representations
```

### Test 3: Performance—Verify No Overhead
```bash
# Benchmark original
time ./matrix_cnot Input.txt > /dev/null

# Benchmark unified (small matrix version)
time ./matrix_cnot_small Input.txt > /dev/null

# Should be nearly identical (< 1% difference)
```

---

## Benefits After Migration

| Aspect | Before | After |
|--------|--------|-------|
| **Lines of code** | ~800 duplicate | ~500 unified |
| **Bug fixes** | Fix in 2 places | Fix in 1 place |
| **Add feature** | Implement twice | Template + specializations |
| **New matrix type** | Rewrite algorithm | Add `MatrixTrait<NewType>` |
| **Performance** | N/A | Zero overhead (all inline) |
| **Build system** | Two targets | One source, two flags |

---

## Potential Pitfalls

### 1. **Inlining Failure**
If compiler doesn't inline trait operations:
```cpp
// Force inlining
static inline __attribute__((always_inline)) bool get(...);
```

### 2. **Different `representative()` Signatures**
The two repr headers might have different function signatures. **Solution:**
```cpp
// Ensure both declare:
counter representative(matrix_type&);  // Modifies in-place

// Call consistently:
counter Stab = representative(y);  // Works for both types
```

### 3. **Template Bloat**
If using many template instantiations, code size might increase.
**Solution:** Use explicit instantiation at end of `matrix_cnot_unified.cpp`:
```cpp
template void process_cnot<uint64_t>(...);
template void process_cnot<Matrix>(...);
// ... forces compiler to generate code once, not per call site
```

---

## Rollback Plan

If issues arise:
1. Keep original `matrix_cnot.cpp` and `matrix_cnotN.cpp` untouched
2. New code is in separate file: `matrix_cnot_unified.cpp`
3. Build scripts can revert to original binaries
4. Zero risk to existing system

---

## Next Steps

1. ✅ Phase 1: Create `matrix_trait.h` with both specializations
2. ✅ Phase 2: Create template functions in `matrix_cnot_unified.cpp`
3. ⏳ Phase 3: Add compilation/linking instructions to README
4. ⏳ Phase 4: Test equivalence on sample inputs
5. ⏳ Phase 5: Deprecate old files when confident

---

## FAQ

**Q: Will this slow down my code?**
A: No. All trait operations are marked `inline`. The compiler generates identical code paths as before. Proof: enable `-DNDEBUG -march=native -O3` and compare assembly.

**Q: Do I need to rewrite my repr module?**
A: No. The `repr.h` and `reprN.h` wrappers remain unchanged. They work with the new code automatically.

**Q: Can I add a third matrix type (e.g., GPU-based)?**
A: Yes. Add:
```cpp
template<>
struct MatrixTrait<GPUMatrix> {
    // Specialize each method for GPU operations
};
```
Then compile with `-DUSE_GPU_MATRIX`.

**Q: What about the hashset—do I need to change it?**
A: No. Hashset already adapts to different element types. The `StorageType` in the trait ensures hashing works correctly.


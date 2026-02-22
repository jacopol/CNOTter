# Quick Reference: Trait-Based Merge

## One-Page Summary

### Problem
Two 400-line files (`matrix_cnot.cpp`, `matrix_cnotN.cpp`) with identical algorithms but different matrix implementations.

### Solution
**MatrixTrait Pattern** — C++ template specialization providing a uniform interface for both representations.

---

## Core Pattern

```cpp
// Define trait (works for all matrix types)
template<typename MatrixType>
struct MatrixTrait { /* pure virtual concept */ };

// Specialize for small matrices
template<> struct MatrixTrait<uint64_t> {
    static inline uint64_t add_row(const uint64_t& x, uint64_t row, byte j) {
        return x ^ (row << N*j);  // bitwise
    }
};

// Specialize for large matrices
template<> struct MatrixTrait<Matrix> {
    static inline Matrix add_row(const Matrix& x, uint64_t row, byte j) {
        return x.addrow2(row, j);  // method call
    }
};

// Use in algorithm (works for BOTH!)
template<typename MatrixImpl>
void process_cnot(const MatrixImpl& x, uint64_t row, byte j, ...) {
    using Trait = MatrixTrait<MatrixImpl>;
    MatrixImpl y = Trait::add_row(x, row, j);  // ← Polymorphic
    // ... identical code for both types
}
```

---

## Compilation

```bash
# For N ≤ 8 (single word)
g++ -DUSE_SMALL_MATRIX -DN=6 -O3 matrix_cnot_unified.cpp

# For N > 8 (bit-packed array)  
g++ -DUSE_LARGE_MATRIX -DN=10 -O3 matrix_cnot_unified.cpp
```

---

## Key Trait Operations

| Operation | Small (`uint64_t`) | Large (`Matrix`) |
|-----------|-------------------|-----------------|
| `get(x, i, j)` | `(x >> (N*i+j)) & 1` | `x.get(i,j)` |
| `set(x, i, j, v)` | Bitwise OR/AND | `x.set(i,j,v)` |
| `add_row(x, row, j)` | `x ^ (row << N*j)` | `x.addrow2(row,j)` |
| `extract_row(x, i)` | `(x >> N*i) & mask` | `x.addrow1(i)` |
| `permute(x, pi)` | Bit-shift loop | `x.permute(pi)` |
| `equals(a, b)` | `a == b` | `a == b` |

All called via `Trait::operation()`—compiler inlines them.

---

## File Structure

```
src/
├── matrix_trait.h              ← NEW: Trait interface + specializations
├── matrix_cnot_unified.cpp     ← NEW: Single algorithm source (templates)
├── matrix.h                    ← EXISTING (unchanged)
├── matrixN.h                   ← EXISTING (unchanged)
├── repr.h                      ← EXISTING (unchanged)
└── reprN.h                     ← EXISTING (unchanged)

Documentation/
├── MERGE_STRATEGY_SUMMARY.md   ← START HERE (this position)
├── MERGE_DESIGN.md             ← Architecture deep-dive
├── IMPLEMENTATION_GUIDE.md     ← Refactoring walkthrough
├── REFACTORING_EXAMPLES.md     ← Concrete code transformations
└── MERGE_CHECKLIST.md          ← Step-by-step checklist
```

---

## Before/After Code Comparison

### BEFORE (Duplicated)
```cpp
// In matrix_cnot.cpp:
matrix y = x ^ (row_i << j*N);

// In matrix_cnotN.cpp:
Matrix y = x.addrow2(row_i, j);
```

### AFTER (Unified)
```cpp
// In matrix_cnot_unified.cpp:
template<typename MatrixImpl>
void process_cnot(const MatrixImpl& x, uint64_t row_i, uint8_t j, ...) {
    using Trait = MatrixTrait<MatrixImpl>;
    MatrixImpl y = Trait::add_row(x, row_i, j);  // Works for both!
}
```

---

## Performance

✅ **Zero Overhead** — All trait methods marked `inline`  
✅ **Identical Assembly** — Compiler generates same code as original  
✅ **Type Safe** — Errors caught at compile time  

Verify with: `g++ -S matrix_cnot_unified.cpp` (inspect .s file)

---

## Migration Phases

| Phase | Files | Effort |
|-------|-------|--------|
| 1️⃣ Trait Layer | `matrix_trait.h` | 2-4h |
| 2️⃣ Templates | `matrix_cnot_unified.cpp` | 6-8h |
| 3️⃣ Integration | Linking + testing | 2-3h |
| 4️⃣ Docs | README, comments | 1-2h |
| 5️⃣ Validation | Benchmark, compare | 3-4h |
| **Total** | | **14-21h** |

---

## Essential Template Methods to Specialize

```cpp
struct MatrixTrait<YourType> {
    using MatrixType = YourType;
    using StorageType = YourType;
    
    // Required methods (make all static inline):
    static inline bool get(const MatrixType& x, uint8_t i, uint8_t j);
    static inline void set(MatrixType& x, uint8_t i, uint8_t j, bool v);
    static inline MatrixType apply_cnot(const MatrixType& x, uint8_t i, uint8_t j);
    static inline uint64_t extract_row(const MatrixType& x, uint8_t i);
    static inline MatrixType add_row(const MatrixType& x, uint64_t row, uint8_t j);
    static inline MatrixType permute(const MatrixType& x, const uint8_t pi[N]);
    static inline MatrixType permute2(const MatrixType& x, const uint8_t pi1[N], const uint8_t pi2[N]);
    static inline MatrixType identity();
    static inline bool equals(const MatrixType& a, const MatrixType& b);
    static inline bool less(const MatrixType& a, const MatrixType& b);
};
```

---

## Common Pitfalls & Solutions

| Problem | Solution |
|---------|----------|
| Compiler doesn't inline trait calls | Add `__attribute__((always_inline))` |
| Different `representative()` signatures | Ensure both return `counter`, take matrix by ref |
| Can't find `MatrixTrait<int>` | Define specialization before use |
| Performance slower than original | Check assembly (-S flag); verify inlining |
| Linking errors | Ensure all repr/hashset symbols resolved |

---

## Testing Checklist (Quick)

- [ ] Compile both `-DUSE_SMALL_MATRIX` and `-DUSE_LARGE_MATRIX`
- [ ] Run on `cycle3.txt`, `cycle4.txt`, ..., `cycle6.txt`
- [ ] Compare output to original `matrix_cnot` (should match exactly)
- [ ] Time both versions: `time ./matrix_cnot_small < test.txt`
- [ ] Check performance within 1% of original

---

## Key Insight

**The algorithm is independent of matrix representation!**

By abstracting storage details in `MatrixTrait`, you can:
- ✅ Keep algorithm code DRY (don't repeat yourself)
- ✅ Support multiple representations (uint64_t, Matrix, GPU, distributed...)
- ✅ Maintain zero performance cost (all ops inlined)
- ✅ Adding matrix type is now trivial (just add trait specialization)

---

## Getting Help

- **Architecture?** → Read `MERGE_DESIGN.md`
- **How to refactor?** → Study `REFACTORING_EXAMPLES.md`  
- **Step by step?** → Follow `MERGE_CHECKLIST.md`
- **Implementation details?** → Reference `IMPLEMENTATION_GUIDE.md`

---

## One Command to Build Both

```bash
# Define in Makefile:
matrix_cnot_small: matrix_cnot_unified.cpp
	g++ -o $@ $< -fopenmp -DN=6 -DE=1 -DNAUTY=1 -DSWAP=0 \
	    -DUSE_SMALL_MATRIX -O3 -DNDEBUG -march=native \
	    -Inauty/ nauty/nautyW1.a

matrix_cnot_large: matrix_cnot_unified.cpp
	g++ -o $@ $< -fopenmp -DN=10 -DE=1 -DNAUTY=1 -DSWAP=0 \
	    -DUSE_LARGE_MATRIX -O3 -DNDEBUG -march=native \
	    -Inauty/ nauty/nautyW1.a

.PHONY: all
all: matrix_cnot_small matrix_cnot_large
```

Then: `make all`

---

## Remember

**This is not a hack—it's a standard, production-ready C++ pattern used in:**
- Boost libraries
- LLVM compiler
- Google's code bases
- High-performance computing libraries

All with **zero runtime overhead** ✓


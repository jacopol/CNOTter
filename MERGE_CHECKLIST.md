# Merge Migration Checklist

This checklist guides you through merging `matrix_cnot.cpp` and `matrix_cnotN.cpp` using the trait-based approach.

## ✅ Prerequisites

- [ ] Read `MERGE_DESIGN.md` (architectural overview)
- [ ] Read `IMPLEMENTATION_GUIDE.md` (step-by-step guide)
- [ ] Review `REFACTORING_EXAMPLES.md` (concrete examples)
- [ ] Existing `matrix_cnot.cpp` and `matrix_cnotN.cpp` compile and work correctly
- [ ] Existing test cases or benchmark scripts available

---

## Phase 1: Create Trait Layer

### Step 1.1: Create `src/matrix_trait.h`

- [ ] Create new file: `src/matrix_trait.h`
- [ ] Copy template code from MERGE_DESIGN.md structure
- [ ] Implement `MatrixTrait<uint64_t>` specialization
  - [ ] `get()`, `set()` using bitwise operations
  - [ ] `apply_cnot()` and `add_row()` using XOR
  - [ ] `extract_row()` for row extraction
  - [ ] `permute()` and `permute2()` 
  - [ ] `identity()`, `equals()`, `less()`
- [ ] Implement `MatrixTrait<Matrix>` specialization
  - [ ] All methods delegate to Matrix class methods
- [ ] Implement generic functions:
  - [ ] `countEssential<MatrixType>()`
  - [ ] `testEssential<MatrixType>()`
- [ ] Verify syntax by compiling: `g++ -c -I. src/matrix_trait.h`

### Step 1.2: Verify Trait Implementations

- [ ] Write small test program using `uint64_t` trait operations
  ```cpp
  #include "matrix_trait.h"
  using Trait = MatrixTrait<uint64_t>;
  uint64_t x = 0;
  Trait::set(x, 0, 1, true);
  assert(Trait::get(x, 0, 1) == true);
  ```
- [ ] Write small test program using `Matrix` trait operations
  ```cpp
  #include "matrixN.h"
  #include "matrix_trait.h"
  using Trait = MatrixTrait<Matrix>;
  Matrix x;
  Trait::set(x, 0, 1, true);
  assert(Trait::get(x, 0, 1) == true);
  ```
- [ ] Test `countEssential()` matches original behavior
  ```cpp
  // Load test matrices, verify countEssential returns same value
  // as original implementations
  ```

---

## Phase 2: Extract Template Functions

### Step 2.1: Create `src/matrix_cnot_unified.cpp`

- [ ] Create skeleton with headers and type selection:
  ```cpp
  #if defined(USE_SMALL_MATRIX)
      #include "matrix.h"
      #include "repr.h"
      #include "trace_back.h"
      using MatrixImpl = uint64_t;
  #elif defined(USE_LARGE_MATRIX)
      #include "matrixN.h"
      #include "reprN.h"
      #include "trace_backN.h"
      using MatrixImpl = Matrix;
  #else
      #error "Define -DUSE_SMALL_MATRIX or -DUSE_LARGE_MATRIX"
  #endif
  #include "matrix_trait.h"
  ```

### Step 2.2: Extract Core Algorithm Functions

For each function below, create a template version:

- [ ] **`compute_orbit_size()`**
  - [ ] Copy from either original (they're identical)
  - [ ] Verify `fac_N` is available

- [ ] **`process_cnot<MatrixImpl>()`**
  - [ ] Start with small-matrix version
  - [ ] Replace bitwise `x ^ (row_i << j*N)` with `Trait::add_row(x, row_i, j)`
  - [ ] Everything else remains identical
  - [ ] Mark `inline`

- [ ] **`init_level<MatrixImpl>()`**
  - [ ] Nearly identical for both; just change `matrix` → `MatrixImpl`
  - [ ] No `Trait::` calls needed (direct storage)

- [ ] **`next_level<MatrixImpl>()`**
  - [ ] Most complex function
  - [ ] Replace row extraction loop with `Trait::extract_row()`
  - [ ] Keep parallelization identical
  - [ ] Replace `process_cnot()` calls (now templates)
  - [ ] Keep padding, thread aggregation as-is

- [ ] **`find_level<MatrixImpl>()`**
  - [ ] Use `Trait::equals()` for comparison

- [ ] **`generate_bfs<MatrixImpl>()`**
  - [ ] Main BFS loop
  - [ ] Replace `process_goal()` check logic
  - [ ] Use `predictSize()` for both versions
  - [ ] Replace `find_level()` call with template version

- [ ] **`intersect<MatrixImpl>()`**
  - [ ] Use `Trait::equals()` to check null matrix

- [ ] **`bidirectional<MatrixImpl>()`**
  - [ ] Similar structure to `generate_bfs()`
  - [ ] Use `Trait::equals()` throughout

### Step 2.3: Compile and Test

- [ ] Compile with `-DUSE_SMALL_MATRIX`:
  ```bash
  g++ -c -I. -DUSE_SMALL_MATRIX -DN=6 src/matrix_cnot_unified.cpp -o unified_small.o
  ```
- [ ] Compile with `-DUSE_LARGE_MATRIX`:
  ```bash
  g++ -c -I. -DUSE_LARGE_MATRIX -DN=10 src/matrix_cnot_unified.cpp -o unified_large.o
  ```
- [ ] Check for any compilation errors
- [ ] Verify no `undefined reference` errors related to `representative()`, `report()`, etc.

---

## Phase 3: Integration Testing

### Step 3.1: Link Complete Binaries

- [ ] Create `matrix_cnot_small` executable:
  ```bash
  g++ -o matrix_cnot_small \
      src/matrix_cnot_unified.cpp \
      -fopenmp -DN=6 -DE=1 -DNAUTY=1 -DSWAP=0 \
      -DUSE_SMALL_MATRIX \
      -O3 -DNDEBUG -march=native \
      -Inauty/ nauty/nautyW1.a
  ```
- [ ] Create `matrix_cnot_large` executable:
  ```bash
  g++ -o matrix_cnot_large \
      src/matrix_cnot_unified.cpp \
      -fopenmp -DN=10 -DE=1 -DNAUTY=1 -DSWAP=0 \
      -DUSE_LARGE_MATRIX \
      -O3 -DNDEBUG -march=native \
      -Inauty/ nauty/nautyW1.a
  ```

### Step 3.2: Functional Testing

For each test case:
- [ ] Run original: `./matrix_cnot < test_input.txt > original_output.txt`
- [ ] Run new (small): `./matrix_cnot_small < test_input.txt > new_small_output.txt`
- [ ] Compare outputs: `diff original_output.txt new_small_output.txt`
- [ ] Should be identical (or near-identical with floating point)

Test cases to use:
- [ ] `Inputs/cycle3.txt` (smallest)
- [ ] `Inputs/cycle4.txt`
- [ ] `Inputs/cycle5.txt`
- [ ] `Inputs/cycle6.txt` (largest for small matrix)

For large matrix version:
- [ ] Run original `matrix_cnotN` on test cases
- [ ] Run new `matrix_cnot_large` on same cases
- [ ] Compare outputs match exactly

### Step 3.3: Performance Testing

- [ ] Benchmark original small: `time ./matrix_cnot_original < test_input.txt > /dev/null`
- [ ] Benchmark new small: `time ./matrix_cnot_small < test_input.txt > /dev/null`
- [ ] Record elapsed times (should be < 1% difference)
- [ ] Benchmark original large: `time ./matrix_cnotN < test_input.txt > /dev/null`
- [ ] Benchmark new large: `time ./matrix_cnot_large < test_input.txt > /dev/null`
- [ ] Record elapsed times (should be < 1% difference)

---

## Phase 4: Documentation & Cleanup

### Step 4.1: Update Build System

- [ ] Update `Makefile` or build scripts to support new approach
  - [ ] Add `matrix_cnot_small` target
  - [ ] Add `matrix_cnot_large` target
  - [ ] Optionally deprecate old targets (or keep as fallback)

- [ ] Create build instructions in `README.md`:
  ```markdown
  ## Building (Unified Version)
  
  For N ≤ 8:
  ```bash
  make matrix_cnot_small
  ```
  
  For N > 8:
  ```bash
  make matrix_cnot_large
  ```
  ```

### Step 4.2: Update Documentation

- [ ] Add note to `README.md` about the trait-based merger
- [ ] Reference `MERGE_DESIGN.md` for architectural details
- [ ] Document compilation flags in README

### Step 4.3: Code Cleanup

- [ ] Verify `matrix.h` and `matrixN.h` still used elsewhere (YES—need to keep them)
- [ ] Verify `repr.h` and `reprN.h` still used elsewhere (YES—need to keep them)
- [ ] **Do NOT delete old files yet** (keep as fallback)
- [ ] Add comments to old files pointing to new unified version

---

## Phase 5: Validation Checklist

### Final Verification

- [ ] All test cases pass for small matrix version
- [ ] All test cases pass for large matrix version
- [ ] Performance within 1% of original for both versions
- [ ] Build times reasonable (< 30 sec for each)
- [ ] No memory leaks detected (if using valgrind)
  ```bash
  valgrind --leak-check=full ./matrix_cnot_small < test_input.txt
  ```
- [ ] No thread race conditions detected (if using ThreadSanitizer)
  ```bash
  g++ ... -fsanitize=thread ... 
  ./matrix_cnot_small < test_input.txt
  ```

### Documentation Complete

- [ ] `MERGE_DESIGN.md` created ✅
- [ ] `IMPLEMENTATION_GUIDE.md` created ✅
- [ ] `REFACTORING_EXAMPLES.md` created ✅
- [ ] `MERGE_CHECKLIST.md` (this file) updated ✅
- [ ] `README.md` updated with new build instructions
- [ ] Inline code comments added explaining use of traits

---

## Phase 6: Gradual Deprecation (Optional)

Once confident in new implementation:

- [ ] Keep old `matrix_cnot.cpp` for 1-2 releases as fallback
- [ ] Mark as deprecated in comments
- [ ] Update CI/CD to build both, verify they produce identical results
- [ ] After confidence period, remove old files:
  - [ ] Delete `matrix_cnot.cpp`
  - [ ] Delete `matrix_cnotN.cpp`
  - [ ] Update build scripts
  - [ ] Document removal in CHANGELOG

---

## Rollback Plan (If Issues Arise)

If any phase fails:

1. [ ] Revert to original files (git restore)
2. [ ] Keep `matrix_trait.h` and documentation for reference
3. [ ] File issue with specific error for future investigation
4. [ ] Can attempt phased approach (smaller subset first)

---

## Key Testing Commands

```bash
# Compile both versions
g++ -o matrix_cnot_small -DUSE_SMALL_MATRIX [options] matrix_cnot_unified.cpp
g++ -o matrix_cnot_large -DUSE_LARGE_MATRIX [options] matrix_cnot_unified.cpp

# Test on sample input
./matrix_cnot_small < Inputs/cycle6.txt
./matrix_cnot_large < Inputs/cycle8.txt

# Benchmark (if original still available)
time ./matrix_cnot < Inputs/cycle6.txt > /tmp/orig.txt
time ./matrix_cnot_small < Inputs/cycle6.txt > /tmp/new.txt
diff /tmp/orig.txt /tmp/new.txt
```

---

## Estimated Timeline

| Phase | Effort | Time |
|-------|--------|------|
| Phase 1 (Trait layer) | Medium | 2-4 hours |
| Phase 2 (Template functions) | High | 6-8 hours |
| Phase 3 (Integration testing) | Medium | 2-3 hours |
| Phase 4 (Documentation) | Low | 1-2 hours |
| Phase 5 (Validation) | Medium | 3-4 hours |
| **Total** | | **14-21 hours** |

Can be done in 1-2 development days if uninterrupted.

---

## Notes

- Keep original files until fully confident in new version
- Incremental testing at each phase saves debugging time later
- The trait layer can be reused for future matrix types (GPU, distributed, etc.)
- Performance should be identical—if not, check that `inline` is working (use `-S` flag to inspect assembly)


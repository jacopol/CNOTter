// Matrix Trait Interface - Abstracts small (uint64_t) vs. large (Matrix) representations
// Allows unified algorithm implementations without code duplication
// 
// Jaco van de Pol, Aarhus University, 2025

#ifndef MATRIX_TRAIT_H
#define MATRIX_TRAIT_H

#include <cstdint>
#include <string>
#include "options.h"

#ifdef USE_LARGE_MATRIX
#include "matrixN.h"
#endif

// ============================================================================
// Base trait template - specializations follow
// ============================================================================

template<typename MatrixType>
struct MatrixTrait {
    // Specializations must define:
    // - StorageType: Type used in hashset (might differ from MatrixType)
    // - get(x, i, j): Get element at [i,j]
    // - set(x, i, j, val): Set element at [i,j]
    // - apply_cnot(x, i, j): Apply CNOT(i,j) - add row i to row j
    // - extract_row(x, i): Extract row i as uint64_t
    // - add_row(x, row, j): Add precomputed row to column j
    // - permute(x, pi): Apply permutation to rows and columns
    // - permute2(x, pi1, pi2): Apply pi1 to rows, pi2 to columns
    // - identity(): Return identity matrix
    // - equals(a, b): Equality check
    // - less(a, b): Less-than for ordering
};

// ============================================================================
// Specialization for uint64_t (small matrices N ≤ 8)
// ============================================================================
// This version stores the entire matrix in a single 64-bit word.
// Row i starts at bit position i*N.

template<>
struct MatrixTrait<uint64_t> {
    using MatrixType = uint64_t;
    using StorageType = uint64_t;
    
    // Get element at row i, column j
    static inline bool get(const MatrixType& x, uint8_t i, uint8_t j) {
        return (x >> (N * i + j)) & 1UL;
    }
    
    // Set element at row i, column j
    static inline void set(MatrixType& x, uint8_t i, uint8_t j, bool val) {
        uint64_t mask = 1UL << (N * i + j);
        if (val)
            x |= mask;
        else
            x &= ~mask;
    }
    
    // Apply CNOT(i,j): add row i to row j
    static inline MatrixType apply_cnot(const MatrixType& x, uint8_t i, uint8_t j) {
        uint64_t mask = (1UL << N) - 1;
        uint64_t row_i = (x >> (N * i)) & mask;
        return x ^ (row_i << (N * j));
    }
    
    // Extract row i for reuse across multiple j targets (optimization)
    static inline uint64_t extract_row(const MatrixType& x, uint8_t i) {
        uint64_t mask = (1UL << N) - 1;
        return (x >> (N * i)) & mask;
    }
    
    // Add pre-extracted row to column j
    static inline MatrixType add_row(const MatrixType& x, uint64_t row_i, uint8_t j) {
        return x ^ (row_i << (N * j));
    }
    
    // Permute matrix: result[i][j] = x[pi[i]][pi[j]]
    static inline MatrixType permute(const MatrixType& x, const uint8_t pi[N]) {
        MatrixType y = 0;
        for (uint8_t i = N - 1; i < N; i--)
            for (uint8_t j = N - 1; j < N; j--) {
                y <<= 1;
                y |= (x >> (pi[i] * N + pi[j])) & 1UL;
            }
        return y;
    }
    
    // Permute with different permutations for rows and columns
    static inline MatrixType permute2(const MatrixType& x,
                                      const uint8_t pi1[N],
                                      const uint8_t pi2[N]) {
        MatrixType y = 0;
        for (uint8_t i = N - 1; i < N; i--)
            for (uint8_t j = N - 1; j < N; j--) {
                y <<= 1;
                y |= (x >> (pi1[i] * N + pi2[j])) & 1UL;
            }
        return y;
    }
    
    // Create identity matrix
    static inline MatrixType identity() {
        MatrixType x = 0;
        for (uint8_t i = 0; i < N; i++)
            x |= 1UL << (N * i + i);
        return x;
    }
    
    // Read matrix from file
    static MatrixType read_matrix(const std::string& filename) {
        std::ifstream input(filename, std::ios_base::in);
        if (!input.is_open()) {
            std::cerr << "Could not open input file: " << filename << "\n";
            exit(-1);
        }
        MatrixType result = 0;
        uint8_t idx = 0;
        for (uint8_t i = 0; i < N; i++)
            for (uint8_t j = 0; j < N; j++, idx++) {
                char c = 0;
                do {
                    input.get(c);
                } while (!input.eof() && (c == ' ' || c == '\n' || c == '\t' || c == '\r'));
                if (c == '1') result ^= 1UL << idx;
                else assert(c == '0' && "Expected input 0 or 1");
            }
        return result;
    }
    
    // Pretty-print matrix
    static void pretty_print(const MatrixType& x) {
        MatrixType y = x;
        std::string delimiter(N * 2 - 1, '=');
        std::cerr << delimiter << std::endl;
        for (uint8_t i = 0; i < N; i++) {
            for (uint8_t j = 0; j < N; j++, y >>= 1)
                fprintf(stderr, "%lu ", y & 1UL);
            fprintf(stderr, "\n");
        }
        std::cerr << delimiter << std::endl;
    }
    
    // Equality comparison (for hashset and ordering)
    static inline bool equals(const MatrixType& a, const MatrixType& b) {
        return a == b;
    }
    
    static inline bool less(const MatrixType& a, const MatrixType& b) {
        return a < b;
    }
};

// ============================================================================
// Specialization for Matrix (large matrices N > 8)
// ============================================================================
// This version uses a bit-packed array stored in Matrix class.
// See matrixN.h for the Matrix class definition.

#ifdef USE_LARGE_MATRIX
template<>
struct MatrixTrait<Matrix> {
    using MatrixType = Matrix;
    using StorageType = Matrix;
    
    // Get element at row i, column j (delegates to Matrix member function)
    static inline bool get(const MatrixType& x, uint8_t i, uint8_t j) {
        return x.get(i, j);
    }
    
    // Set element at row i, column j
    static inline void set(MatrixType& x, uint8_t i, uint8_t j, bool val) {
        x.set(i, j, val);
    }
    
    // Apply CNOT(i,j): add row i to row j (delegates to Matrix method)
    static inline MatrixType apply_cnot(const MatrixType& x, uint8_t i, uint8_t j) {
        return x.addrow(i, j);
    }
    
    // Extract row i (returns as uint64_t for XOR operation)
    static inline uint64_t extract_row(const MatrixType& x, uint8_t i) {
        return x.addrow1(i);
    }
    
    // Add pre-extracted row to column j
    static inline MatrixType add_row(const MatrixType& x, uint64_t row_i, uint8_t j) {
        return x.addrow2(row_i, j);
    }
    
    // Permute matrix: result[i][j] = x[pi[i]][pi[j]]
    static inline MatrixType permute(const MatrixType& x, const uint8_t pi[N]) {
        return x.permute(pi);
    }
    
    // Permute with different permutations for rows and columns
    static inline MatrixType permute2(const MatrixType& x,
                                      const uint8_t pi1[N],
                                      const uint8_t pi2[N]) {
        return x.permute2(pi1, pi2);
    }
    
    // Create identity matrix
    static inline MatrixType identity() {
        return Matrix(true);  // Matrix(true) = identity
    }
    
    // Read matrix from file (delegates to Matrix static method)
    static MatrixType read_matrix(const std::string& filename) {
        return Matrix::read(filename);
    }
    
    // Pretty-print matrix
    static void pretty_print(const MatrixType& x) {
        x.print();
    }
    
    // Equality and ordering (enables hashset storage)
    static inline bool equals(const MatrixType& a, const MatrixType& b) {
        return a == b;
    }
    
    static inline bool less(const MatrixType& a, const MatrixType& b) {
        return a < b;
    }
};
#endif

// ============================================================================
// Generic functions that work with any matrix type via the trait
// ============================================================================

// Test if index i is essential (interacts with another index)
// Algorithm works identically for both matrix types
template<typename MatrixType>
inline bool testEssential(const MatrixType& x, uint8_t i) {
    using Trait = MatrixTrait<MatrixType>;
    
    if (!Trait::get(x, i, i))  // Diagonal must be 1 (when SWAP==0)
        return true;
    for (uint8_t j = 0; j < N; j++) {
        if (j != i && (Trait::get(x, i, j) || Trait::get(x, j, i)))
            return true;
    }
    return false;
}

// Count the number of essential indices
template<typename MatrixType>
inline uint8_t countEssential(const MatrixType& x) {
    using Trait = MatrixTrait<MatrixType>;
    
    if constexpr (SWAP == 0) {
        uint8_t ess = 0;
        for (uint8_t i = 0; i < N; i++)
            if (testEssential(x, i)) ess++;
        return ess;
    } else {
        // Count inessential indices with SWAP==1
        uint8_t ess = 0;
        for (uint8_t i = 0; i < N; i++) {
            uint8_t count = 0, jj;
            for (uint8_t j = 0; j < N && count < 2; j++)
                if (Trait::get(x, i, j)) { count++; jj = j; }
            if (count == 1) {
                bool essential = true;
                for (uint8_t k = 0; k < N; k++) {
                    if (k != i && Trait::get(x, k, jj)) {
                        essential = false;
                        break;
                    }
                }
                if (essential) ess++;
            }
        }
        return N - ess;
    }
}

#endif

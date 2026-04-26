#ifndef MATRIXN_H
#define MATRIXN_H
#include "options.h"

#include <cstdlib>
#include <cstdio>
#include <array>

using byte = uint8_t;
using mat_idx = uint64_t;

constexpr byte NC = 64 / N;           // Columns: number of rows in a single word.
constexpr byte NR = (N - 1) / NC + 1; // Rows: number of words needed (last word may not be filled)

// TODO: move to permutation.h

using perm = byte[N];       // permutation of N elements

void pretty_perm(const perm pi) {
    for (byte i=0; i<N; i++)
        fprintf(stderr,"%3u", i);
    fprintf(stderr,"\n");
    for (byte i=0; i<N; i++)
        fprintf(stderr,"%3u", pi[i]);
    fprintf(stderr,"\n");
}

// return the identity permutation in pi
inline void id_perm(perm pi) {
    for (byte i=0; i<N; i++)
        pi[i] = i;
}

// return the inverse permutation in pi_inv
inline void inv_perm(const perm pi, perm pi_inv) {
    for (byte i=0; i<N; i++)
        pi_inv[pi[i]] = i;
}

// return the composition pi = pi1 . pi2
inline void compose_perm(const perm pi1, const perm pi2, perm pi) {
    for (byte i=0; i<N; i++)
        pi[i] = pi2[pi1[i]]; // non-standard, since we permute indices
}

// return the composition pi = pi1^-1 . pi2
inline void compose_inv_perm(const perm pi1, const perm pi2, perm pi) {
    for (byte i=0; i<N; i++)
        pi[pi1[i]] = pi2[i]; // non-standard, since we permute indices
}

class Matrix {

public:
    std::array<uint64_t,NR> _bits = {{ }}; // initalizes to 0

public:

    Matrix() = default; // zero matrix

    explicit Matrix(bool diag) { // create identity when diag=true
        if (!diag) return;
        for (uint8_t i=0; i<N; i++)
            set(i,i,true);
    }

    inline bool get(uint8_t i, uint8_t j) const {
        if constexpr (NR == 1) {
            return (_bits[0] >> (N * i + j)) & 1UL;
        } else {
            div_t wordi = div(i,NC);
            uint64_t res = _bits[wordi.quot] & (1UL << (N * wordi.rem + j));
            return (res ? true : false);
        }
    }

    inline void set(uint8_t i, uint8_t j, bool val) {
        if constexpr (NR == 1) {
            uint64_t mask = 1UL << (N * i + j);
            if (val)
                _bits[0] |= mask;
            else
                _bits[0] &= ~mask;
        } else {
            div_t wordi = div(i,NC);
            uint64_t mask = (1UL << (N * wordi.rem + j));
            if (val)
                _bits[wordi.quot] |= mask;
            else
                _bits[wordi.quot] &= ~ mask;
        }
    }

    bool operator==(const Matrix &other) const { // assume unused bits are 0
        for (uint8_t i=NR-1; i<NR; i--) {
            if (_bits[i] != other._bits[i]) return false;
        }
        return true;
    }
    
    bool operator<(const Matrix &other) const { // assume unused bits are 0
        for (uint8_t i=NR-1; i<NR; i--) {
            if (_bits[i] < other._bits[i]) return true;
            if (_bits[i] > other._bits[i]) return false;
        }
        return false;
    }

    /* pretty print matrix */
    void print() const {
        std::string delimiter(N*2-1,'=');
        std::cerr << delimiter << std::endl;
            for (uint8_t i=0; i<N; i++) {
                for (uint8_t j=0; j<N; j++)
                    fprintf(stderr,"%c ", (get(i,j) ? '1' : '0'));
                fprintf(stderr,"\n");   
            }
        std::cerr << delimiter << std::endl;
    };

    static Matrix read(std::string filename) {
        std::ifstream input(filename, std::ios_base::in);
        if (!input.is_open()) { 
            std::cerr << "Could not open input file: " << filename << "\n";
            exit(-1); 
        }
        Matrix result;
        for (u_int8_t i=0; i<N; i++)
            for (u_int8_t j=0; j<N; j++) {
                char c=0;
                do {
                    input.get(c);
                } while (!input.eof() && (c==' ' || c=='\n' || c=='\t' || c=='\r'));
                if (c=='1') result.set(i,j,true);
                else {
                    assert(c=='0' && "Expected input 0 or 1");
                    result.set(i,j,false);
                }
        }
        return result;
    }

    /* For efficiency, split addition add(i,j) in two parts:
        - compute the row for i (independent of j)
        - add that row to j (can be reused for multiple j) 
    */

    uint64_t addrow1(uint8_t i) const {
        assert(i<N);
        if constexpr (NR == 1) {
            uint64_t mask = (1UL << N) - 1;
            return (_bits[0] >> (N * i)) & mask;
        } else {
            div_t wordi = div(i,NC);
            uint64_t mask = (1UL << N) - 1; // single row of 1s
            return (this->_bits[wordi.quot] & (mask << (N*wordi.rem))) >> (N*wordi.rem); // select row i
        }
    }

    Matrix addrow2(uint64_t row_i, uint8_t j) const {
        assert(j<N);
        Matrix result = *this;
        if constexpr (NR == 1) {
            result._bits[0] ^= (row_i << (N * j));
        } else {
            div_t wordj = div(j,NC);
            result._bits[wordj.quot] ^= (row_i << (N*wordj.rem));
        }
        return result;
    }

    /* Add row i to row j*/
    Matrix addrow(uint8_t i, uint8_t j) const {
        assert(i!=j && i<N && j<N);
        return addrow2(addrow1(i),j);
    };

    Matrix permute(const uint8_t pi[N]) const { // other[i][j] := this[pi[i]][pi[j]]
        if constexpr (NR == 1) {
            Matrix other;
            uint64_t y = 0;
            for (uint8_t i = N - 1; i < N; i--)
                for (uint8_t j = N - 1; j < N; j--) {
                    y <<= 1;
                    y |= (_bits[0] >> (pi[i] * N + pi[j])) & 1UL;
                }
            other._bits[0] = y;
            return other;
        } else {
            Matrix other;
            for (uint8_t i=0; i<N; i++) {
                for (uint8_t j=0; j<N; j++) {
                    other.set(i,j, get(pi[i],pi[j]));
                }
            }
            return other;
        }
    }

    Matrix permute2(const uint8_t pi1[N], const uint8_t pi2[N]) const { // other[i][j] := this[pi1[i]][pi2[j]]
        if constexpr (NR == 1) {
            Matrix other;
            uint64_t y = 0;
            for (uint8_t i = N - 1; i < N; i--)
                for (uint8_t j = N - 1; j < N; j--) {
                    y <<= 1;
                    y |= (_bits[0] >> (pi1[i] * N + pi2[j])) & 1UL;
                }
            other._bits[0] = y;
            return other;
        } else {
            Matrix other;
            for (uint8_t i=0; i<N; i++) {
                for (uint8_t j=0; j<N; j++) {
                    other.set(i,j, get(pi1[i],pi2[j]));
                }
            }
            return other;
        }
    }

};

#if SWAP==0
// Test if index i is essential (interacts with another index)
inline bool testEssential(const Matrix &x, byte i) {
    if (!(x.get(i,i)))
        return true;
    for (byte j=0; j<N; j++)
        if (j!=i && (x.get(i,j) || x.get(j,i)))
            return true;
    return false;
}

// Count the number of essential indices
inline byte countEssential(const Matrix &x) {
    byte ess=0;
    for (byte i=0; i<N; i++)
        if (testEssential(x,i)) ess++;
    return ess;
}

#else

// Count the number of ones that are lonely in their row and column
inline byte countEssential(const Matrix &x) {
    byte ess=0; // we count the inessential indices
    for (byte i=0; i<N; i++) {
        byte count=0, jj; // count number of ones and remember their column
        for (byte j=0; j<N && count<2; j++)
            if (x.get(i,j)) { count++; jj=j; }
        if (count==1) {
            bool essential=true;
            for (byte k=0; k<N; k++) // test of rest of column is zeros
                if (k!=i && x.get(k,jj)) {
                    essential = false;  
                    break;
                }
            if (essential) ess++;
            }
    }
    return N-ess;
}

#endif

#endif
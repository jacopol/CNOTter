#ifndef SMALLMATRIX_H
#define SMALLMATRIX_H

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "options.h"

class SmallMatrix {
public:
    using WordType = uint64_t;

private:
    WordType bits_ = 0;

public:
    SmallMatrix() = default;

    explicit SmallMatrix(bool diag) {
        if (!diag) {
            return;
        }
        for (uint8_t i = 0; i < N; i++) {
            set(i, i, true);
        }
    }

    explicit SmallMatrix(WordType raw_bits) : bits_(raw_bits) {}

    inline bool get(uint8_t i, uint8_t j) const {
        return (bits_ >> (N * i + j)) & 1UL;
    }

    inline void set(uint8_t i, uint8_t j, bool val) {
        const WordType mask = 1UL << (N * i + j);
        if (val) {
            bits_ |= mask;
        } else {
            bits_ &= ~mask;
        }
    }

    static inline SmallMatrix identity() {
        return SmallMatrix(true);
    }

    bool operator==(const SmallMatrix &other) const {
        return bits_ == other.bits_;
    }

    bool operator<(const SmallMatrix &other) const {
        return bits_ < other.bits_;
    }

    uint64_t addrow1(uint8_t i) const {
        assert(i < N);
        const uint64_t mask = (1UL << N) - 1;
        return (bits_ >> (N * i)) & mask;
    }

    SmallMatrix addrow2(uint64_t row_i, uint8_t j) const {
        assert(j < N);
        return SmallMatrix(bits_ ^ (row_i << (N * j)));
    }

    SmallMatrix addrow(uint8_t i, uint8_t j) const {
        assert(i != j && i < N && j < N);
        return addrow2(addrow1(i), j);
    }

    SmallMatrix permute(const uint8_t pi[N]) const {
        uint64_t y = 0;
        for (uint8_t i = N - 1; i < N; i--) {
            for (uint8_t j = N - 1; j < N; j--) {
                y <<= 1;
                y |= (bits_ >> (pi[i] * N + pi[j])) & 1UL;
            }
        }
        return SmallMatrix(y);
    }

    SmallMatrix permute2(const uint8_t pi1[N], const uint8_t pi2[N]) const {
        uint64_t y = 0;
        for (uint8_t i = N - 1; i < N; i--) {
            for (uint8_t j = N - 1; j < N; j--) {
                y <<= 1;
                y |= (bits_ >> (pi1[i] * N + pi2[j])) & 1UL;
            }
        }
        return SmallMatrix(y);
    }

    static SmallMatrix read(const std::string &filename) {
        std::ifstream input(filename, std::ios_base::in);
        if (!input.is_open()) {
            std::cerr << "Could not open input file: " << filename << "\n";
            exit(-1);
        }
        SmallMatrix result;
        uint8_t idx = 0;
        for (uint8_t i = 0; i < N; i++) {
            for (uint8_t j = 0; j < N; j++, idx++) {
                char c = 0;
                do {
                    input.get(c);
                } while (!input.eof() && (c == ' ' || c == '\n' || c == '\t' || c == '\r'));
                if (c == '1') {
                    result.bits_ ^= 1UL << idx;
                } else {
                    assert(c == '0' && "Expected input 0 or 1");
                }
            }
        }
        return result;
    }

    void print() const {
        uint64_t y = bits_;
        std::string delimiter(N * 2 - 1, '=');
        std::cerr << delimiter << std::endl;
        for (uint8_t i = 0; i < N; i++) {
            for (uint8_t j = 0; j < N; j++, y >>= 1) {
                fprintf(stderr, "%lu ", y & 1UL);
            }
            fprintf(stderr, "\n");
        }
        std::cerr << delimiter << std::endl;
    }

    inline uint64_t raw() const {
        return bits_;
    }
};

#endif

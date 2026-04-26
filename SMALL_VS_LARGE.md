# Small vs Large Matrix Performance (N=6)

## Conclusion
For small N (for example N=6), the small matrix implementation is expected to be substantially faster than the large matrix implementation, even though the matrix fits in one 64-bit word.

## Why Small Is Faster

1. Hot-path arithmetic is simpler in small mode.
- Small mode operates directly on a `uint64_t` with shifts/xors.
- Large mode uses `Matrix::get`/`set` style accessors and object-level operations, which add per-access overhead.

2. Canonicalization is heavier in large mode.
- The representative/permutation logic in large mode repeatedly calls matrix accessors in nested loops.
- Small mode performs equivalent work with direct bit operations, which are cheaper.

3. More temporary object work in large mode.
- Large mode creates and copies `Matrix` objects in several hot paths.
- Some paths initialize object state before overwriting it, adding avoidable cost.

4. Extra wrapper/conversion overhead.
- Large mode level operations go through tree/hash wrappers (`CONTAINS`/`INSERT`/`GET`) and root-to-matrix conversions during iteration.
- Small mode directly stores and iterates matrix values.

## Practical Implication
If your matrix still fits in one machine word, prefer the small representation for speed-critical runs.

## Potential Optimizations for Large Mode

1. Reduce constructor overhead in `Matrix` (avoid unnecessary identity initialization in temporary objects).
2. Pass `Matrix` by `const&` in wrappers where possible.
3. Add NR==1 fast paths that operate directly on `_bits[0]`.
4. Avoid unnecessary `GET` reconstruction during level iteration when NR==1.

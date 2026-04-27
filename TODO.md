small_matrix is superfluous.

large_matrix is now faster. This is mainly due to specialisation for NR==1

So we can keep matrix_cnot_unified.cpp.
    NOTE: double check we have all info from matrix and matrixN
    NOTE: we can remove matrix.h, repr_perm.h, traceback.h
        or replace them by their N-variants

Question: Do we still need Trait Matrix? Seems superfluous.
Might come in handy later (Clifford)

Next steps:
- do the same for nauty, i.e. keep nautyN and specialize for NR==1 as in nauty.
- update the scripts
- can we do "representative" as a "policy"?
- can we do the whole action thing as a "policy"
- we need to add <N> as a template, to enable multiple N in the same program
- we need to wrap the levels in a class, so we can combine multiple explorations

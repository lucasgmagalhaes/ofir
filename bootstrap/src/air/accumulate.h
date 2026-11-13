// Accumulator recursion (ADR 0044): `return x + f(args)` in a function `f`
// becomes a loop whose sum is checked for overflow when it returns.

#ifndef OFIR_ACCUMULATE_H
#define OFIR_ACCUMULATE_H

#include "air/air.h"
#include "sema/types.h"

// Turns the one self call whose result is added to a term and returned into
// a jump back to the entry: the terms go into an accumulator, and each return
// adds the accumulator to its value under one overflow check. A term that is
// negative or would overflow the accumulator takes the original call. The
// function must have only scalar locals, no `mut` parameters and no address
// taken.
void air_accumulate(AirBody *body, const TypeTable *types);

#endif

#pragma once

// This file is not a class, but a documentation of the concept of an
// "ios_precomputer", as implemented by the other files in this directory.
//
// An "ios_precomputer" is a generic term for a component that analyzes an
// automaton's transition function and precomputes its input/output behavior.
// The goal is to produce a data structure that partitions the input space into
// disjoint conditions. For each condition, the precomputer determines the set
// of possible output behaviors and the corresponding state transitions.
//
// This precomputation is particularly useful for determinization algorithms and
// other analyses where the automaton's response to various inputs needs to be
// efficiently queried.
//
// ## Interface
//
// All ios_precomputer implementations share a common static interface:
//
// template <typename Aut, typename TransSet = ...>
// static auto make(Aut aut, bdd input_support, bdd output_support);
//
// This `make` function returns a callable object (typically a lambda). When
// invoked, this object returns an iterable container.
//
// ## Returned Data Structure
//
// The container returned by the callable object iterates over pairs. Each pair
// consists of:
//
// 1. A `bdd` representing a disjoint input condition. The union of all these
//    BDDs covers the entire input space.
// 2. An iterable container of "IO sets". Each IO set corresponds to a unique
//    output behavior possible under the given input condition. It contains the
//    set of transitions `(p, q)` that are enabled for that specific input/output
//    combination.
//
// ## Implementations
//
// This directory contains several implementations of the ios_precomputer
// concept, each with different performance characteristics:
//
// - `standard`: This is a straightforward implementation that iterates through all
//   possible input assignments and, for each, iterates through all possible
//   output assignments to determine the resulting transition sets. While simple
//   to understand, it can be inefficient for large input/output spaces.
//
// - `powset` and `fake_vars`: These two implementations are based on the same
//   algorithmic idea: refining a set of BDDs to be unambiguous (disjoint).
//   `powset` does this by iteratively splitting BDDs, while `fake_vars` uses
//   a more complex approach with auxiliary variables to track the partitions.
//   These can be more efficient than `standard` when the transition function
//   has a regular structure.
//
// - `mona`: This implementation takes a symbolic approach, building a single
//   BDD that represents the entire input/output/transition relation. It then
//   traverses this BDD to extract the IO sets. This can be very efficient if
//   the BDD remains small, but may consume significant memory otherwise.
//
// - `delegate`: A trivial implementation that simply returns the input and
//   output support BDDs, without any precomputation. Useful for scenarios
//   where no complex analysis is needed.

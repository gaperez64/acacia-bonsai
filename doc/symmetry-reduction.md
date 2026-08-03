# Symmetry Reduction for Antichain-Based Reactive Synthesis

This document explains Acacia-Bonsai's automaton-level symmetry detection, client-block recovery, count-vector quotient, and exact equivariant solver. It was converted from LaTeX to Markdown so it can be read directly in the repository; inline mathematics uses standard Markdown math notation.

## Current status and open questions (2026-08-02)

Two solver paths are implemented: the count-vector quotient is default-off after a negative performance result, while the exact equivariant hybrid ships enabled in `best_decomp_mona`. The earlier phase-3 conclusion that the benchmark corpus contained no useful partial group was an artifact of two gates:

1. Fast recognition rooted its star at index 0 and stopped at the first failed transposition, so it could return only all of $S_n$ or nothing.
2. `ACACIA_EQUIVARIANT_MIN_BLOCKS` was 8, rejecting the plain arbiter family even though its four-block layout produced the documented 71.7× orbit collapse at six clients.

An exhaustive structural measurement on the final realizability automata gives:

| Family and measured sizes | Verified structural group | Recovered layout | Consequence |
|---|---|---|---|
| `amba_decomposed_arbiter`, $n=3\ldots8$ | $S_{n-1}$ on indices $1\ldots n-1$; index 0 fixed | 5 active-client blocks, 8 shared states for $n\ge4$ | Previously missed by the fixed-root detector |
| `load_balancer`, $n=3\ldots8$ | $S_{n-1}$ on indices $1\ldots n-1$; index 0 fixed | 5 active-client blocks, 14 shared states for $n\ge4$ | Previously missed by the fixed-root detector |
| `round_robin_arbiter`, $n=3\ldots6$ | No verified non-trivial transposition | No usable layout | Translation has destroyed the semantic surface symmetry; sizes 7–8 did not reach the final automaton within 60 seconds |
| `prioritized_arbiter`, $n=3\ldots8$ | Full $S_n$, but only output family `g_` survives | 2 blocks, 4 shared states | The indexed input family is lost before the solver, so the equivariant input reduction still declines |
| `arbiter_pb_N_pe_`, $n=5\ldots8$ | Full $S_n$ | 4 blocks, 3 shared states | Previously rejected only by the eight-block payoff gate |

The detector now selects the largest verified symmetric index component, leaving excluded indices as ordinary shared AP/state structure. The payoff threshold is 4 and is a Meson/config-registry option. Assertions-enabled and release symmetry suites pass 6/6; targeted post-change runs show the equivariant path is attempted on AMBA (3 active clients, 5 blocks), load-balancer (3, 5), and plain arbiter (5, 4), with matching realizable verdicts.

The established 994-instance protocol was repeated in both configuration orders with a 17-second cap, one test job, and an 8 GiB solver cgroup with swap disabled:

| Configuration order | Equivariant solved / PAR-2 | Classic-only solved / PAR-2 |
|---|---:|---:|
| Equivariant first | 822 / 6255.735 s | 821 / 6298.821 s |
| Classic-only first | 823 / 6212.734 s | 823 / 6248.121 s |
| Counterbalanced mean | 6234.234 s | 6273.471 s |

The equivariant configuration wins by 39.237 seconds (0.625%) on the counterbalanced mean. The two campaigns use identical 994-instance universes and contain no failures or wrong verdicts. One first-order boundary case (`detector_unreal16`) is solved in 16.15 seconds only by the equivariant run; the reversed order has identical solved counts, so the performance conclusion does not depend on that unstable timeout boundary.

The intended partial-group wins repeat across both orders: `amba_decomposed_arbiter6` averages 1.92 seconds versus 13.75 seconds, and `load_balancer7` averages 11.05 seconds versus 13.63 seconds. The established `arbiter_on_inpchange4` control remains 2.39 seconds versus 12.96 seconds.

### Why this attempt paid off

- **The earlier attempt changed the algebra; this one changes only the gate.** The count-vector quotient replaced `contains` with a max-flow transportation test and `intersect` with a capped Northwest-corner construction that recovers only about 45–60% of the exact maximal intersection points. Every downset operation acquired a strictly larger constant, while incomplete intersections can require extra Kleene iterations and $K$-increments. Because `try_solve` returns `optional<bool>`, the quotient can short-circuit only a Realizable result; otherwise its work becomes fallback overhead. Even the measured 71.7× orbit collapse did not repay those costs. The current change leaves `cpre_inplace`, `actioners::standard::apply`, and the classic downset backends bit-for-bit unchanged.
- **The cost/benefit is asymmetric by construction.** Detection is a bounded, once-per-instance cost: at most $n(n-1)/2$ label-preserving isomorphism searches on an automaton already capped by `ACACIA_EQUIVARIANT_MAX_STATES = 512`. Its benefit recurs in every fixed-point iteration, where input letters in the same verified orbit are not re-expanded. Nothing became slower inside the solving loop, so instances on which recognition succeeds can repay the fixed detection cost repeatedly.
- **The win extends the exact orbit-reuse mechanism through the representative path.** Diagnostics show three active clients on the smaller AMBA and load-balancer probes, which do dispatch to `solve_orbit_sweep`, but the headline `amba_decomposed_arbiter6` and `load_balancer7` cases have five and six active clients. Since both exceed `ACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS = 4`, they dispatch to the existing representative-input/picker path instead. Their gain comes from widening recognition so that this already-shipping exact path can reuse verified input orbits; it does not come from the retired quotient algebra. The `arbiter_on_inpchange4` sweep result remains an independent control showing that exact orbit reuse can pay, but it is not the same dispatch path as the two new headline wins.
- **Lowering `MIN_BLOCKS` from 8 to 4 does not loosen correctness.** The payoff gate is reached only after `compute_block_layout` succeeds; `generators_match_layout` and `boolean_side_consistent` still run afterward. The lower threshold merely admits instances whose recovered layout is fully validated and would previously have been discarded.
- **The count-vector theory did not transfer.** Plain `arbiter6` through `arbiter8` and AMBA sizes 7–8 still time out in both configurations, while `arbiter5` is effectively unchanged. The 0.625% corpus-level improvement is a real win from cheap orbit reuse, not the linear-orbit collapse derived later in this document.

The round-robin and prioritized front-end losses are evidence for possible TLSF provenance work, but that work is outside this change.

## Motivation and Empirical Setting

### The problem

Acacia-Bonsai reduces LTL realizability to a $K$-bounded safety game solved by a backward antichain fixed point (the *Acacia* algorithm family; see the tool paper for the base algorithm). For a family of specifications parameterized by a number of interchangeable clients $n$ (round-robin and priority arbiters, request/grant protocols, …), we observed the following on the SYNTCOMP arbiter benchmarks (automaton state count is $4n+3$, i.e. *linear* in $n$; timings for `arbiterN` on the current best acacia configuration versus `ltlsynt`):

| $n$                        |     5 |     6 |       7 |       8 |       9 |      10 |
|:---------------------------|------:|------:|--------:|--------:|--------:|--------:|
| antichain size (converged) |   743 |  2439 |       — |       — |       — |       — |
| acacia wall time           |  fast |  slow | timeout | timeout | timeout | timeout |
| `ltlsynt` wall time        | 0.03s | 0.10s |   0.59s |   3.78s |   24.5s | timeout |

Both tools are exponential in $n$ on this family; `ltlsynt` simply has a much better constant, buying it roughly three additional client-sizes before it too times out. Since the automaton itself is linear-sized, the combinatorial blow-up lives entirely in the $K$-bounded game’s *winning-region antichain*, not in the automaton.

### A negative result: binary decision diagrams do not help

The natural first hypothesis is that the antichain’s blow-up is *representational* — many near-duplicate, permutation-related vectors that a shared, canonical structure such as a BDD could compress — rather than information-theoretically large. We tested this directly: we dumped the converged antichains for `arbiter3`–`arbiter6` and built a BDD (BuDDy) of their downward closure, bit-blasting each coordinate’s counter value and using dynamic variable reordering (sifting) to find a near-optimal variable order. The result is negative and clean:

| $n$ | antichain vectors | sifted BDD nodes | nodes/vectors |     |
|----:|------------------:|-----------------:|--------------:|----:|
|   3 |                45 |              211 |          4.69 |     |
|   4 |               189 |              645 |          3.41 |     |
|   5 |               743 |             2111 |          2.84 |     |
|   6 |              2439 |             6257 |          2.57 |     |

Both the antichain size and the BDD node count grow exponentially at *the same base* ($\approx 3\times$ per client), with the BDD consistently using *more* nodes than there are antichain elements. No variable ordering linearizes the structure: the “middle section” of each vector genuinely carries $\Theta(n)$ bits of information about which client holds which of a bounded number of counter values, subject to mutual exclusion and fairness constraints, and this information is not compressible by node sharing. **A BDD-backed downset would push the memory wall out by only a constant factor, not break the exponential.**

### A positive result: the blow-up is exactly a symmetric-group orbit-counting problem

Arbitration clients are *interchangeable*: the specification (and hence the winning region) is invariant under any permutation of client indices. We tested whether *canonicalizing* the antichain under this symmetry — keeping one representative per orbit of the permutation action — collapses the blow-up, again by direct measurement on the same dumped antichains. Column-signature analysis first confirmed the expected block structure: each vector’s coordinates split into exactly $4$ blocks of size $n$ (one automaton state per client, per structural role) plus $3$ client-independent (“shared”) coordinates, i.e. $4n+3$ total, matching the automaton’s state count exactly. We then verified, empirically, that *every* pairwise client transposition preserves the antichain (i.e. the full symmetric group $S_n$ is a genuine automorphism group of the winning region) and computed orbit counts:

| $n$ | antichain vectors | valid client transpositions | orbits | collapse factor |     |
|----:|------------------:|----------------------------:|-------:|----------------:|----:|
|   3 |                45 |          $3/3$ (full $S_3$) |     13 |     3.5$\times$ |     |
|   4 |               189 |          $6/6$ (full $S_4$) |     20 |     9.4$\times$ |     |
|   5 |               743 |        $10/10$ (full $S_5$) |     27 |    27.5$\times$ |     |
|   6 |              2439 |        $15/15$ (full $S_6$) |     34 |    71.7$\times$ |     |

The orbit count is *linear* in $n$ ($7n-8$, exactly), while the raw antichain is exponential ($\approx 3^n$). This is the result the BDD experiment could not deliver: a genuinely sub-exponential — here, linear — representation of the winning region, obtained purely by exploiting the permutation symmetry rather than by attempting node sharing on an unstructured encoding. The remainder of this document derives a sound algorithm that computes with this canonical representation directly, without ever materializing the exponential raw antichain or enumerating a permutation group of size $n!$.

## Background: the $K$-Bounded Safety Game

Fix a universal co-Büchi automaton $\mathcal{A}= (Q, q_0, \delta, \mathrm{Acc})$ over an alphabet partitioned into input letters $I$ and output letters $O$, obtained from the (possibly negated) LTL specification. A *vector* is a function $v : Q\to \{-1, 0, \dots, K\}$; $-1$ denotes “no obligation remaining” and each non-$(-1)$ value is a bound on how many more times an accepting state may be visited before the run must be declared losing for the safety abstraction. Vectors are ordered pointwise ($u \succeq v \iff \forall q.\, u(q) \geq v(q)$), and a *downset* is a set closed downward under $\succeq$; every downset is determined by its antichain of $\succeq$-maximal elements.

For a fixed input $i \in I$ and output $o \in O$ compatible with $i$, the *backward action* $\widehat{\mathrm{Pre}}(\cdot, i, o) : \{-1,\dots,K\}^Q\to
\{-1,\dots,K\}^Q$ propagates a vector one step backward along the transitions labelled $(i,o)$, decrementing on visits to accepting states and flooring at $-1$ (this is the operator implemented, unchanged, by `actioners::standard::apply` in the codebase; we reuse it verbatim throughout this document and do not re-derive or modify it). The controllable predecessor of a downset $F$ is $$\mathrm{CPre}(F) \;=\; F \,\cap\, \bigcap_{i \in I} \bigcup_{o\ \mathrm{compat.}\ i} \widehat{\mathrm{Pre}}(F, i, o),$$ and Acacia computes the greatest fixed point of $\mathrm{CPre}$ starting from the $K$-bounded “safe” downset, incrementing $K$ when the initial vector $\iota$ (with $\iota(q_0)=0$, $\iota(q)=-1$ otherwise) drops out of the current iterate. The specification is realizable iff this process converges with $\iota$ inside the fixed point for some $K \leq K_{\max}$. Because the fixed point is computed as a *monotonically shrinking* Kleene iteration starting above the true answer, **any operator we substitute for an exact one, provided it only ever removes points that a correct computation would also remove (i.e. computes a subset of the exact result at every step), yields a sound under-approximation of the true fixed point**: whatever survives to the end is genuinely in the true winning region, so the final verdict is trustworthy whenever it is definitive; the only possible defect is spurious non-convergence (falling back to Unknown, or an unnecessary $K$-increment) where an exact computation would have concluded. This one fact underwrites every soundness argument in the remainder of the document.

## Detecting and Verifying the Symmetry Group

We want a permutation group $\Phi$ acting on the states $Q$ of $\mathcal{A}$ such that the $K$-bounded game is $\Phi$-equivariant, so that we may canonicalize downsets under $\Phi$ without changing the winner. We restrict attention to $\Phi$ generated by *client-index transpositions*: pairs of indices $a, b$ (read off atomic-proposition names that share a prefix and differ only in a trailing integer, e.g. $r_a,
r_b$, $g_a, g_b, \dots$) such that swapping every occurrence of index $a$ and index $b$, across every such indexed family simultaneously, is a structural automorphism of $\mathcal{A}$.

**Definition 1** (Structural automorphism). *Let $\pi$ be a permutation of the atomic propositions of $\mathcal{A}$ that maps inputs to inputs and outputs to outputs, and let $\phi : Q\to Q$ be a bijection. The pair $(\phi,\pi)$ is a *structural automorphism* of $\mathcal{A}$ if $\phi(q_0) = q_0$, and for every edge $q
\xrightarrow{c} q'$ of $\mathcal{A}$ there is an edge $\phi(q)
\xrightarrow{\pi(c)} \phi(q')$ with the same acceptance marking, and vice versa.*

**Theorem 1** (Soundness of canonicalization under a verified automorphism). *If $(\phi,\pi)$ is a structural automorphism of $\mathcal{A}$, then $\phi$ acts on vectors coordinatewise ($(\phi \cdot v)(q) = v(\phi^{-1}(q))$), $\widehat{\mathrm{Pre}}$ is $\phi$-equivariant — $\widehat{\mathrm{Pre}}(\phi\cdot v,\, \pi(i),\, \pi(o)) = \phi \cdot
\widehat{\mathrm{Pre}}(v, i, o)$ — and consequently $\mathrm{CPre}$ is $\Phi$-equivariant for $\Phi= \langle \phi \rangle$: $\mathrm{CPre}(\Phi\cdot F) =
\Phi\cdot \mathrm{CPre}(F)$ for every downset $F$. Since the initial safe downset is $\Phi$-invariant and $\iota$ is fixed by every $\phi \in
\Phi$ (as $\phi(q_0)=q_0$), every iterate of the Kleene sequence computing the greatest fixed point of $\mathrm{CPre}$, and hence the fixed point itself, is $\Phi$-invariant.*

The proof is immediate from the definitions and the fact that $\delta$ is carried faithfully by $(\phi,\pi)$; it is exactly the standard automorphism argument for symmetry reduction in model checking, specialized to this particular backward operator. The practical content of Theorem 1 is that we never need to *trust* a candidate symmetry derived from, say, the surface syntax of the LTL formula: we can and do *verify it structurally on the automaton* before using it, and discard any candidate that fails verification. Since discarding a generator only shrinks $\Phi$, this is conservative by construction (Remark 1).

**Remark 1** (Conservativeness of partial detection). *If detection finds only a strict subgroup of the true (semantic) symmetry group of the specification — because, e.g., our syntactic candidate generation misses a symmetry not reflected in AP naming, or because a genuine symmetry fails our *structural* (automaton-level) verification even though it holds *semantically* (language-level) — the algorithm still loses nothing in soundness, only in the amount of reduction achieved. We never assume a symmetry we have not verified.*

### Candidate generation and structural verification

Candidates are read off directly from AP names: every atomic proposition matching `prefix_index` (a maximal trailing run of digits) is grouped by prefix into an indexed *family*; the client-index set is the intersection of the index sets of every family. For every pair of client indices $(a,b)$ we test whether the transposition $\pi_{ab}$ — swap $a \leftrightarrow b$ simultaneously in every family — is realized by a structural automorphism, and if so, record the induced state permutation $\phi_{ab}$ as a verified generator.

Because the game automaton $\mathcal{A}$ is in general *non-deterministic* (this is the case for every automaton arising in this pipeline, including the small arbiter instances), we cannot simply propagate $\phi$ forward from the initial state by following unique successor edges as one could for a deterministic automaton. Instead we build $\mathcal B = \pi_{ab}(\mathcal{A})$ by literally relabelling the atomic propositions of a copy of $\mathcal{A}$ (a BDD-variable-pair substitution on every edge condition), and then search for a *label-preserving isomorphism* $\phi : \mathcal{A}\to \mathcal B$ — a bijection on states respecting edge labels, acceptance marks, and the initial state — via a backtracking search with signature-based pruning (each state’s signature is its acceptance bit, in-degree, and the sorted multiset of (label, acceptance) pairs on its outgoing edges; this is not merely a heuristic filter but a necessary condition satisfied by any valid $\phi$, so pruning on it never discards a correct assignment). If such a $\phi$ exists, it is (by construction of $\mathcal B$) exactly a witness that $(\phi,\pi_{ab})$ is a structural automorphism, and is recorded as a generator; if the search fails, $\pi_{ab}$ is silently discarded.

We say $\Phi$ is *full-symmetric* on the client-index set $\{i_1,\dots,i_n\}$ when the verified transpositions generate $S_n$. A verified star rooted at one index is sufficient: the transpositions on the edges of any connected graph generate the full symmetric group on that graph's vertices. The fast detector therefore tries roots until it finds the largest verified star, and it never assumes an edge that did not pass structural verification. Indices outside the chosen component are not permuted and are handled as shared AP/state structure.

### Empirical confirmation on real automata

On the plain arbiter family, exhaustive detection recovers the full symmetric group on the actual non-deterministic game automata. On AMBA and load-balancer, it recovers the maximal $S_{n-1}$ component on indices $1,\dots,n-1$, which is the partial group the earlier fixed-root detector could not report.

## Recovering the Client-Block Structure

Given a verified full-symmetric $\Phi= S_n$ on client indices, we next need to know, for the purpose of representing vectors canonically, *which automaton states are “owned” by which client, and in which structural role* — i.e., the partition of $Q$ into $B$ *blocks* of size $n$ (one state per client per role) plus a set of *shared* states fixed by every generator.

**Definition 2** (Orbit partition). *Let $\Gamma = \langle \phi_{ab} : (a,b)\ \text{verified} \rangle$ be the permutation group on $Q$ generated by the verified generators. The orbits of $\Gamma$ acting on $Q$ partition the automaton states; we require every orbit to have size $1$ (a *shared* state) or size $n$ (a *client-block* state), and reject (fall back to no symmetry exploitation) if any other orbit size occurs.*

Orbits are computed by a standard union–find over the generator action (apply every generator to every state and union the endpoints), which is polynomial in $|Q|$ and the number of generators ($O(n^2)$).

### Slot identity across blocks

Knowing the orbit partition is not yet enough: to build a client’s *type* (its value in every block simultaneously) we need a *consistent* labelling of “which state in block $j$” corresponds to “the same client” as “which state in block $j'$”. Since automaton states carry no inherent client label beyond what the group action reveals, we recover this labelling via each state’s *stabilizer signature*.

**Proposition 1** (Stabilizer signatures separate clients for $n\geq 3$). *For $\Phi\cong S_n$ acting naturally on $n$ labelled points with $n\geq 3$, the point-stabilizer of point $k$ — the subgroup fixing $k$, here identified with the *subset of generators $\phi_{ab}$ that fix $k$*, i.e. those with $k \notin \{a,b\}$ — is a distinct subgroup for each $k \in \{0,\dots,n-1\}$, and hence a distinct *signature* (as a bit-vector over the generator list). This fails for $n=2$: the unique generator $\phi_{01}$ moves both points, so both have the identical (empty) signature.*

We therefore compute, for every state $s$ in a client-block, its signature $\mathrm{sig}(s) = \{\, k : \phi_k(s) = s \,\}$ (indices into the verified generator list), fix a canonical labelling on one reference block by sorting its $n$ states by raw index, and match every other block’s states to the reference by exact signature equality. We *verify at run time* that (a) the reference block’s $n$ signatures are pairwise distinct, (b) every other block has exactly one state per reference signature, and decline (return “no usable layout”) if either check fails — in particular, and by design, whenever $n=2$ specifically. This is again a conservative choice: $n=2$ is not the regime this optimization targets (Table in §1.3 shows the blow-up only bites for $n\gtrsim 5$), so we decline rather than engineer a bespoke, harder-to-verify tie-break for a case of no practical interest here.

### Validation

The algorithm was validated two ways. First, synthetically: on hand-built groups emulating $(n,B,S)$-structured automata for $n\in\{2,3,5,7\}$, $B\in\{1,3,4\}$, $S\in\{0,2\}$ (24 configurations), the recovered layout matches the intended structure exactly in every $n\geq3$ case, and $n=2$ correctly declines. Second, against the *real* arbiter automata’s detected generators (not synthetic): the recovered layout is $4$ blocks, $3$ shared states for every $n=3,4,5,6$ instance — matching, exactly, an entirely independent offline analysis of the antichain’s raw column signatures (§1.3). Two unrelated analyses — one on automaton structure, one on antichain data — agreeing exactly is strong corroborating evidence that the recovered block structure is the correct one.

## Canonical Representation: Count-Vectors

**Definition 3** (Type, count-vector). *Fix a client-block layout with $B$ blocks and shared coordinates $S$. A client’s *type* is its tuple of values across the $B$ blocks, an element of $D = \{-1,\dots,K\}^B$; write $r = |D| \leq (K+2)^B$. A *count-vector* is a pair $(s, c)$ where $s \in \{-1,\dots,K\}^{|S|}$ is the shared part and $c : D \to \mathbb N$ records, for each type, how many of the $n$ clients realize it ($\sum_{t} c(t) = n$).*

**Proposition 2** (Count-vectors are exactly $\Phi$-orbit representatives). *Two raw vectors $u, v$ (agreeing on the shared part) lie in the same $\Phi$-orbit if and only if they induce the same count-vector.*

This is immediate: $\Phi= S_n$ acts by arbitrarily permuting which client owns which type, so the orbit of a vector is exactly determined by the *multiset* of client types, i.e. by $c$. Crucially, $r$ is bounded by a constant *independent of $n$* (it depends only on $B$ and the current $K$), so a count-vector has boundedly many nonzero entries regardless of how many clients there are — this is the representation that achieves the linear orbit count of §1.3, in contrast to the raw antichain (exponential in $n$) and the BDD encoding (also exponential in $n$, §1.2).

## Domination Algebra on Count-Vectors

Acacia’s downset representation needs three operations: membership (*contains*), *union*, and *intersect*. We derive each for count-vectors.

### Exact: Contains via bipartite transportation feasibility

**Proposition 3** (Domination as a transportation problem). *A count-vector $u=(s_u,c_u)$ dominates $v=(s_v,c_v)$ (i.e. some raw realization of $u$ dominates some/every raw realization of $v$ under the same client-index assignment) iff $s_u \succeq s_v$ pointwise *and* there is a feasible transportation plan from supply $c_u$ to demand $c_v$ using only edges $(t,t')$ with $t \succeq t'$ (pointwise domination of type-tuples).*

*Proof sketch.* A flow of value $n$ through the bipartite network with left nodes $D$ (capacity $c_u(t)$), right nodes $D$ (capacity $c_v(t')$), and edges exactly $\{(t,t') : t \succeq t'\}$ decomposes into unit flows, each specifying that one client realizing $u$-type $t$ is paired with one client realizing $v$-type $t'$ with $t \succeq t'$; conservation at every node guarantees every $u$-client and every $v$-client is matched exactly once. Such a decomposition exists iff the network admits a flow saturating all demand, i.e. iff $\max\text{-flow} = n$ (since $\sum c_u = \sum c_v = n$). Conversely, a max flow of value $n$ gives exactly such a pairing, i.e. a bijection between the $n$ clients of one realization and the $n$ clients of the other, respecting the required pointwise domination client-by-client — precisely the definition of raw vector domination. ◻

This is exact and polynomial in $r$ (independent of $n$): the network has $O(r)$ nodes, so a standard max-flow algorithm (we use a simple Edmonds–Karp-style augmenting-path search, pushing full bottleneck capacity per augmentation, giving $O(r)$ augmentations independent of the magnitude of $n$) suffices. Union of two count-vector antichains reduces to pairwise dominance filtering with the same test — no additional combinatorics is required, since the antichain-of-maximal- elements of a union of downsets is simply the union of the two antichains with dominated elements removed.

**Remark 2** (Validation). *Contains and Union were validated by $1500$ randomized trials cross-checked against brute-force enumeration of all $n!$ permutation pairings for small $n$ (up to $6$–$7$ clients, $B$ up to $4$, $K$ up to $4$): $100\%$ agreement.*

### Inexact by necessity: Intersect

The classical antichain identity $${\downarrow}A \cap {\downarrow}B = {\downarrow}\{\, \mathrm{meet}(a,b) : a \in A,\ b \in B \,\}$$ still holds for count-vectors, but the set of achievable meet outcomes from pairing two type-distributions *depends on which client-to-client pairing (transportation plan) is used* — unlike Contains, which only asks whether *some* pairing exists, Intersect needs the *maximal achievable* meet outcomes over *all* pairings, i.e. effectively the extreme points of the transportation polytope between $c_u$ and $c_v$. We were unable to find a polynomial (in $r$) characterization of this set; the number of extreme points of a transportation polytope is not bounded by a polynomial in the number of supply/demand nodes in general.

#### The resolution: soundness does not require completeness

**Lemma 1** (Under-approximating Intersect is sound). *Let $I(A,B)$ be any procedure that returns a subset of the exact antichain-of-maximal-elements of ${\downarrow}A \cap {\downarrow}B$, where every returned point is a genuinely achievable meet (i.e. realized by some valid transportation plan between some $a\in A$ and some $b\in B$). Then substituting $I$ for exact intersection throughout Acacia’s Kleene iteration (§2) preserves soundness: every Realizable/Unrealizable verdict reached remains correct.*

*Proof.* By the greatest-fixed-point monotonicity argument of §2: since $I(A,B) \subseteq {\downarrow}A \cap {\downarrow}B$ at every step, the computed sequence of iterates is pointwise $\subseteq$ the exact Kleene sequence, hence the computed fixed point is a subset of the true winning region. Anything that survives (in particular the initial vector $\iota$) is therefore genuinely winning. The only possible defect is the computed fixed point excluding points the exact computation would have kept, which can only cause spurious non-termination or an unnecessary $K$-increment, never a false positive. ◻

#### A concrete, bounded construction

We generate candidate merges via the *Northwest-corner* construction from classical transportation theory: sort both type-multisets by a chosen key and greedily match largest-with-largest (or by any other fixed orientation), producing, by construction, *one* valid transportation plan of size $O(r)$ in $O(r\log r)$ time. We use four orientations (sum ascending/descending on each side) per antichain-element pair, giving a bounded ($O(1)$ per pair) set of candidates; each is, individually, a provably valid achievable point (Lemma 1’s hypothesis is satisfied trivially, by construction, for every candidate we emit), so soundness is structural and requires no separate proof per candidate — only *which* maximal points are found is heuristic.

**Remark 3** (Validation). *Soundness (never emitting a point outside the true intersection) was validated by $400$ single-pair and $80$ multi-element-antichain randomized trials, cross-checked against brute-force enumeration of the true intersection antichain (via all $n!$ pairings): *zero* unsound points across all trials. Completeness (recovering literally every true maximal point) is, as expected, partial: roughly $45$–$60\%$ of true maximal points are recovered exactly, which is immaterial to correctness by Lemma 1 but is recorded as a quality signal.*

## Assembling the Symmetric Step

We now assemble §6’s operations into a replacement for one iteration of $\mathrm{CPre}$ that keeps $F$ in canonical (count-vector, $\Phi$-invariant) form throughout, never materializing a raw antichain or enumerating $\Phi$.

### Representative inputs via Young subgroups

For a specific raw input $i$ (say, “exactly $k$ of $n$ clients request”), its stabilizer $\mathrm{Stab}_\Phi(i) \leq \Phi$ is the *Young subgroup* $S_k \times S_{n-k}$: permutations may freely rearrange the $k$ requesting clients among themselves, and freely rearrange the $n-k$ non-requesting clients among themselves, but may not move a client across the two groups. Consequently the orbit of $i$ under $\Phi$ is exactly “every input with the same requesting count $k$”, and there are only $n+1$ such orbits ($k=0,\dots,n$) — linear in $n$, as opposed to the $2^n$ raw inputs.

### The obstacle: no canonical split of a canonical vector’s clients

Fix a representative input $i$ for orbit $k$. We want $$T_i \;=\; \bigcup_{o\ \mathrm{compat.}\ i} \widehat{\mathrm{Pre}}(F, i, o),$$ computed from $F$’s canonical (unsplit) antichain. The obstacle is that $\widehat{\mathrm{Pre}}(\cdot,i,o)$, for the *fixed*, non-symmetric $i$, is only $\mathrm{Stab}_\Phi(i)$-equivariant, not $\Phi$-equivariant (a straightforward adaptation of the proof of Theorem 1, restricting to $\phi \in \mathrm{Stab}_\Phi(i)$). A canonical $u \in F$ does not itself record *which* of its (interchangeable, same-typed) clients would be “the $k$ requesting ones” were we to realize a concrete raw vector and apply $i$ to it — different choices of which clients play the requesting role can, in general, yield genuinely different $\widehat{\mathrm{Pre}}$ outcomes, so there is no single canonical answer.

### The resolution: reuse the Intersect soundness argument

Since $T_i$ only ever appears as a factor intersected into $F$, exactly the same reasoning as Lemma 1 applies: an under-approximated $T_i$ (any subset of the true $T_i$ built entirely from genuinely achievable points) keeps the overall computation sound. We therefore build $T_i$ from a *bounded set of candidate client-to-role assignments* per canonical $u$ — concretely, sort $u$’s clients by a small fixed family of keys (total value, sum of the first coordinate, ascending and descending) and assign the top $k$ to the requesting role — realize each candidate as one concrete raw vector, apply the *existing, unmodified* backward action $\widehat{\mathrm{Pre}}$ (`actioners::standard::apply`, reused verbatim: no new game semantics are introduced or need re-verification), convert the raw result back to canonical form, and take the union (§6) over all candidates and all compatible outputs $o$.

**Proposition 4** (Un-splitting is exact, not approximate). *Converting a raw result vector back to an (unsplit) canonical count-vector via the type-counting map of §5 loses no information relative to any intermediate “which clients were assigned the requesting role” bookkeeping: that assignment is external scaffolding used only to construct one valid raw vector, and is never part of the raw vector’s coordinates.*

### The resulting algorithm

Inputs: canonical antichain $F$ (a list of count-vectors), block layout $L$, representative input $i$, and its compatible outputs $O_i$.

```text
C := empty
for each u in F:
  for each split key κ in {sum↑, sum↓, first-coordinate↑, first-coordinate↓}:
    v_raw := Realize(u, L, κ)
    for each o in O_i:
      r_raw := ActionerApply(v_raw, i, o, backward)
      C := C union {ToCount(r_raw, L)}
T_i := Union(C)                 # exact
return Intersect(F, T_i)        # capped-sound
```

The result is a canonical, sound under-approximation of $F \cap \bigcup_{o\in O_i}\widehat{\mathrm{Pre}}(F,i,o)$.

The outer solve loop (mirroring `k_bounded_safety_aut::solve`) iterates the algorithm above over the $n+1$ representative inputs until none changes $F$, checks $\textsc{Contains}(F,\iota)$ (§6, exact) for convergence, and increments $K$ exactly as the existing solver does (shifting the counting-coordinate entries of every type in every count-vector by the increment; no new algebra is needed for this step).

**Theorem 2** (End-to-end soundness). *The algorithm obtained by replacing `cpre_inplace` with the symmetric step above, run to the same convergence criterion as the existing solver, never returns Realizable for an unrealizable specification nor Unrealizable for a realizable one. It may, relative to the exact antichain algorithm, answer Unknown (or perform an unnecessary $K$-increment) on instances the exact algorithm would have resolved.*

*Proof.* Compose Theorem 1 (canonicalization under a verified automorphism is exact and loses nothing) with Lemma 1 applied twice: once directly to Intersect in the final line of the symmetric step, and once to the construction of $T_i$ itself (a union of genuinely achievable points is, a fortiori, a subset of the true $T_i$, and this subset relation is exactly what Lemma 1’s hypothesis needs). Both substitutions are one-directional (subset-of-true), so their composition is as well; the greatest-fixed-point argument of §2 then gives the result. ◻

## Implementation Status and Remaining Work

The repository contains the core symmetry modules, a default-off quotient decision path, and the default-on exact equivariant hybrid. The main components are under `src/solver/`, with corresponding tests in the `symmetry` Meson suite:

- `symmetry.hh` — detection and structural verification of client-transposition automorphisms (§3), including maximal verified partial components.

- `symmetric_blocks.hh` — client-block and slot recovery via stabilizer signatures (§4); validation includes a partial $S_{n-1}$ group plus one fixed index.

- `symmetric_downset.hh` — the count-vector domination algebra (§6): exact Contains and Union, capped-sound Intersect; validated by thousands of randomized cross-checks against brute-force ground truth, with zero soundness violations observed.

- `symmetric_conversion.hh` — raw-vector $\leftrightarrow$ count-vector conversion and the bounded candidate-split key family used in the symmetric step; validated by exhaustive round-trip tests.

- `symmetric_k_bounded_safety_aut.hh` — the live quotient fixed-point loop, representative input/output dispatch, bounded `union_o` construction, and fallback boundary to the existing solver. This path is decision-only and guarded by `ACACIA_ENABLE_SYMMETRIC_SOLVER`.

- `symmetric_dense_downset.hh` and `symmetric_profile.hh` — the current dense/SIMD-oriented adapter and profiling hooks used to measure and optimize the quotient hot path. Dense union is exact-equivalent to the sparse implementation; dense intersection preserves the same bounded-sound semantics as the sparse path.

- `equivariant_k_bounded_safety_aut.hh` — the exact hybrid that keeps the raw antichain, reuses work across verified input orbits, and closes predecessor results under verified state generators.

The quotient integration remains experimental and default-off. It is bounded by explicit work caps and falls back to the existing antichain solver whenever symmetry is absent, representative construction is capped, or the quotient loop does not reach a trusted decision cheaply enough. The exact equivariant path is enabled for realizability decisions and conservatively declines when its structural or payoff checks fail. Strategy extraction still requires a canonical-representative tie-break to reconstruct a concrete symmetry-*breaking* strategy and remains out of scope.

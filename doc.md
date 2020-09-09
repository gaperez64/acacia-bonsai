# Definitions

An *infinite word* \(\alpha\) over an *alphabet* \(A\),
i.e. \(\alpha \in
A^\omega\), can be seen as a function
\(\alpha : \mathbb{N}_{> 0} \to A\). Thus, we write \(\alpha(i)\) to
refer to the \(i\)-th letter from \(\alpha\). We also write
\(\alpha(i..j)\) to denote the infix
\(\alpha(i)\alpha(i+1)\dots\alpha(j-1)\alpha(j)\); \(\alpha(..j)\)
instead of \(\alpha(1..j)\); and \(\alpha(i..)\) to denote the infinite
suffix \(\alpha(i)\alpha(i+1)\dots\)

## Büchi and parity automata

An (\(\omega\)-word) *automaton* is a tuple \(\mathcal{N}=
  (Q,q_0,A,\Delta)\) where \(Q\) is a finite set of states,
\(q_0 \in Q\) is the initial state, \(A\) is a finite alphabet, and
\(\Delta \subseteq Q \times A
  \times Q\) is the transition relation. Since we are interested in
infinite words, we will henceforth assume that for all \(p \in Q\) and
all \(a \in A\) there exists \(q \in Q\) such that \((p,a,q) \in Q\).

A run of \(\mathcal{N}\) on a word \(\alpha \in A^\omega\) is an
infinite sequence \(\rho =
q_0 \alpha(1) q_1 \alpha(2) \dots \in (Q\cdot A)^\omega\) such that
\((q_i,\alpha(i+1),q_{i+1}) \in \Delta\) for all \(i \in \mathbb{N}\).
The automaton is said to be *deterministic* if for all \(p \in Q\) and
all \(a \in A\) we have that \((p,a,q_1), (p,a,q_2) \in \Delta\) implies
\(q_1 = q_2\).

Automata are paired with a condition which determines which runs are
*accepting*. In this work we will consider two conditions.

  - The *Büchi* condition is defined with respect to a set of *Büchi* or
    accepting states \(B \subseteq Q\). A run \(\rho = q_0
        a_1 q_1 a_1 \dots\) is accepting for this condition if and only
    if for all \(i \in \mathbb{N}\) there exists \(j \ge i\) such that
    \(q_j \in B\). That is, the run visits accepting states infinitely
    often.

  - The *parity* condition is defined with respect to a *priority
    function* \(p : Q \to \mathbb{N}\). A run \(\rho = q_0 a_1
        q_1 a_1 \dots\) is accepting for the parity condition if and
    only if the value \(\liminf_{i \to \infty} p(q_i)\) is even.

A word \(\alpha\) is accepted by the automaton \(\mathcal{N}\) if it has
a run on \(\alpha\) that is accepting. We denote by
\(\mathcal{L}({\mathcal{N}})\) the *language* of the automaton
\(\mathcal{N}\), that is, the set of words that \(\mathcal{N}\) accepts.

For all Büchi automata \(\mathcal{N}\) there exists a deterministic
parity automaton \(\mathcal{D}\) of exponential size with respect to the
size of \(\mathcal{N}\) such that
\(\mathcal{L}({\mathcal{N}}) = \mathcal{L}({\mathcal{D}})\).

In this work we will consider Büchi automata \(\mathcal{N}\) whose
language represent sets of “bad” words. Thus, rather than being
interested in the set \(\mathcal{L}({\mathcal{N}})\), we will mostly
focus on its complement. It will therefore be convenient to view
\(\mathcal{N}\) as a *universal co-Büchi automaton*. That is to say, an
automaton that accepts a word \(\alpha\) if all of its runs on
\(\alpha\) are **not** accepting with respect to the Büchi condition.
(In other words, all runs on \(\alpha\) visit accepting states only
finitely often.)

## Synthesis games and strategies

A *(Gale-Stewart) game* on input and output alphabets \(I\) and \(O\),
respectively, is a two-player perfect-information game played by Eve and
Adam in rounds: Adam chooses an element \(i_k \in I\) and Eve responds
with an element \(o_k \in O\). A *play* in such a game is an infinite
word
\(\langle i_1, o_1\rangle \langle i_2, o_2\rangle \dots \in (I\times O)^\omega\).

A game is paired with a *payoff set* \(P \subseteq (I \times
O)^\omega\) used to determine who wins a play \(\pi\). If \(\pi \in P\)
then *\(\pi\) is winning for Eve*, otherwise it is *winning for Adam*.

A *strategy* for Adam in a game is a function \(\tau : (I \times O)^*
  \to I\) which maps every (possibly empty) play prefix to a choice of
input letter. Similarly, a *strategy for Eve* is a function
\(\sigma : (I \times O)^* I \to O\) which maps every play prefix and
input letter to a choice of output letter.

A play \(\pi = \langle i_1, o_1\rangle \langle i_2, o_2\rangle \dots\)
is *consistent with* a strategy \(\tau\) for Adam if
\(i_k = \tau( \langle
i_1, o_1\rangle \dots \langle i_{k-1}, o_{k-1}\rangle)\) for all \(k \in
\mathbb{N}\); it is consistent with a strategy \(\sigma\) for Eve if
\(o_k =
\sigma( \langle i_1, o_1\rangle \dots \langle i_{k-1}, o_{k-1}\rangle,
i_{k})\). A pair of strategies \(\sigma\) and \(\tau\) for Eve and Adam,
respectively, induces a unique play \(\pi_{\sigma\tau}\) consistent with
both \(\sigma\) and \(\tau\).

#### Winning strategies.

In a game with payoff set \(P\), the strategy \(\sigma\) for Eve is a
*winning strategy* if for all strategies \(\tau\) for Adam it holds that
\(\pi_{\sigma\tau} \in P\); the strategy \(\tau\) for Eve is winning if
for all strategies \(\sigma\) for Eve we have
\(\pi_{\sigma\tau} \not\in P\).

For all games with Borel-definable payoff set \(P\), it holds that there
exists a winning strategy for Adam in the game if and only if there
exists no winning strategy for Eve in the game.

## Realizability and synthesis

The realizability and synthesis problems are defined for games whose
payoff sets are given as the language of an automaton. We sometimes
refer to these as *games played on automata*.

Consider finite input and output alphabets \(I\) and \(O\),
respectively, and an automaton \(\mathcal{N}\) with alphabet
\(I \times O\). The *realizability problem* asks whether there exists a
winning strategy for Eve in the game with payoff set
\(\mathcal{L}({\mathcal{N}})\). The *synthesis problem* further asks to
compute and output such a strategy if one exists.

#### Finite-memory strategies.

A *finite-memory* strategy \(\sigma\) for Eve in a game played on the
automaton \(\mathcal{N}=
(Q,q_0,I \times O,\Delta)\) with finite input and output alphabets \(I\)
and \(O\) is a strategy that can be encoded as a *(deterministic) Mealy
machine* \(\mathcal{M}= (S,s_0,I, \lambda_u,\lambda_o)\) where \(S\) is
a finite set of (memory) states, \(s_0\) is the initial state,
\(\lambda_u : S \times (Q \times I) \to S\) is the update function and
\(\lambda_o
: S \times (Q \times I) \to O\) is the output function. The machine
encodes \(\sigma\) in the following way. For all play prefixes
\(\langle i_1,  o_1\rangle
\dots \langle i_{k-1}, o_{k-1} \rangle\) and input letters \(i_k \in I\)
we have that
\(\sigma(\langle i_1, o_1\rangle \dots \langle i_{k-1}, o_{k-1} \rangle,
i_k) = \lambda_o(s_k,i_k)\) where \(s_{\ell + 1} =
\lambda_u(s_\ell,i_\ell)\). We then say the strategy \(\sigma\) has
*memory* \(|S|\). In particular, when \(|S| = 1\), we say the strategy
is *memoryless* or *positional*.

For all games played on deterministic parity automata, it holds that
there exists a winning strategy for Eve in the game if and only if there
exists a memoryless winning strategy for Eve.

# Universal co-Büchi games

In this section we recall the Acacia  solution for the realizability and
synthesis problems for games played on universal co-Büchi automata.
Recall that these are games whose payoff set is given by a universal
co-Büchi (UcB) automaton. From a given UcB automaton we will consider a
stronger version of its acceptance condition which asks that visits to
accepting states are bounded.

A universal \(K\)-co-Büchi (UKcB) automaton is an automaton
\(\mathcal{N}=
  (Q,q_0,A,\Delta,B)\) that accepts a word \(\alpha\) if and only if all
of its runs \(\rho = q_0 \alpha(1) q_1 \alpha(2) \dots\) on \(\alpha\)
visit at most \(K\) accepting states,
i.e. \(|\{i \in \mathbb{N} \mathrel{:}
  q_i \in B\}| \le K\).

Our intention is to give an incremental algorithm to solve games played
on UcB automata by solving the game played on the same automaton with
UKcB semantics for increasing values of \(K\). As a first step, we will
construct a finite “safety automaton” which keeps track of the maximal
accepting-state visit count per reachable state instead of doing so for
each run.

For a given UcB automaton \(\mathcal{N}= (Q,q_0,A,\Delta,B)\) and
\(K \in
  \mathbb{N}\), let \(\mathcal{S}^K_\mathcal{N}\) be the automaton
\((\mathcal{F},f_0,A,T,U)\) with components defined as follows.

  - \(\mathcal{F}:= \{f : Q \to \{0,\dots,K\} \cup
          \{\bot,\top\}\}\),

  - \(f_0 \in \mathcal{F}\) is such that \(f_0(q_0) = 0\) and \(f_0(q) =
          \bot\) for all \(q \in Q\) such that \(q \neq q_0\),

  - \(U := \{ f \in \mathcal{F}\mathrel{:}f(q) = \top \text{ for
          some } q \in Q \}\),

  - \(T\) consists of all triples \((f,a,g)\) such that for all
    \(q \in Q\) we have \[g(q) = \max_{(p,a,q) \in \Delta}
            f(p) + \mathbf{1}_{U}(q)\] where \(\bot < n < \top\),
    \(\bot + n = \bot\), and \(\top + n =
          \top\) for all \(n \in \mathbb{N}\).

The automaton \(\mathcal{S}_\mathcal{N}\) accepts a word \(\alpha\) if
and only if it has no run \(q_0 \alpha(1) q_1 \alpha(2) \dots\) on
\(\alpha\) which visits a state from \(U\),
i.e. \(\forall i \in \mathbb{N} : q_i \not\in U\) holds for all runs.

The following property of universal safety automata is immediate from
the definition.

<span id="lem:safe-approx" label="lem:safe-approx">\[lem:safe-approx\]</span>
For all \(\alpha \in A^\omega\) and all \(K,\ell \in \mathbb{N}\) with
\(\ell
  \leq K\), if there exists a run \(\rho\) of \(\mathcal{N}\) on
\(\alpha\) which visits more than \(K\) Büchi states then
\(\alpha \not\in \mathcal{L}({\mathcal{S}^\ell_\mathcal{N}})\).

The above lemma allows us to argue that Eve winning a universal safety
game implies she wins the game played on the UcB game.

Consider input and output alphabets \(I\) and \(O\), a UcB automaton
\(\mathcal{N}\) with alphabet \(I \times O\), a bound
\(K \in \mathbb{N}\), and the corresponding universal safety automaton
\(\mathcal{S}^K_\mathcal{N}\). If there exists a winning strategy for
Eve in the game played on \(\mathcal{S}^K_\mathcal{N}\) then there
exists a winning strategy for Eve in the game played on \(\mathcal{N}\).

We will argue the contraposition of the claimed implication holds. Note
that \(\mathcal{L}({\mathcal{N}})\) is clearly Borel definable. It
follows that the game played on \(\mathcal{N}\) is determined and that,
by assumption, there exists a winning strategy \(\tau\) for Adam. Hence,
any play \(\pi\) consistent with \(\tau\) will be such that
\(\pi \not\in \mathcal{L}({\mathcal{N}})\) and thus \(\pi \not\in
    \mathcal{L}({\mathcal{S}^K_\mathcal{N}})\) by
Lemma [\[lem:safe-approx\]](#lem:safe-approx). So \(\tau\) is a winning
strategy for Adam in the game played on \(\mathcal{S}^K_\mathcal{N}\).

Conversely, there exists a large enough bound \(K\) such that if Eve
wins the game played on \(\mathcal{N}\) then she also wins the one
played on \(\mathcal{S}^K_\mathcal{N}\).

Consider input and output alphabets \(I\) and \(O\) and a UcB automaton
\(\mathcal{N}\) with alphabet \(I \times O\). There exists a bound
\(K \in
    \mathbb{N}\), exponential with respect to the size of
\(\mathcal{N}\), such that if there exists a winning strategy for Eve in
the game played on \(\mathcal{N}\) then there exists a winning strategy
for Eve in the game played on \(\mathcal{S}^K_\mathcal{N}\).

Let us consider a deterministic parity automaton \(\mathcal{D}\) such
that \(\mathcal{L}({\mathcal{D}}) = \mathcal{L}({\mathcal{N}})\) and let
\(\sigma\) be the winning strategy for Eve in the game played on
\(\mathcal{N}\) (and in the game played on \(\mathcal{D}\)). By
memoryless determinacy of parity games, there is a memoryless strategy
\(\mu\) for Eve in the game played on \(\mathcal{D}\). Since
\(\mathcal{D}\) has exponential size with respect to the size of
\(\mathcal{N}\), there exists an exponential-memory strategy \(\mu'\)
for Eve in the game played on \(\mathcal{N}\). It follows that if any
run of \(\mathcal{N}\) on a play \(\pi\) consistent with \(\mu'\) has
visited sufficiently many accepting states, the Mealy machine realizing
\(\mu'\) must have revisited some configuration. Hence, by “pumping”
this behaviour, Adam must be able to win against \(\mu'\), contradicting
the fact that \(\mu'\) is a winning strategy for Eve. We thus have an
exponential upper bound on visits to accepting states for all runs on
plays consistent with \(\mu'\). By construction of
\(\mathcal{S}^K_\mathcal{N}\) we have that \(\mu'\) is a winning
strategy for Eve in the game played on \(\mathcal{S}^K_\mathcal{N}\).

## Antichain-based algorithm

<span> </span>

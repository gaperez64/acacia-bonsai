import SymmetryProofs.Detector

namespace Symmetry

/-- C++: actioners/standard.hh:153–181. Shifted counters: 0 is -1 (absent),
    j+1 is j; K+1 is overflow and safe counting coordinates are at most K. -/
abbrev State (n K : Nat) := Fin n → Fin (K + 2)

/-- C++: actioners/standard.hh:161–166. Maximum over predecessor contributions. -/
def maximum : List Nat → Nat
  | [] => 0
  | x :: xs => max x (maximum xs)

theorem maximum_le (xs : List Nat) (b : Nat) :
    maximum xs ≤ b ↔ ∀ x ∈ xs, x ≤ b := by
  induction xs with
  | nil => simp [maximum]
  | cons x xs ih => simp [maximum, Nat.max_le, ih]

/-- C++: actioners/standard.hh:162–166. State-indexed universal branching. -/
def maxStates (g : Fin n → Nat) : Nat := maximum ((List.finRange n).map g)

theorem maxStates_le (g : Fin n → Nat) (b : Nat) :
    maxStates g ≤ b ↔ ∀ q, g q ≤ b := by
  simp [maxStates, maximum_le]

theorem le_maxStates (g : Fin n → Nat) (q : Fin n) : g q ≤ maxStates g :=
  (maxStates_le g _).mp (Nat.le_refl _) q

theorem maxStates_perm (p : Perm (Fin n)) (f g : Fin n → Nat)
    (h : ∀ q, f (p q) = g q) : maxStates f = maxStates g := by
  apply Nat.le_antisymm
  · apply (maxStates_le _ _).mpr
    intro q
    have := h (p.invFun q)
    rw [p.right_inv] at this
    rw [this]
    exact le_maxStates _ _
  · apply (maxStates_le _ _).mpr
    intro q
    rw [← h q]
    exact le_maxStates _ _

/-- C++: actioners/standard.hh:153–172; posets/vectors/X_and_boolvec.hh:21–30.
    Scalar counter update before storage; Boolean coordinates have a tighter safe
    cap. Overflow remains explicit for the preimage proof (see Trusted gap). -/
def contribution (A : Automaton n a) (K : Nat) (f : State n K)
    (v : Fin a → Bool) (q r : Fin n) : Nat :=
  if A.enabled q r v && decide ((f q).val > 0) then
    min (K + 1) ((f q).val + if A.rejecting r then 1 else 0)
  else 0

theorem contribution_bound (A : Automaton n a) (K : Nat) (f : State n K)
    (v : Fin a → Bool) (q r : Fin n) : contribution A K f v q r < K + 2 := by
  unfold contribution
  split
  · exact Nat.lt_succ_of_le (Nat.min_le_left ..)
  · omega

/-- C++: actioners/standard.hh:149–195 (forward branch). No predecessor means -1. -/
def succ (A : Automaton n a) (K : Nat) (f : State n K) (v : Fin a → Bool) : State n K :=
  fun r => ⟨maxStates (fun q => contribution A K f v q r), by
    have := (maxStates_le _ (K + 1)).mpr (fun q => Nat.le_of_lt_succ
      (contribution_bound A K f v q r))
    omega⟩

/-- C++: k_bounded_safety_aut.hh:106–110; equivariant:909–912. -/
def safe (A : Automaton n a) (K : Nat) (f : State n K) : Bool :=
  decide (∀ q, (f q).val ≤ if A.counting q then K else 1)

/-- C++: k_bounded_safety_aut.hh:101–104. One live initial state, counter zero. -/
def initial (A : Automaton n a) (K : Nat) : State n K :=
  fun q => if q = A.init then ⟨1, by omega⟩ else ⟨0, by omega⟩

/-- C++: k_bounded_safety_aut.hh:244–247. Concrete downset membership semantics. -/
abbrev Region (n K : Nat) := State n K → Bool

/-- C++: equivariant_k_bounded_safety_aut.hh:388–432. Permute raw downset coordinates. -/
def image (p : Perm (Fin n)) (X : Region n K) : Region n K :=
  fun f => X (act p.symm f)

/-- C++: k_bounded_safety_aut.hh:334–349; solver_invoker.cc:1159–1165.
    `side` is the protagonist's side of the original I/O partition. -/
def merge (A : Automaton n a) (side : Bool) (i o : Fin a → Bool) : Fin a → Bool :=
  fun p => if A.isInput p = side then o p else i p

/-- C++: equivariant_k_bounded_safety_aut.hh:434–466. Union over every response. -/
def computeT (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (i : Fin a → Bool) : Region n K :=
  fun f => (letters a).any fun o => X (succ A K f (merge A side i o))

theorem computeT_iff (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (i : Fin a → Bool) (f : State n K) :
    computeT A K side X i f = true ↔ ∃ o, X (succ A K f (merge A side i o)) = true := by
  simp only [computeT, List.any_eq_true]
  exact ⟨fun ⟨o, _, h⟩ => ⟨o, h⟩, fun ⟨o, h⟩ => ⟨o, mem_letters o, h⟩⟩

/-- C++: equivariant_k_bounded_safety_aut.hh:57–68, 151–172. An opponent
    letter assigns only the opponent's APs; fill unused protagonist bits with false. -/
def opponentProjection (A : Automaton n a) (side : Bool) (i : Fin a → Bool) : Fin a → Bool :=
  fun p => if A.isInput p = side then false else i p

/-- Enumerating full valuations duplicates unused bits but gives the same T
    sets as the input cubes enumerated by C++, preserving the game;
    symmetry.hh:14–17, #331. -/
theorem computeT_projected (K : Nat) (side : Bool) (X : Region n K) (i : Fin a → Bool) :
    computeT A K side X (opponentProjection A side i) = computeT A K side X i := by
  have hm : ∀ o, merge A side (opponentProjection A side i) o = merge A side i o := by
    intro o
    funext p
    by_cases h : A.isInput p = side <;> simp [merge, opponentProjection, h]
  unfold computeT
  simp only [hm]

/-- C++: k_bounded_safety_aut.hh:244–301. Shrinking CPre, including X ∩ Pre(X). -/
def cpre (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K) : Region n K :=
  fun f => X f && (letters a).all fun i => computeT A K side X i f

theorem cpre_iff (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (f : State n K) : cpre A K side X f = true ↔
    X f = true ∧ ∀ i, ∃ o, X (succ A K f (merge A side i o)) = true := by
  simp only [cpre, Bool.and_eq_true, List.all_eq_true, computeT_iff]
  exact and_congr_right (fun _ => ⟨fun h i => h i (mem_letters i), fun h i _ => h i⟩)

/-- C++: k_bounded_safety_aut.hh:129–165. Complete descending rounds at fixed K. -/
def iterate (F : (α → Bool) → α → Bool) (X : α → Bool) : Nat → α → Bool
  | 0 => X
  | k + 1 => F (iterate F X k)

/-- C++: k_bounded_safety_aut.hh:129–165. Simultaneous CPre stages. -/
def stage (A : Automaton n a) (K : Nat) (side : Bool) : Nat → Region n K :=
  iterate (cpre A K side) (safe A K)

/-- C++: k_bounded_safety_aut.hh:101–110, 129–165. Enumerate the finite game space. -/
def states (n K : Nat) : List (State n K) := vectors (List.finRange (K + 2)) n

theorem mem_states (f : State n K) : f ∈ states n K :=
  mem_vectors _ List.mem_finRange f

/-- C++: k_bounded_safety_aut.hh:148–165. A finite bound on descending rounds. -/
def final (A : Automaton n a) (K : Nat) (side : Bool) : Region n K :=
  stage A K side (states n K).length

/-- C++: k_bounded_safety_aut.hh:148–165. The verdict tests membership of init. -/
def winner (A : Automaton n a) (K : Nat) (side : Bool) : Bool :=
  final A K side (initial A K)

end Symmetry

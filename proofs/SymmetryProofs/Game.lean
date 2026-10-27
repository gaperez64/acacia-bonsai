import SymmetryProofs.Detector

namespace Symmetry
abbrev State (n K : Nat) := Fin n → Fin (K + 2) -- Shifted ranks: 0 is absent; K+1 is overflow; standard.hh:153–181.
def maximum : List Nat → Nat
  | [] => 0
  | x :: xs => max x (maximum xs)
theorem maximum_le (xs : List Nat) (b : Nat) : maximum xs ≤ b ↔ ∀ x ∈ xs, x ≤ b := by
  induction xs with
  | nil => simp [maximum]
  | cons x xs ih => simp [maximum, Nat.max_le, ih]
def maxStates (g : Fin n → Nat) : Nat := maximum ((List.finRange n).map g)
theorem maxStates_le (g : Fin n → Nat) (b : Nat) :
    maxStates g ≤ b ↔ ∀ q, g q ≤ b := by simp [maxStates, maximum_le]
theorem le_maxStates (g : Fin n → Nat) (q : Fin n) : g q ≤ maxStates g :=
  (maxStates_le g _).mp (Nat.le_refl _) q
theorem maxStates_perm (p : Perm (Fin n)) (f g : Fin n → Nat)
    (h : ∀ q, f (p q) = g q) : maxStates f = maxStates g := by
  apply Nat.le_antisymm
  · apply (maxStates_le _ _).mpr
    intro q; have := h (p.invFun q)
    rw [p.right_inv] at this; rw [this]
    exact le_maxStates _ _
  · apply (maxStates_le _ _).mpr
    intro q; rw [← h q]
    exact le_maxStates _ _
def contribution (A : Automaton n a) (K : Nat) (f : State n K) -- Scalar increment before Boolean storage; standard.hh:161–172.
    (v : Fin a → Bool) (q r : Fin n) : Nat :=
  if A.enabled q r v && decide ((f q).val > 0) then
    min (K + 1) ((f q).val + if A.rejecting r then 1 else 0)
  else 0
theorem contribution_bound (A : Automaton n a) (K : Nat) (f : State n K)
    (v : Fin a → Bool) (q r : Fin n) : contribution A K f v q r < K + 2 := by
  unfold contribution; split
  · exact Nat.lt_succ_of_le (Nat.min_le_left ..)
  · omega
def succ (A : Automaton n a) (K : Nat) (f : State n K) (v : Fin a → Bool) : State n K :=
  fun r => ⟨maxStates (fun q => contribution A K f v q r), by -- Maximum over all enabled predecessor contributions; standard.hh:149–195.
    have := (maxStates_le _ (K + 1)).mpr (fun q => Nat.le_of_lt_succ
      (contribution_bound A K f v q r))
    omega⟩
-- Counting cap K; Boolean cap 1 in shifted ranks; k_bounded_safety_aut.hh:106–110.
def safe (A : Automaton n a) (K : Nat) (f : State n K) : Bool :=
  decide (∀ q, (f q).val ≤ if A.counting q then K else 1)
def initial (A : Automaton n a) (K : Nat) : State n K := -- Only init is live, with counter zero; k_bounded_safety_aut.hh:101–104.
  fun q => if q = A.init then ⟨1, by omega⟩ else ⟨0, by omega⟩
abbrev Region (n K : Nat) := State n K → Bool
def image (p : Perm (Fin n)) (X : Region n K) : Region n K :=
  fun f => X (act p.symm f) -- Transport a raw region by its inverse coordinates; equivariant_k_bounded_safety_aut.hh:388–432.
def merge (A : Automaton n a) (side : Bool) (i o : Fin a → Bool) : Fin a → Bool :=
  fun p => if A.isInput p = side then o p else i p -- side chooses the protagonist AP partition; k_bounded_safety_aut.hh:334–349.
-- Union over all protagonist responses; equivariant_k_bounded_safety_aut.hh:434–466.
def computeT (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (i : Fin a → Bool) : Region n K :=
  fun f => (letters a).any fun o => X (succ A K f (merge A side i o))
theorem computeT_iff (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (i : Fin a → Bool) (f : State n K) :
    computeT A K side X i f = true ↔ ∃ o, X (succ A K f (merge A side i o)) = true := by
  simp only [computeT, List.any_eq_true]; exact ⟨fun ⟨o, _, h⟩ => ⟨o, h⟩, fun ⟨o, h⟩ => ⟨o, mem_letters o, h⟩⟩
def opponentProjection (A : Automaton n a) (side : Bool) (i : Fin a → Bool) : Fin a → Bool :=
  fun p => if A.isInput p = side then false else i p -- Clear unused protagonist bits; equivariant_k_bounded_safety_aut.hh:151–172.
-- Unused protagonist bits do not change Pre; equivariant_k_bounded_safety_aut.hh:434–466.
theorem computeT_projected (K : Nat) (side : Bool) (X : Region n K) (i : Fin a → Bool) :
    computeT A K side X (opponentProjection A side i) = computeT A K side X i := by
  have hm : ∀ o, merge A side (opponentProjection A side i) o = merge A side i o := by
    intro o; funext p
    by_cases h : A.isInput p = side <;> simp [merge, opponentProjection, h]
  unfold computeT; simp only [hm]
-- Shrink X by the universal input predecessor; k_bounded_safety_aut.hh:244–301.
def cpre (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K) : Region n K :=
  fun f => X f && (letters a).all fun i => computeT A K side X i f
theorem cpre_iff (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (f : State n K) : cpre A K side X f = true ↔
    X f = true ∧ ∀ i, ∃ o, X (succ A K f (merge A side i o)) = true := by
  simp only [cpre, Bool.and_eq_true, List.all_eq_true, computeT_iff]
  exact and_congr_right (fun _ => ⟨fun h i => h i (mem_letters i), fun h i _ => h i⟩)
def iterate (F : (α → Bool) → α → Bool) (X : α → Bool) : Nat → α → Bool
  | 0 => X
  | k + 1 => F (iterate F X k)
def stage (A : Automaton n a) (K : Nat) (side : Bool) : Nat → Region n K :=
  iterate (cpre A K side) (safe A K) -- Simultaneous descending CPre rounds; k_bounded_safety_aut.hh:129–165.
def states (n K : Nat) : List (State n K) := vectors (List.finRange (K + 2)) n
theorem mem_states (f : State n K) : f ∈ states n K :=
  mem_vectors _ List.mem_finRange f
def final (A : Automaton n a) (K : Nat) (side : Bool) : Region n K :=
  stage A K side (states n K).length -- Finite termination bound for descending rounds; k_bounded_safety_aut.hh:148–165.
def winner (A : Automaton n a) (K : Nat) (side : Bool) : Bool :=
  final A K side (initial A K) -- Verdict is initial membership in the final region; k_bounded_safety_aut.hh:148–165.

end Symmetry

import SymmetryProofs.Frames

namespace Symmetry
variable {α : Type} {A : Automaton n a}

/-- C++: k_bounded_safety_aut.hh:157–162; equivariant:746–750,952–956.
    At fixed K, stop losing as soon as init leaves the descending region. -/
def runUntilInitialOut (init : α) (F : (α → Bool) → α → Bool) (X : α → Bool) : Nat → Bool
  | 0 => X init
  | k + 1 => if X init then runUntilInitialOut init F (F X) k else false

theorem iterate_shrinks (F : (α → Bool) → α → Bool) (shrink : ∀ X, Subset (F X) X)
    (X : α → Bool) (k : Nat) : Subset (iterate F X k) X := by
  induction k with
  | zero => exact fun _ h => h
  | succ k ih => exact fun f h => ih f (shrink _ f h)

/-- Stopping when init disappears gives exactly the verdict of the completed
    descending iteration. This covers the C++ early exit, without assuming it
    preserves the result; symmetry.hh:14–17 and issue #331. -/
theorem early_exit_exact (init : α) (F : (α → Bool) → α → Bool)
    (shrink : ∀ X, Subset (F X) X) (X : α → Bool) (k : Nat) :
    runUntilInitialOut init F X k = iterate F X k init := by
  induction k generalizing X with
  | zero => rfl
  | succ k ih =>
    change (if X init then runUntilInitialOut init F (F X) k else false) =
      iterate F X (k + 1) init
    cases hx : X init with
    | true => simp only [ite_true]; rw [ih, iterate_shift]
    | false =>
      change false = iterate F X (k + 1) init
      apply Eq.symm
      apply Bool.eq_false_iff.mpr
      intro hf
      have := iterate_shrinks F shrink X (k + 1) init hf
      rw [hx] at this
      contradiction

/-- Both actual fixed-K reuse loops, including the early-initial-out branch,
    have the plain verdict in either frame. The plan's obligations are explicit;
    symmetry.hh:14–17, equivariant:721–774,923–956, issue #331. -/
theorem executed_verdicts_equal (K : Nat) (side : Bool) (P : SweepPlan A side) :
    runUntilInitialOut (initial A K) (sweep A K side P) (safe A K) (states n K).length =
      winner A K side ∧
    runUntilInitialOut (initial A K) (representativeRound K side P) (safe A K) (states n K).length =
      winner A K side := by
  rw [early_exit_exact _ _ (sweep_shrinks K side P),
    early_exit_exact _ _ (representativeRound_shrinks K side P)]
  exact verdicts_equal K side P

end Symmetry

import SymmetryProofs.Closure

namespace Symmetry
variable {A : Automaton n a}

/-- C++: solver_invoker.cc:1159–1165. The unreal formula worker exchanges its
    engine input and output supports, after the formula-side transformation. -/
def swapFrame (A : Automaton n a) : Automaton n a :=
  { A with isInput := fun p => !A.isInput p }

/-- C++: solver_invoker.cc:1165; symmetry.hh:9–17. The same checked symmetry
    remains valid after complementing the engine's partition. -/
def Verified.swapFrame (s : Verified A) : Verified (Symmetry.swapFrame A) where
  states := s.states
  aps := s.aps
  init_fixed := s.init_fixed
  acceptance := s.acceptance
  partition p := by simp [Symmetry.swapFrame, s.partition]
  caps := s.caps
  forward := s.forward

/-- The abstract side=true game equals the engine side=false game after
    I/O exchange. This connects the theorems for both frames to the unreal formula
    worker, solver_invoker.cc:1159–1165, symmetry.hh:9–17 and #331. -/
theorem swapped_engine_cpre (K : Nat) (X : Region n K) :
    cpre (swapFrame A) K false X = cpre A K true X := by
  have hm : ∀ i o, merge (swapFrame A) false i o = merge A true i o := by
    intro i o
    funext p
    cases h : A.isInput p <;> simp [merge, swapFrame, h]
  unfold cpre computeT
  simp only [hm]
  rfl

/-- C++: k_bounded_safety_aut.hh:220–238; equivariant:963–969.
    Widen maximal counting bounds uniformly and reset Boolean maxima to zero.
    Shifted encoding adds delta even to -1 counting bounds, just like the source. -/
def bump (A : Automaton n a) (K delta : Nat) (m : State n K) : State n (K + delta) :=
  fun q => if A.counting q then ⟨(m q).val + delta, by have := (m q).isLt; omega⟩
    else ⟨1, by omega⟩

/-- Raising K preserves symmetry because the verified maps preserve the
    bool/counting split, proving the source's bound-raise claim at
    equivariant:972–976, symmetry.hh:14–17, issue #331. -/
theorem bump_commutes (s : Verified A) (K delta : Nat) (m : State n K) :
    bump A K delta (act s.states m) = act s.states (bump A K delta m) := by
  funext q
  rw [← s.states.right_inv q]
  simp only [bump, act_apply, s.caps]

/-- Both engine frames satisfy the same CPre equivariance law;
    symmetry.hh:9–17 and issue #331. -/
theorem both_engine_frames (s : Verified A) (K : Nat) (X : Region n K) :
    cpre A K false (image s.states X) = image s.states (cpre A K false X) ∧
    cpre (swapFrame A) K false (image s.states X) =
      image s.states (cpre (swapFrame A) K false X) :=
  ⟨normal_frame s K X, normal_frame s.swapFrame K X⟩

end Symmetry

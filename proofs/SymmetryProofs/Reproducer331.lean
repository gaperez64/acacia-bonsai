import SymmetryProofs.Reuse

namespace Symmetry.Reproducer331

/-- Issue #331; PR #330, tests/equivariant_pre_test.cc:510–553.
    Seven nonrejecting states and the mixed r_0,r_1,r_2 indexed family. -/
def aut : Automaton 7 3 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => true
  isInput := fun p => p != 1
  edges := [⟨0, .atom 0, 1⟩, ⟨0, .atom 1, 2⟩, ⟨0, .atom 2, 3⟩,
    ⟨1, .atom 0, 4⟩, ⟨2, .atom 1, 5⟩, ⟨3, .atom 2, 6⟩,
    ⟨4, .top, 4⟩, ⟨5, .top, 5⟩, ⟨6, .top, 6⟩]

/-- C++: symmetry.hh:260–264. Swap clients in each of the two state blocks. -/
def candidate (i j : Fin 3) : Candidate 7 3 where
  phi := fun q => swap (⟨i.val + 1, by omega⟩ : Fin 7) ⟨j.val + 1, by omega⟩
    (swap (⟨i.val + 4, by omega⟩ : Fin 7) ⟨j.val + 4, by omega⟩ q)
  phiInv := fun q => swap (⟨i.val + 1, by omega⟩ : Fin 7) ⟨j.val + 1, by omega⟩
    (swap (⟨i.val + 4, by omega⟩ : Fin 7) ⟨j.val + 4, by omega⟩ q)
  pi := swap i j
  piInv := swap i j
  families := [(true, [0, 1, 2])]

/-- All structurally valid client transpositions pass the old detector,
    including those crossing I/O. This distinguishes the unguarded detector
    from the PR #330 check on the #331 reproducer; symmetry.hh:9–17. -/
theorem old_accepts_all : ∀ i j : Fin 3, unguardedCheck aut (candidate i j) = true := by
  decide

/-- The PR #330 family filter rejects this mixed family regardless of which
    member was registered last. This is the guarded-detector obligation of #331,
    symmetry.hh:9–17. -/
theorem family_rejected : ∀ tag : Bool,
    familyGuard aut { candidate 0 1 with families := [(tag, [0, 1, 2])] } = false := by
  decide

/-- Every crossing transposition is rejected, including both orders of its
    endpoints. #331, PR #330:326–335, symmetry.hh:9–17. -/
theorem guarded_rejects_crossing : ∀ i j : Fin 3, aut.isInput i ≠ aut.isInput j →
    check aut (candidate i j) = false := by
  decide

/-- The C++ family filter also skips same-side swaps in a mixed family.
    The port retains this whole-family check. -/
theorem whole_mixed_family_rejected : ∀ i j : Fin 3, check aut (candidate i j) = false := by
  decide

/-- Issue #331: the old (unguarded) state/AP maps are bijections. -/
def stateSwap : Perm (Fin 7) where
  toFun := (candidate 0 1).phi
  invFun := (candidate 0 1).phiInv
  left_inv := by decide
  right_inv := by decide

/-- Issue #331: corresponding AP transposition r_0 <-> r_1. -/
def apSwap : Perm (Fin 3) := transposition 0 1

/-- C++: k_bounded_safety_aut.hh:244–247. A downward closed region that
    excludes a live r_0 branch. The swapped region excludes the r_1 branch. -/
def avoid0 : Region 7 1 := fun f => (f 1).val == 0

/-- An environment can force r_0; a protagonist can choose r_1 false. Swapping
    these roles breaks CPre although structural automorphism holds, violating
    the #331 conclusion when PR #330's hypothesis is removed;
    symmetry.hh:9–17. -/
theorem old_cpre_fails :
    cpre aut 1 false (image stateSwap avoid0) (initial aut 1) = true ∧
    image stateSwap (cpre aut 1 false avoid0) (initial aut 1) = false := by
  decide

/-- The opposite frame also fails without the I/O guard; symmetry.hh:9–17, #331. -/
theorem old_swapped_cpre_fails :
    cpre aut 1 true (image stateSwap avoid0) (initial aut 1) = false ∧
    image stateSwap (cpre aut 1 true avoid0) (initial aut 1) = true := by
  decide

end Symmetry.Reproducer331

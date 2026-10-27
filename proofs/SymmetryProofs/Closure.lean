import SymmetryProofs.Backward

namespace Symmetry
variable {A : Automaton n a}
-- Each generator sees preceding intersections; equivariant_k_bounded_safety_aut.hh:565–581.
def closePass (G : List (Verified A)) (X : Region n K) : Region n K :=
  G.foldl (fun Y s => fun f => Y f && image s.states Y f) X
-- Iterate generator intersections to stability; equivariant_k_bounded_safety_aut.hh:565–581.
def close (G : List (Verified A)) (X : Region n K) : Region n K :=
  iterate (closePass G) X (states n K).length
theorem closePass_shrinks (G : List (Verified A)) (X : Region n K) :
    Subset (closePass G X) X := by apply fold_shrinks; exact fun _ _ _ h => (Bool.and_eq_true_iff.mp h).1
theorem close_shrinks (G : List (Verified A)) (X : Region n K) : Subset (close G X) X :=
  iterate_shrinks _ (closePass_shrinks G) X _
theorem closePass_fixed_invariant (G : List (Verified A)) (X : Region n K)
    (hf : closePass G X = X) : Symmetric G X := by
  intro s hs; have h := fold_fixed G (fun s Y => fun f => Y f && image s.states Y f)
    (fun _ _ _ h => (Bool.and_eq_true_iff.mp h).1) X hf s hs
  have contained : ∀ f, X f = true → X (act s.states.symm f) = true := by
    intro f hf; have hh : (X f && image s.states X f) = true := by
      have he := congrFun h f
      rw [he]; exact hf
    exact (Bool.and_eq_true_iff.mp hh).2
  have hc : ∀ f ∈ (states n K).filter X, (actionPerm s.states.symm) f ∈ (states n K).filter X := by
    intro f hf; exact List.mem_filter.mpr ⟨mem_states _, contained f (List.mem_filter.mp hf).2⟩
  intro f; have he := finite_closed_iff ((states n K).filter X) (actionPerm s.states.symm) hc (act s.states f)
  apply Bool.eq_iff_iff.mpr; have hh : X f = true ↔ X (act s.states f) = true := by
    simpa [actionPerm, mem_states] using he
  exact hh.symm
theorem close_invariant (G : List (Verified A)) (X : Region n K) : Symmetric G (close G X) := by
  apply closePass_fixed_invariant -- Terminated generator closure is invariant; equivariant_k_bounded_safety_aut.hh:558–581.
  exact iterate_stable (states n K) mem_states (closePass G) (closePass_shrinks G)
    _ X (List.length_filter_le ..)
-- Closure preserves any symmetric contained region; equivariant_k_bounded_safety_aut.hh:558–581.
theorem close_preserves (G : List (Verified A)) (W X : Region n K)
    (hw : Symmetric G W) (hx : Subset W X) : Subset W (close G X) := by
  have one : ∀ Y, Subset W Y → Subset W (closePass G Y) := by
    intro Y hy; apply fold_preserves G _ (Subset W) _ Y hy
    intro s hs Z hz f hf; refine Bool.and_eq_true_iff.mpr ⟨hz f hf, hz _ ?_⟩
    have hi := hw s hs (act s.states.symm f); simpa [hf] using hi.symm
  have go : ∀ k, Subset W (iterate (closePass G) X k) := by
    intro k; induction k with
    | zero => exact hx
    | succ k ih => exact one _ ih
  exact go _
-- Tighten by one input, then close the evolving region; equivariant_k_bounded_safety_aut.hh:945–947.
def representativeUpdate (K : Nat) (side : Bool) (G : List (Verified A))
    (i : Fin a → Bool) (X : Region n K) : Region n K :=
  close G (fun f => X f && computeT A K side X i f)
def representativeRound (K : Nat) (side : Bool) (P : SweepPlan A side) (X : Region n K) : Region n K :=
  P.orbits.foldl (fun Y O => representativeUpdate K side P.generators O.representative Y) X
theorem representativeUpdate_shrinks (K : Nat) (side : Bool) (G : List (Verified A))
    (i : Fin a → Bool) (X : Region n K) : Subset (representativeUpdate K side G i X) X := by intro f h; exact (Bool.and_eq_true_iff.mp (close_shrinks G _ f h)).1
theorem representativeRound_shrinks (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) : Subset (representativeRound K side P X) X :=
  fold_shrinks P.orbits
    (fun O Y => representativeUpdate K side P.generators O.representative Y)
    (fun O Y => representativeUpdate_shrinks K side P.generators O.representative Y) X
-- Any selected input preserves all winners; equivariant_k_bounded_safety_aut.hh:945–947.
theorem selected_preserves (K : Nat) (side : Bool) (G : List (Verified A))
    (i : Fin a → Bool) (X : Region n K) (contains : Subset (final A K side) X) :
    Subset (final A K side) (representativeUpdate K side G i X) := by
  apply close_preserves G _ _ (fun s _ => final_invariant s K side); intro f hf
  refine Bool.and_eq_true_iff.mpr ⟨contains f hf, ?_⟩
  apply computeT_mono A K side contains
  have h : cpre A K side (final A K side) f = true := by rw [final_fixed]; exact hf
  exact (computeT_iff _ _ _ _ _ _).mpr (((cpre_iff _ _ _ _ _).mp h).2 i)
theorem representativeRound_preserves (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) (hx : Subset (final A K side) X) :
    Subset (final A K side) (representativeRound K side P X) :=
  fold_preserves P.orbits (fun O Y => representativeUpdate K side P.generators O.representative Y)
    (Subset (final A K side)) (fun O _ Y hy => selected_preserves K side P.generators O.representative Y hy) X hx
theorem representativeRound_fixed_cpre (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) (hf : representativeRound K side P X = X) : cpre A K side X = X := by
  have each := fold_fixed P.orbits
    (fun O Y => representativeUpdate K side P.generators O.representative Y)
    (fun O Y => representativeUpdate_shrinks K side P.generators O.representative Y) X hf
  have hx : Symmetric P.generators X := by
    obtain ⟨O, ho, _⟩ := P.covers (fun _ => false)
    rw [← each O ho]; exact close_invariant P.generators _
  apply subset_antisymm (cpre_shrinks A K side X); intro f hxf
  apply (cpre_iff _ _ _ _ _).mpr; refine ⟨hxf, fun i => ?_⟩
  obtain ⟨O, ho, hi⟩ := P.covers i
  obtain ⟨s, hs, he⟩ := List.mem_map.mp hi
  have ho_fixed := each O ho
  have hsymmetric := generated_invariant (P.member_generated O ho s hs) hx
  have hrep : ∀ g, X g = true → computeT A K side X O.representative g = true := by
    intro g hg; have hh : representativeUpdate K side P.generators O.representative X g = true := by
      rw [ho_fixed]; exact hg
    exact (Bool.and_eq_true_iff.mp (close_shrinks P.generators _ g hh)).2
  have hin : X (act s.states.symm f) = true := by
    have := hsymmetric (act s.states.symm f); simpa [hxf] using this.symm
  have ht := hrep _ hin
  have hr := congrFun (reuse_exact s K side X hsymmetric O.representative) f
  change image s.states (computeT A K side X O.representative) f = _ at hr
  have hm : computeT A K side X i f = true := by
    rw [← computeT_projected K side X i, ← he, computeT_projected, ← hr]; exact ht
  exact (computeT_iff _ _ _ _ _ _).mp hm
def representativeFinal (K : Nat) (side : Bool) (P : SweepPlan A side) : Region n K :=
  iterate (representativeRound K side P) (safe A K) (states n K).length
-- Representative/closure iteration returns the plain final region; equivariant_k_bounded_safety_aut.hh:923–955.
theorem representativeFinal_eq (K : Nat) (side : Bool) (P : SweepPlan A side) :
    representativeFinal K side P = final A K side :=
  iterate_eq_final K side _ (representativeRound_shrinks K side P)
    (representativeRound_preserves K side P) (fun _ => representativeRound_fixed_cpre K side P _)
theorem verdicts_equal (K : Nat) (side : Bool) (P : SweepPlan A side) : -- Both fixed-K paths return the plain verdict; symmetry.hh:14–17.
    sweepFinal A K side P (initial A K) = winner A K side ∧
    representativeFinal K side P (initial A K) = winner A K side := by rw [sweepFinal_eq, representativeFinal_eq]; exact ⟨rfl, rfl⟩

variable {A : Automaton n a}
def swapFrame (A : Automaton n a) : Automaton n a := -- Exchange the engine input and output supports; solver_invoker.cc:1159–1165.
  { A with isInput := fun p => !A.isInput p }
def Verified.swapFrame (s : Verified A) : Verified (Symmetry.swapFrame A) where
  states := s.states
  aps := s.aps
  init_fixed := s.init_fixed
  acceptance := s.acceptance
  partition p := by simp [Symmetry.swapFrame, s.partition]
  caps := s.caps
  forward := s.forward
-- The exchanged engine implements side=true; solver_invoker.cc:1159–1165.
theorem swapped_engine_cpre (K : Nat) (X : Region n K) : cpre (swapFrame A) K false X = cpre A K true X := by
  have hm : ∀ i o, merge (swapFrame A) false i o = merge A true i o := by
    intro i o; funext p
    cases h : A.isInput p <;> simp [merge, swapFrame, h]
  unfold cpre computeT; simp only [hm]
  rfl
-- Uniform counting lift; reset Boolean maxima to shifted 1; k_bounded_safety_aut.hh:220–238.
def bump (A : Automaton n a) (K delta : Nat) (m : State n K) : State n (K + delta) :=
  fun q => if A.counting q then ⟨(m q).val + delta, by have := (m q).isLt; omega⟩
    else ⟨1, by omega⟩
theorem bump_commutes (s : Verified A) (K delta : Nat) (m : State n K) :
    bump A K delta (act s.states m) = act s.states (bump A K delta m) := by
  funext q; rw [← s.states.right_inv q] -- Uniform bound raises preserve coordinate symmetry; equivariant_k_bounded_safety_aut.hh:972–976.
  simp only [bump, act_apply, s.caps]
-- The same equivariance law covers both engine partitions; solver_invoker.cc:1159–1165.
theorem both_engine_frames (s : Verified A) (K : Nat) (X : Region n K) :
    cpre A K false (image s.states X) = image s.states (cpre A K false X) ∧
    cpre (swapFrame A) K false (image s.states X) = image s.states (cpre (swapFrame A) K false X) :=
  ⟨normal_frame s K X, normal_frame s.swapFrame K X⟩

variable {α : Type} {A : Automaton n a}
-- Stop as soon as init leaves a descending region; equivariant_k_bounded_safety_aut.hh:746–750,952–956.
def runUntilInitialOut (init : α) (F : (α → Bool) → α → Bool) (X : α → Bool) : Nat → Bool
  | 0 => X init
  | k + 1 => if X init then runUntilInitialOut init F (F X) k else false
theorem early_exit_exact (init : α) (F : (α → Bool) → α → Bool)
    (shrink : ∀ X, Subset (F X) X) (X : α → Bool) (k : Nat) :
    runUntilInitialOut init F X k = iterate F X k init := by
  induction k generalizing X with -- Early initial-out equals the completed descending verdict; k_bounded_safety_aut.hh:157–162.
  | zero => rfl
  | succ k ih =>
    change (if X init then runUntilInitialOut init F (F X) k else false) = iterate F X (k + 1) init
    cases hx : X init with
    | true => simp only [ite_true]; rw [ih, iterate_shift]
    | false =>
      change false = iterate F X (k + 1) init
      apply Eq.symm; apply Bool.eq_false_iff.mpr
      intro hf; have := iterate_shrinks F shrink X (k + 1) init hf
      rw [hx] at this; contradiction
theorem executed_verdicts_equal (K : Nat) (side : Bool) (P : SweepPlan A side) :
    runUntilInitialOut (initial A K) (sweep A K side P) (safe A K) (states n K).length = winner A K side ∧
    runUntilInitialOut (initial A K) (representativeRound K side P) (safe A K) (states n K).length =
      winner A K side := by
  rw [early_exit_exact _ _ (sweep_shrinks K side P), -- Both actual early-exit loops preserve the verdict; symmetry.hh:14–17.
    early_exit_exact _ _ (representativeRound_shrinks K side P)]
  exact verdicts_equal K side P

end Symmetry

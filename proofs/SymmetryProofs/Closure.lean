import SymmetryProofs.Backward

namespace Symmetry
variable {A : Automaton n a}

/-- C++: equivariant_k_bounded_safety_aut.hh:565–581. Each generator sees the
    region updated by preceding generators in this pass. -/
def closePass (G : List (Verified A)) (X : Region n K) : Region n K :=
  G.foldl (fun Y s => fun f => Y f && image s.states Y f) X

/-- C++: equivariant_k_bounded_safety_aut.hh:565–581. Descend until no generator
    tightens the region; the finite bound is proved sufficient below. -/
def close (G : List (Verified A)) (X : Region n K) : Region n K :=
  iterate (closePass G) X (states n K).length

theorem closePass_shrinks (G : List (Verified A)) (X : Region n K) :
    Subset (closePass G X) X := by
  apply fold_shrinks
  exact fun _ _ _ h => (Bool.and_eq_true_iff.mp h).1

theorem close_shrinks (G : List (Verified A)) (X : Region n K) : Subset (close G X) X := by
  have h : ∀ k, Subset (iterate (closePass G) X k) X := by
    intro k
    induction k with
    | zero => exact fun _ h => h
    | succ k ih => exact fun f h => ih f (closePass_shrinks G _ f h)
  exact h _

theorem closePass_fixed_invariant (G : List (Verified A)) (X : Region n K)
    (hf : closePass G X = X) : Symmetric G X := by
  intro s hs
  have h := fold_fixed G (fun s Y => fun f => Y f && image s.states Y f)
    (fun _ _ _ h => (Bool.and_eq_true_iff.mp h).1) X hf s hs
  have contained : ∀ f, X f = true → X (act s.states.symm f) = true := by
    intro f hf
    have hh : (X f && image s.states X f) = true := by
      have he := congrFun h f
      rw [he]; exact hf
    exact (Bool.and_eq_true_iff.mp hh).2
  have hc : ∀ f ∈ (states n K).filter X,
      (actionPerm s.states.symm) f ∈ (states n K).filter X := by
    intro f hf
    exact List.mem_filter.mpr ⟨mem_states _, contained f (List.mem_filter.mp hf).2⟩
  intro f
  have he := finite_closed_iff ((states n K).filter X) (actionPerm s.states.symm) hc (act s.states f)
  apply Bool.eq_iff_iff.mpr
  have hh : X f = true ↔ X (act s.states f) = true := by
    simpa [actionPerm, mem_states] using he
  exact hh.symm

/-- The generator-closure loop terminates with an invariant raw region.
    It tightens the region by intersection with generator images, as in C++;
    equivariant:558–581, symmetry.hh:14–17, issue #331. -/
theorem close_invariant (G : List (Verified A)) (X : Region n K) : Symmetric G (close G X) := by
  apply closePass_fixed_invariant
  exact iterate_stable (states n K) mem_states (closePass G) (closePass_shrinks G)
    _ X (List.length_filter_le ..)

theorem close_preserves (G : List (Verified A)) (W X : Region n K)
    (hw : Symmetric G W) (hx : Subset W X) : Subset W (close G X) := by
  have one : ∀ Y, Subset W Y → Subset W (closePass G Y) := by
    intro Y hy
    unfold closePass
    have go : ∀ gs : List (Verified A), (∀ s ∈ gs, s ∈ G) → ∀ Z, Subset W Z →
        Subset W (gs.foldl (fun R s => fun f => R f && image s.states R f) Z) := by
      intro gs
      induction gs with
      | nil => exact fun _ _ h => h
      | cons s gs ih =>
        intro h Z hz
        apply ih (fun g hg => h g (List.mem_cons_of_mem _ hg))
        intro f hf
        apply Bool.and_eq_true_iff.mpr
        refine ⟨hz f hf, hz _ ?_⟩
        have hi := hw s (h _ (by simp)) (act s.states.symm f)
        simpa [hf] using hi.symm
    exact go G (fun _ h => h) Y hy
  have go : ∀ k, Subset W (iterate (closePass G) X k) := by
    intro k
    induction k with
    | zero => exact hx
    | succ k ih => exact one _ ih
  exact go _

/-- C++: equivariant_k_bounded_safety_aut.hh:945–947. Pick one representative,
    tighten with its T, then close the updated region under generators. -/
def representativeUpdate (K : Nat) (side : Bool) (G : List (Verified A))
    (i : Fin a → Bool) (X : Region n K) : Region n K :=
  close G (fun f => X f && computeT A K side X i f)

/-- C++: equivariant_k_bounded_safety_aut.hh:923–947. A fair complete round of
    the representative input picker, keeping its in-place update order. -/
def representativeRound (K : Nat) (side : Bool) (P : SweepPlan A side) (X : Region n K) : Region n K :=
  P.orbits.foldl (fun Y O => representativeUpdate K side P.generators O.representative Y) X

theorem representativeUpdate_shrinks (K : Nat) (side : Bool) (G : List (Verified A))
    (i : Fin a → Bool) (X : Region n K) : Subset (representativeUpdate K side G i X) X := by
  intro f h
  exact (Bool.and_eq_true_iff.mp (close_shrinks G _ f h)).1

theorem representativeRound_shrinks (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) : Subset (representativeRound K side P X) X :=
  fold_shrinks P.orbits
    (fun O Y => representativeUpdate K side P.generators O.representative Y)
    (fun O Y => representativeUpdate_shrinks K side P.generators O.representative Y) X

theorem representativeRound_preserves (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) (hx : Subset (final A K side) X) :
    Subset (final A K side) (representativeRound K side P X) := by
  have one : ∀ i Y, Subset (final A K side) Y →
      Subset (final A K side) (representativeUpdate K side P.generators i Y) := by
    intro i Y hy
    apply close_preserves P.generators _ _ (fun s _ => final_invariant s K side)
    intro f hf
    refine Bool.and_eq_true_iff.mpr ⟨hy f hf, ?_⟩
    apply computeT_mono A K side hy
    have hh : cpre A K side (final A K side) f = true := by rw [final_fixed]; exact hf
    exact (computeT_iff _ _ _ _ _ _).mpr (((cpre_iff _ _ _ _ _).mp hh).2 i)
  unfold representativeRound
  induction P.orbits generalizing X with
  | nil => exact hx
  | cons O os ih => exact ih _ (one O.representative X hx)

theorem representativeRound_fixed_cpre (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) (hf : representativeRound K side P X = X) : cpre A K side X = X := by
  have each := fold_fixed P.orbits
    (fun O Y => representativeUpdate K side P.generators O.representative Y)
    (fun O Y => representativeUpdate_shrinks K side P.generators O.representative Y) X hf
  have hx : Symmetric P.generators X := by
    obtain ⟨O, ho, _⟩ := P.covers (fun _ => false)
    rw [← each O ho]
    exact close_invariant P.generators _
  apply subset_antisymm (cpre_shrinks A K side X)
  intro f hxf
  apply (cpre_iff _ _ _ _ _).mpr
  refine ⟨hxf, fun i => ?_⟩
  obtain ⟨O, ho, hi⟩ := P.covers i
  obtain ⟨s, hs, he⟩ := List.mem_map.mp hi
  have ho_fixed := each O ho
  have hsymmetric := generated_invariant (P.member_generated O ho s hs) hx
  have hrep : ∀ g, X g = true → computeT A K side X O.representative g = true := by
    intro g hg
    have hh : representativeUpdate K side P.generators O.representative X g = true := by
      rw [ho_fixed]; exact hg
    exact (Bool.and_eq_true_iff.mp (close_shrinks P.generators _ g hh)).2
  have hin : X (act s.states.symm f) = true := by
    have := hsymmetric (act s.states.symm f)
    simpa [hxf] using this.symm
  have ht := hrep _ hin
  have hr := congrFun (reuse_exact s K side X hsymmetric O.representative) f
  change image s.states (computeT A K side X O.representative) f = _ at hr
  have hm : computeT A K side X i f = true := by
    rw [← computeT_projected K side X i, ← he, computeT_projected, ← hr]
    exact ht
  exact (computeT_iff _ _ _ _ _ _).mp hm

/-- C++: equivariant_k_bounded_safety_aut.hh:923–947. Terminating fixed-K solver. -/
def representativeFinal (K : Nat) (side : Bool) (P : SweepPlan A side) : Region n K :=
  iterate (representativeRound K side P) (safe A K) (states n K).length

/-- The representative-picker and generator-closure path returns the same
    greatest fixed point as plain CPre. Together with sweepFinal_eq, this covers
    both shipped paths, symmetry.hh:14–17, #331. -/
theorem representativeFinal_eq (K : Nat) (side : Bool) (P : SweepPlan A side) :
    representativeFinal K side P = final A K side := by
  let F := representativeRound K side P
  have hs := representativeRound_shrinks K side P
  have contains : ∀ k, Subset (final A K side) (iterate F (safe A K) k) := by
    intro k
    induction k with
    | zero => exact stage_safe K side _
    | succ k ih => exact representativeRound_preserves K side P _ ih
  have bounded : ∀ k, Subset (iterate F (safe A K) k) (safe A K) := by
    intro k
    induction k with
    | zero => exact fun _ h => h
    | succ k ih => exact fun f h => ih f (hs _ f h)
  have fixed := iterate_stable (states n K) mem_states F hs (states n K).length
    (safe A K) (List.length_filter_le ..)
  have post := representativeRound_fixed_cpre K side P (representativeFinal K side P) fixed
  apply subset_antisymm
  · apply final_greatest K side _ (bounded _)
    intro f h
    change cpre A K side (representativeFinal K side P) f = true
    rw [post]; exact h
  · exact contains _

/-- Both equivariant implementations give the plain verdict for the original
    or swapped protagonist side, proving the #331 reduction claim;
    symmetry.hh:14–17. -/
theorem verdicts_equal (K : Nat) (side : Bool) (P : SweepPlan A side) :
    sweepFinal A K side P (initial A K) = winner A K side ∧
    representativeFinal K side P (initial A K) = winner A K side := by
  rw [sweepFinal_eq, representativeFinal_eq]
  exact ⟨rfl, rfl⟩

end Symmetry

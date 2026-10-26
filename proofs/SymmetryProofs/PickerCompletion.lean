import SymmetryProofs.BooleanBoundary

namespace Symmetry
variable {A : Automaton n a}

theorem initial_safe (K : Nat) (hk : 1 ≤ K) : safe A K (initial A K) = true := by
  apply decide_eq_true
  intro q
  unfold initial
  split <;> cases A.counting q <;> simp_all

/-- Any selected input update, followed by closure, preserves every winning
    state. This holds for arbitrary picker order in the C++ while loop. -/
theorem selected_preserves (K : Nat) (side : Bool) (G : List (Verified A))
    (i : Fin a → Bool) (X : Region n K) (contains : Subset (final A K side) X) :
    Subset (final A K side) (representativeUpdate K side G i X) := by
  apply close_preserves G _ _ (fun s _ => final_invariant s K side)
  intro f hf
  refine Bool.and_eq_true_iff.mpr ⟨contains f hf, ?_⟩
  apply computeT_mono A K side contains
  have h : cpre A K side (final A K side) f = true := by rw [final_fixed]; exact hf
  exact (computeT_iff _ _ _ _ _ _).mpr (((cpre_iff _ _ _ _ _).mp h).2 i)

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:905–942,945–955.
    Histories reaching the next picker call start safe and continue only after
    the source's initial-out test. Inputs may be selected in any order. -/
inductive PickerReach (A : Automaton n a) (K : Nat) (side : Bool) (G : List (Verified A)) :
    Region n K → Prop
  | start (positive : 1 ≤ K) : PickerReach A K side G (safe A K)
  | update {X : Region n K} (prior : PickerReach A K side G X) (i : Fin a → Bool)
      (initial_in : representativeUpdate K side G i X (initial A K) = true) :
      PickerReach A K side G (representativeUpdate K side G i X)

/-- Every actual fixed-K picker call has a safe symmetric region containing
    the reference winner and the initial vector. The last condition explains
    why the no-input branch may return a successful optional result. -/
theorem picker_reach_invariants (h : PickerReach A K side G X) :
    Subset (final A K side) X ∧ Subset X (safe A K) ∧ Symmetric G X ∧ X (initial A K) = true := by
  induction h with
  | start positive =>
    exact ⟨stage_safe K side _, (fun _ h => h), (fun s _ => safe_commutes s K), initial_safe K positive⟩
  | update _ i hin ih =>
    refine ⟨selected_preserves K side G i _ ih.1, ?_, close_invariant G _, hin⟩
    intro f hf
    exact ih.2.1 f (representativeUpdate_shrinks K side G i _ f hf)

/-- C++: equivariant_k_bounded_safety_aut.hh:936–942. For the actual stored
    forward scan, exhaustion at a reachable call implies every representative
    backward constraint and the exact winning verdict. K and safe maxima feed
    the backward bridge; the history supplies the success-branch invariants. -/
theorem picker_completed (closed : BooleanClosed A) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (rows : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ rows, row.1 = O.representative)
    (compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o)
    (exhausted : (criticalCached (fun f v => storedSucc A K f v) maxima rows).1 = none) :
    (∀ row ∈ rows, (fun f => downset maxima f && backwardT A K side maxima row.1 f) = downset maxima) ∧
      downset maxima = final A K side ∧ winner A K side = true := by
  obtain ⟨contains, safeRegion, symmetric, initialIn⟩ := picker_reach_invariants reached
  have safeMax : ∀ m ∈ maxima, safe A K m = true := by
    intro m hm
    exact safeRegion m (List.any_eq_true.mpr ⟨m, hm, decide_eq_true (fun _ => Nat.le_refl _)⟩)
  have scalar := stored_picker_none closed K maxima safeMax rows exhausted
  have he := picker_success_eq K side P maxima safeMax contains symmetric rows retained scalar compatible
  refine ⟨picker_none_backward K positive side maxima safeMax rows scalar compatible, he, ?_⟩
  change final A K side (initial A K) = true
  rw [← he]
  exact initialIn

/-- The default Boolean preprocessing port supplies the graph condition, so
    the completion theorem needs no separately assumed forward-storage bridge. -/
theorem preprocessed_picker_completed (raw : Automaton n a) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan (preprocessBoolean raw) side) (maxima : List (State n K))
    (reached : PickerReach (preprocessBoolean raw) K side P.generators (downset maxima))
    (rows : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ rows, row.1 = O.representative)
    (compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge (preprocessBoolean raw) side row.1 o)
    (exhausted : (criticalCached (fun f v => storedSucc (preprocessBoolean raw) K f v) maxima rows).1 = none) :
    downset maxima = final (preprocessBoolean raw) K side ∧ winner (preprocessBoolean raw) K side = true :=
  (picker_completed (preprocessBoolean_closed raw) K positive side P maxima reached rows retained compatible exhausted).2

/-- The same success verdict holds for the actual priority picker, including
    arbitrary signed hit counts and the current cached response ordering. -/
theorem priority_picker_completed [DecidableEq (Fin a → Bool)]
    (closed : BooleanClosed A) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (queue : List (Int × ((Fin a → Bool) × List (Fin a → Bool))))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ queue.map Prod.snd, row.1 = O.representative)
    (compatible : ∀ row ∈ queue.map Prod.snd, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o)
    (exhausted : (criticalPQCached (fun f v => storedSucc A K f v) maxima queue).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  have hnone := (criticalPQ_none _ _ _).mp ((criticalPQCached_none _ _ _).mp exhausted)
  have hc := (criticalCached_none _ _ _).mpr ((criticalPicker_none _ _ _).mpr hnone)
  exact (picker_completed closed K positive side P maxima reached (queue.map Prod.snd) retained compatible hc).2

/-- The initial-out branch is also sound for arbitrary picker schedules:
    a shrinking sound update cannot remove an actual winning initial vector. -/
theorem picker_initial_out (K : Nat) (side : Bool) (G : List (Verified A))
    (X : Region n K) (reached : PickerReach A K side G X) (i : Fin a → Bool)
    (out : representativeUpdate K side G i X (initial A K) = false) : winner A K side = false := by
  have contains := selected_preserves K side G i X (picker_reach_invariants reached).1
  cases hw : winner A K side with
  | false => rfl
  | true => have := contains _ hw; rw [out] at this; contradiction

end Symmetry

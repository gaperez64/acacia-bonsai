import SymmetryProofs.PlanBuilder
import SymmetryProofs.Pickers
import SymmetryProofs.OrbitWitnesses

namespace Symmetry.BuilderWitnesses
open Builders

/-- Two blocks and three clients, with both input and output families. -/
def source : SourceStars OrbitWitnesses.aut 3 2 where
  families := ⟨OrbitWitnesses.apLayout, fun b => b == 0, by decide⟩
  pivot := 0
  star j := if j = 1 then OrbitWitnesses.s01 else
    if j = 2 then OrbitWitnesses.s02 else Verified.identity OrbitWitnesses.aut
  ap_star := by
    intro j hj
    have he : j = 1 ∨ j = 2 := by omega
    rcases he with rfl | rfl <;> rfl

/-- Raw dictionary rows, before inverse AP layout construction. -/
def rawFamilies : FamilyTable 6 3 where
  rows := [(true, OrbitWitnesses.apLayout.state 0), (false, OrbitWitnesses.apLayout.state 1)]
  unique := by decide

-- Check each builder stage separately so kernel reduction never expands the
-- detector, union-find table, alignment, and orbit enumeration in one term.
theorem families_accept : (buildFamilies OrbitWitnesses.aut rawFamilies).isSome = true := by decide

def checkedFamilies := (buildFamilies OrbitWitnesses.aut rawFamilies).get families_accept

theorem stars_checked : ∀ j : Fin 3, j ≠ 0 →
    check OrbitWitnesses.aut (sourceCandidate checkedFamilies.2 0 j (OrbitWitnesses.candidate j)) = true := by
  intro j hj
  have he : j = 1 ∨ j = 2 := by omega
  rcases he with rfl | rfl <;> decide

-- Keep checked maps explicit: looking up a state must not re-run the detector.
def checkedStars : SourceStars OrbitWitnesses.aut 3 checkedFamilies.1 where
  families := checkedFamilies.2
  pivot := 0
  star j := if hj : j ≠ 0 then
    accepted_sound OrbitWitnesses.aut (sourceCandidate checkedFamilies.2 0 j (OrbitWitnesses.candidate j)) (stars_checked j hj)
    else Verified.identity OrbitWitnesses.aut
  ap_star := by
    intro j hj
    rw [dite_eq_left hj]
    apply Perm.ext
    intro p
    rfl

theorem checkedStars_result : buildSourceStars checkedFamilies.2 0 OrbitWitnesses.candidate = some checkedStars := by
  unfold buildSourceStars
  rw [dite_eq_left stars_checked]
  rfl

theorem stars_accept : (buildSourceStars checkedFamilies.2 0 OrbitWitnesses.candidate).isSome = true := by
  rw [checkedStars_result]
  rfl

theorem roots : orbitRoots (checkedStars.generators.map Verified.states) =
    (fun q : Fin 7 => if q = 0 then 0 else if q.val < 4 then 3 else 6) := by
  have h : ∀ q : Fin 7, orbitRoots (checkedStars.generators.map Verified.states) q =
      (if q = 0 then 0 else if q.val < 4 then 3 else 6) := by decide
  exact funext h

theorem layout_accepts :
    (computeBlockLayout (checkedStars.generators.map Verified.states) checkedStars.indexSignature).isSome = true := by
  simp only [computeBlockLayout, recognizeLayout, roots]
  decide

-- Substitute the proved root table once; later lookups reuse that table.
def recognized : AlignedLayout 7 3 := by
  have h := layout_accepts
  simp only [computeBlockLayout, recognizeLayout, roots] at h
  exact Option.get _ h

theorem recognized_result :
    computeBlockLayout (checkedStars.generators.map Verified.states) checkedStars.indexSignature = some recognized := by
  have h := layout_accepts
  simp only [computeBlockLayout, recognizeLayout, roots] at h ⊢
  exact (Option.some_get h).symm

theorem recognized_eq : recognized = (⟨2, OrbitWitnesses.stateLayout, Perm.id⟩ : AlignedLayout 7 3) := by
  unfold recognized
  change (⟨2, _, _⟩ : AlignedLayout 7 3) = _
  congr 1
  · apply layout_ext
    decide
  · apply Perm.ext
    decide

-- Freeze the proved fields; reading the family table does not re-run checks.
def aligned : LayoutGroup OrbitWitnesses.aut 3 2 checkedFamilies.1 where
  states := OrbitWitnesses.stateLayout
  families := checkedFamilies.2
  pivot := 0
  star := checkedStars.star
  ap_star := checkedStars.ap_star
  state_star := by
    intro j hj
    apply Perm.ext
    intro q
    change (if h : j ≠ 0 then accepted_sound OrbitWitnesses.aut
      (sourceCandidate checkedFamilies.2 0 j (OrbitWitnesses.candidate j)) (stars_checked j h)
      else Verified.identity OrbitWitnesses.aut).states q = _
    rw [dite_eq_left hj]
    rfl

theorem aligned_result :
    alignGroup checkedStars (⟨2, OrbitWitnesses.stateLayout, Perm.id⟩ : AlignedLayout 7 3) = some aligned := by
  unfold alignGroup checkLayoutGroup
  dsimp only
  have h : ∀ j : Fin 3, j ≠ checkedStars.pivot → ∀ q,
      (checkedStars.star j).states q = OrbitWitnesses.stateLayout.phiFromSigma (transposition checkedStars.pivot j) q :=
    fun j hj q => congrArg (fun p : Perm (Fin 7) => p q) (aligned.state_star j hj)
  split
  · apply congrArg some
    unfold aligned
    congr 1
    congr 1
    exact reindexLayout_id _
  · next hn => exact False.elim (hn h)

theorem alignment_accepts : (alignGroup checkedStars recognized).isSome = true := by
  rw [recognized_eq, aligned_result]
  rfl

theorem caps_accept (side : Bool) : (buildPlan aligned side 3).isSome = true := by
  cases side <;> decide

/-- The fixture's plan uses the port's enumeration and checked two-block
    layout; no orbit member list or coverage proof is written by hand. -/
def produced (side : Bool) : SweepPlan OrbitWitnesses.aut side := builtPlan aligned side

/-- All successful stages compose into the raw builder's exact output. -/
theorem builder_result (side : Bool) :
    buildReduction OrbitWitnesses.aut rawFamilies 0 OrbitWitnesses.candidate side 3 = some (produced side) :=
  reduction_of_stages OrbitWitnesses.aut rawFamilies 0 OrbitWitnesses.candidate side 3
    checkedFamilies checkedStars ⟨2, OrbitWitnesses.stateLayout, Perm.id⟩ aligned
    (Option.some_get families_accept).symm checkedStars_result
    (recognized_result.trans (congrArg some recognized_eq)) aligned_result (caps_accept side)

/-- Both frames pass recognition, stabilizer alignment, stored-generator
    validation, and every enumeration cap. -/
theorem builder_accepts (side : Bool) :
    (buildReduction OrbitWitnesses.aut rawFamilies 0 OrbitWitnesses.candidate side 3).isSome = true := by
  rw [builder_result]
  rfl

/-- Slot recovery preserves the original stored star list, in its original
    order; it does not replace that list with all orbit transports. -/
theorem original_generators (side : Bool) : (produced side).generators = checkedStars.generators :=
  alignGroup_generators checkedStars ⟨2, OrbitWitnesses.stateLayout, Perm.id⟩ aligned_result

/-- The generated two-block fixture instantiates both final-set theorems. -/
theorem final_sets (side : Bool) :
    sweepFinal OrbitWitnesses.aut 1 side (produced side) = final OrbitWitnesses.aut 1 side ∧
      representativeFinal 1 side (produced side) = final OrbitWitnesses.aut 1 side := by
  have he := reduction_final_eq OrbitWitnesses.aut rawFamilies 0 OrbitWitnesses.candidate side 3 1
    (builder_result side)
  exact ⟨he.1, he.2.1⟩

/-- Builder output also instantiates both verdict and early-exit results. -/
theorem verdicts (side : Bool) :
    sweepFinal OrbitWitnesses.aut 1 side (produced side) (initial OrbitWitnesses.aut 1) =
      winner OrbitWitnesses.aut 1 side ∧
    representativeFinal 1 side (produced side) (initial OrbitWitnesses.aut 1) =
      winner OrbitWitnesses.aut 1 side := verdicts_equal 1 side (produced side)

/-- The count recursion and stable matcher exercise repeated types. -/
theorem enumeration :
    typeCounts 2 3 = [[0, 3], [1, 2], [2, 1], [3, 0]] ∧
    permutations [0, 0, 1] = [[0, 0, 1], [0, 1, 0], [1, 0, 0]] ∧
    (permutations [0, 1, 2]).length = 6 ∧
    takeType 1 [(0, 0), (1, 1), (2, 1)] = some ((1, 1), [(0, 0), (2, 1)]) := by decide

/-- An unsorted starting word omits earlier permutations; sorting is a real
    hypothesis of traversal completeness. The mismatched multiset declines. -/
theorem enumeration_mutants :
    [0, 0, 1] ∉ permutations [1, 0, 0] ∧
    matchEntries [0, 1] [(0, 0), (1, 0)] = none := by decide

/-- An oversized response alphabet declines rather than dropping responses. -/
theorem response_cap_mutant : (buildSharedPlan source false 13).isNone = true := by
  have cap : ∀ src : SourceStars OrbitWitnesses.aut 3 2,
      (buildSharedPlan src false 13).isNone = true := by
    intro src
    unfold buildSharedPlan
    cases hl : computeBlockLayout (src.generators.map Verified.states) src.indexSignature with
    | none => rfl
    | some L =>
      change (do let group ← alignGroup src L
                 buildPlan group false 13).isNone = true
      cases hg : alignGroup src L with
      | none => rfl
      | some D => simp [buildPlan]
  exact cap source

/-- The whole-family side check rejects a wrongly tagged raw row. -/
def mixedFamilies : FamilyTable 6 3 := { rawFamilies with
  rows := [(false, OrbitWitnesses.apLayout.state 0), (false, OrbitWitnesses.apLayout.state 1)] }

theorem family_mutant : (buildFamilies OrbitWitnesses.aut mixedFamilies).isNone = true := by decide

/-- Empty state orbits cannot meet the block gate. Reversing client slots
    in the state table alone fails the simultaneous state/AP star check. -/
theorem layout_mutants :
    (computeBlockLayout ([] : List (Perm (Fin 7))) (fun _ : Fin 3 => 0)).isNone = true ∧
    (checkLayoutGroup (reindexLayout OrbitWitnesses.stateLayout (transposition 1 2))
      source.families source.pivot source.star source.ap_star).isNone = true := by decide

/-- All responses of the concrete mixed-I/O fixture are represented. -/
theorem responses (side : Bool) (i : Fin 6 → Bool) (o : Fin 6 → Bool) :
    responseAction OrbitWitnesses.aut (merge OrbitWitnesses.aut side i o) ∈
      responseActions OrbitWitnesses.aut side i :=
  (responseActions_complete _ _ _ _).mpr ⟨o, rfl⟩

end Symmetry.BuilderWitnesses

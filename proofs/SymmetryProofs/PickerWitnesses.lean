import SymmetryProofs.PickerCompletion
import SymmetryProofs.Witnesses

namespace Symmetry.PickerWitnesses

/-- No APs or choices are needed to exercise picker completion. -/
def letter : Fin 0 → Bool := fun p => Fin.elim0 p

/-- A safe, Boolean-closed one-state graph; the loop does not increment. -/
def aut : Automaton 1 0 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => false
  isInput := fun p => Fin.elim0 p
  edges := [⟨0, .top, 0⟩]

def maximumState : State 1 1 := fun _ => 1
def maxima : List (State 1 1) := [maximumState]
def rows : List ((Fin 0 → Bool) × List (Fin 0 → Bool)) := [(letter, [letter])]

def plan : SweepPlan aut false where
  generators := []
  orbits := [⟨letter, [Verified.identity aut]⟩]
  member_generated := by
    intro O ho s hs
    simp only [List.mem_singleton] at ho
    subst O
    simp only [List.mem_singleton] at hs
    subst s
    exact .identity
  orbit_closed := by simp
  covers := by decide

theorem closed : BooleanClosed aut := by unfold BooleanClosed; decide
theorem safe_maxima : ∀ m ∈ maxima, safe aut 1 m = true := by decide
theorem cached_exhausted : (criticalCached (fun f v => storedSucc aut 1 f v) maxima rows).1 = none := by decide
theorem pq_exhausted : (criticalPQCached (fun f v => storedSucc aut 1 f v) maxima [(0, rows[0])]).1 = none := by
  rw [criticalPQCached_none, criticalPQ_none]
  decide

theorem scalar_exhausted : criticalPicker (fun f v => succ aut 1 f v) maxima rows = none :=
  stored_picker_none closed 1 maxima safe_maxima rows cached_exhausted

theorem compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge aut false row.1 o := by
  intro row _ v _
  exact ⟨v, funext fun p => Fin.elim0 p⟩

theorem retained : ∀ O ∈ plan.orbits, ∃ row ∈ rows, row.1 = O.representative := by
  intro O ho
  simp only [plan, List.mem_singleton] at ho
  subst O
  exact ⟨(letter, [letter]), by simp [rows], rfl⟩

theorem symmetric : Symmetric plan.generators (downset maxima) := by simp [Symmetric, plan]

/-- The real cached scan supplies both literal backward constraints and the
    representative fixed point, using the proved storage/preimage bridges. -/
theorem completion :
    (∀ row ∈ rows, (fun f => downset maxima f && backwardT aut 1 false maxima row.1 f) = downset maxima) ∧
      representativeRound 1 false plan (downset maxima) = downset maxima :=
  ⟨picker_none_backward 1 (by decide) false maxima safe_maxima rows scalar_exhausted compatible,
    picker_none_fixed 1 false plan maxima symmetric rows retained scalar_exhausted compatible⟩

/-- A concrete successful picker return gives the exact final region. -/
theorem success : downset maxima = final aut 1 false := by
  apply picker_success_eq 1 false plan maxima safe_maxima _ symmetric rows retained scalar_exhausted compatible
  intro f hf
  have hs := stage_safe (A := aut) 1 false (states 1 1).length f hf
  have h : ∀ g : State 1 1, safe aut 1 g = true → downset maxima g = true := by decide
  exact h f hs

theorem reached : PickerReach aut 1 false plan.generators (downset maxima) := by
  have he : downset maxima = safe aut 1 := by
    funext f
    simp [downset, maxima, below, maximumState, safe, aut]
    rfl
  rw [he]
  exact .start (by decide)

/-- The source's success branch is justified for a reachable cached call. -/
theorem reached_success : winner aut 1 false = true :=
  (picker_completed closed 1 (by decide) false plan maxima reached rows retained compatible cached_exhausted).2.2

/-- Splicing is exercised, not merely abstracted as arbitrary ordering. -/
theorem caching : scanCriticalRows (fun n : Nat => n == 1) [(7, [0, 1, 2])] =
    (none, [(7, [1, 0, 2])]) := by decide

/-- Mutant: the same safe Boolean coordinate now has a rejecting self-loop.
    It violates BooleanClosed; preprocessing reachability is a separate matter. -/
def bad : Automaton 1 0 := { aut with rejecting := fun _ => true }

/-- This is the smallest nonempty automaton and positive K. Both actual
    forward-storage picker scans are exhausted at the safe initial region,
    but its sole representative backward constraint fails. Thus positive K
    and safe maxima alone do not justify equivariant:936–942. -/
theorem completion_counterexample :
    safe bad 1 maximumState = true ∧
    (criticalCached (fun f v => storedSucc bad 1 f v) maxima rows).1 = none ∧
    (criticalPQCached (fun f v => storedSucc bad 1 f v) maxima [(0, rows[0])]).1 = none ∧
    backwardT bad 1 false maxima letter maximumState = false ∧
    cpre bad 1 false (downset maxima) maximumState = false ∧
    winner bad 1 false = false := by
  refine ⟨by decide, by decide, ?_, by decide, by decide, by decide⟩
  rw [criticalPQCached_none, criticalPQ_none]
  decide

/-- The discrepancy is exactly the storage conversion after forward apply:
    scalar successor overflows, stored successor looks safe. The existing
    backward_preimage theorem correctly rejects the scalar successor. -/
theorem counterexample_bridge :
    initial bad 1 = maximumState ∧
    succ bad 1 maximumState letter 0 = 2 ∧
    storedSucc bad 1 maximumState letter 0 = 1 ∧
    backward bad 1 maximumState letter 0 = 0 ∧
    ¬ BooleanClosed bad := by unfold BooleanClosed; decide

/-- Omitting the representative table also makes exhaustion vacuous; input
    coverage is needed even for scalar storage and a singleton region. -/
theorem missing_input_mutant :
    criticalPicker (fun f v => succ bad 1 f v) maxima ([] : List (Nat × List (Fin 0 → Bool))) = none ∧
    backwardT bad 1 false maxima letter maximumState = false := by decide

/-- The preprocessing port excludes the invalid Boolean/rejecting graph.
    Its output satisfies the graph contract the counterexample violates. -/
theorem preprocessing_excludes_mutant :
    BooleanClosed (preprocessBoolean bad) ∧ preprocessBoolean bad ≠ bad := by
  refine ⟨preprocessBoolean_closed bad, ?_⟩
  intro he
  have hc := preprocessBoolean_closed bad
  rw [he] at hc
  exact counterexample_bridge.2.2.2.2 hc

/-- The total saturation proof's hypotheses hold on both the stable graph and
    the rejecting-cycle mutant, whose saturation takes a strict pass. -/
theorem saturation_witnesses :
    saturationPass aut 0 (fun _ => 0) = (fun _ => 0) ∧
    saturationPass bad 1 (fun _ => 2) = (fun _ => 2) ∧
    saturationPass bad 1 (fun _ => 1) 0 = 2 := by decide

/-- The stable one-state preprocessing pass keeps the zero rank vector. -/
theorem saturation_zero : (saturation aut).2.val = (fun _ : Fin 1 => 0) := by
  simp only [saturation, aut, Bool.false_eq_true, and_false, ↓reduceIte]
  change (saturateRanks aut 0 (fun _ => 0) (by intro q; omega)).val = _
  rw [saturateRanks]
  rw [dite_eq_left (show ∀ q : Fin 1, saturationPass aut 0 (fun _ => 0) q = 0 from by decide)]

/-- This fixture is already in the Boolean preprocessor's output form. -/
theorem preprocessing_fixed : preprocessBoolean aut = aut := by
  unfold preprocessBoolean
  simp only [saturation_zero]
  change (⟨_, _, _, _, _⟩ : Automaton 1 0) = aut
  unfold aut
  congr 1 <;> decide

/-- The preprocessing-to-success theorem has all its hypotheses witnessed,
    including the actual cached no-input result and reachable safe region. -/
theorem preprocessed_success : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  have complete := preprocessed_picker_completed aut
  rw [preprocessing_fixed] at complete
  exact complete 1 (by decide) false plan maxima reached rows retained compatible cached_exhausted

/-- A critical hit lowers the signed queue priority, exercising the other
    branch of the priority picker's returned cache. -/
theorem priority_hit :
    ((criticalPQCached (fun f v => succ bad 1 f v) maxima [(0, (letter, [letter]))]).2.map Prod.fst) = [-1] := by
  simp only [criticalPQCached, List.mergeSort_singleton]
  decide

def zeroAut : Automaton 1 0 := { aut with counting := fun _ => true }
def zeroMaximum : State 1 0 := fun _ => 0

/-- Without positive K, even an uncompressed graph can exhaust the picker
    while the safe initial region excludes init. Thus success needs K >= 1. -/
theorem zero_bound_mutant :
    BooleanClosed zeroAut ∧ safe zeroAut 0 zeroMaximum = true ∧
    (criticalCached (fun f v => storedSucc zeroAut 0 f v) [zeroMaximum] rows).1 = none ∧
    winner zeroAut 0 false = false := by
  unfold BooleanClosed
  decide

end Symmetry.PickerWitnesses

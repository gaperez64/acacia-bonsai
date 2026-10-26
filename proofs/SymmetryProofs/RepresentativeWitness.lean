import SymmetryProofs.PlanBuilder
import SymmetryProofs.PickerCompletion
import SymmetryProofs.Witnesses

namespace Symmetry.RepresentativeWitness
open Builders

/-- Five clients take the production representative/closure branch. Two
    blocks meet its payoff gate; all response actions coincide on this graph. -/
def aut : Automaton 11 10 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => false
  isInput := fun p => p.val < 5
  edges := []

def stateRows : List (Fin 5 → Fin 11) :=
  [fun j => ⟨j.val + 1, by omega⟩, fun j => ⟨j.val + 6, by omega⟩]

def layout : BlockLayout 11 5 2 := layoutOfRows stateRows (by decide)

def families : FamilyTable 10 5 where
  rows := [(true, fun j => ⟨j.val, by omega⟩), (false, fun j => ⟨j.val + 5, by omega⟩)]
  unique := by decide

def candidate (j : Fin 5) : Candidate 11 10 where
  phi := layout.phiFromSigma (transposition 0 j)
  phiInv := (layout.phiFromSigma (transposition 0 j)).invFun
  pi := fun p => p
  piInv := fun p => p

theorem families_accept : (buildFamilies aut families).isSome = true := by decide

def checkedFamilies := (buildFamilies aut families).get families_accept

theorem stars_checked : ∀ j : Fin 5, j ≠ 0 →
    check aut (sourceCandidate checkedFamilies.2 0 j (candidate j)) = true := by
  intro j hj
  have he : j = 1 ∨ j = 2 ∨ j = 3 ∨ j = 4 := by omega
  rcases he with rfl | rfl | rfl | rfl <;> decide

-- Keep checked maps explicit: looking up a state must not re-run the detector.
def checkedStars : SourceStars aut 5 checkedFamilies.1 where
  families := checkedFamilies.2
  pivot := 0
  star j := if hj : j ≠ 0 then
    accepted_sound aut (sourceCandidate checkedFamilies.2 0 j (candidate j)) (stars_checked j hj)
    else Verified.identity aut
  ap_star := by
    intro j hj
    rw [dite_eq_left hj]
    apply Perm.ext
    intro p
    rfl

theorem checkedStars_result : buildSourceStars checkedFamilies.2 0 candidate = some checkedStars := by
  unfold buildSourceStars
  rw [dite_eq_left stars_checked]
  rfl

theorem stars_accept : (buildSourceStars checkedFamilies.2 0 candidate).isSome = true := by
  rw [checkedStars_result]
  rfl

theorem roots : orbitRoots (checkedStars.generators.map Verified.states) =
    (fun q : Fin 11 => if q = 0 then 0 else if q.val < 6 then 5 else 10) := by
  have h : ∀ q : Fin 11, orbitRoots (checkedStars.generators.map Verified.states) q =
      (if q = 0 then 0 else if q.val < 6 then 5 else 10) := by decide
  exact funext h

theorem layout_accepts :
    (computeBlockLayout (checkedStars.generators.map Verified.states) checkedStars.indexSignature).isSome = true := by
  simp only [computeBlockLayout, recognizeLayout, roots]
  decide

-- Substitute the proved root table once; later lookups reuse that table.
def recognized : AlignedLayout 11 5 := by
  have h := layout_accepts
  simp only [computeBlockLayout, recognizeLayout, roots] at h
  exact Option.get _ h

theorem recognized_result :
    computeBlockLayout (checkedStars.generators.map Verified.states) checkedStars.indexSignature = some recognized := by
  have h := layout_accepts
  simp only [computeBlockLayout, recognizeLayout, roots] at h ⊢
  exact (Option.some_get h).symm

theorem recognized_eq : recognized = (⟨2, layout, Perm.id⟩ : AlignedLayout 11 5) := by
  unfold recognized
  change (⟨2, _, _⟩ : AlignedLayout 11 5) = _
  congr 1
  · apply layout_ext
    decide
  · apply Perm.ext
    decide

-- Freeze the proved fields; reading the family table does not re-run checks.
def aligned : LayoutGroup aut 5 2 checkedFamilies.1 where
  states := layout
  families := checkedFamilies.2
  pivot := 0
  star := checkedStars.star
  ap_star := checkedStars.ap_star
  state_star := by
    intro j hj
    apply Perm.ext
    intro q
    change (if h : j ≠ 0 then accepted_sound aut
      (sourceCandidate checkedFamilies.2 0 j (candidate j)) (stars_checked j h)
      else Verified.identity aut).states q = _
    rw [dite_eq_left hj]
    rfl

theorem aligned_result :
    alignGroup checkedStars (⟨2, layout, Perm.id⟩ : AlignedLayout 11 5) = some aligned := by
  unfold alignGroup checkLayoutGroup
  dsimp only
  have h : ∀ j : Fin 5, j ≠ checkedStars.pivot → ∀ q,
      (checkedStars.star j).states q = layout.phiFromSigma (transposition checkedStars.pivot j) q :=
    fun j hj q => congrArg (fun p : Perm (Fin 11) => p q) (aligned.state_star j hj)
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

theorem caps_accept : (buildPlan aligned false 5).isSome = true := by decide

def plan : SweepPlan aut false := builtPlan aligned false

/-- Five clients and two blocks pass the production representative-path
    gates. The raw builder returns exactly this enumerated plan. -/
theorem builder_result : buildReduction aut families 0 candidate false 5 = some plan :=
  reduction_of_stages aut families 0 candidate false 5
    checkedFamilies checkedStars ⟨2, layout, Perm.id⟩ aligned
    (Option.some_get families_accept).symm checkedStars_result
    (recognized_result.trans (congrArg some recognized_eq)) aligned_result caps_accept

theorem accepted : (buildReduction aut families 0 candidate false 5).isSome = true := by
  rw [builder_result]
  rfl

def maximumState : State 11 1 := fun _ => 1
def maxima : List (State 11 1) := [maximumState]

/-- Every response has the same action on this empty-edge graph, so keeping
    one compatible response per input exercises the action-deduplication bridge. -/
theorem identical_responses (K : Nat) (f : State 11 K) (v w : Fin 10 → Bool) :
    succ aut K f v = succ aut K f w := responseAction_succ aut K f v w rfl

/-- Identical action vectors deduplicate to one response per input. -/
def rows : List ((Fin 10 → Bool) × List (Fin 10 → Bool)) :=
  plan.orbits.map fun O => (O.representative, [merge aut false O.representative (fun _ => false)])

theorem all_actions_survive (f : State 11 1) (v : Fin 10 → Bool) :
    downset maxima (storedSucc aut 1 f v) = true := by
  apply List.any_eq_true.mpr
  refine ⟨maximumState, by simp [maxima], ?_⟩
  apply decide_eq_true
  intro q
  change (storedSucc aut 1 f v q).val ≤ 1
  change (if (succ aut 1 f v q).val = 0 then (0 : Fin 3) else 1).val ≤ 1
  by_cases hz : (succ aut 1 f v q).val = 0 <;> simp [hz]

theorem retained : ∀ O ∈ plan.orbits, ∃ row ∈ rows, row.1 = O.representative := by
  intro O ho
  exact ⟨_, List.mem_map.mpr ⟨O, ho, rfl⟩, rfl⟩

theorem compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge aut false row.1 o := by
  intro row hr v hv
  obtain ⟨O, _, rfl⟩ := List.mem_map.mp hr
  simp only [List.mem_singleton] at hv
  subst v
  exact ⟨fun _ => false, rfl⟩

theorem exhausted : (criticalCached (fun f v => storedSucc aut 1 f v) maxima rows).1 = none := by
  rw [criticalCached_none, criticalPicker_none]
  intro f _ row hr
  obtain ⟨O, _, rfl⟩ := List.mem_map.mp hr
  exact ⟨merge aut false O.representative (fun _ => false), List.mem_singleton.mpr rfl,
    all_actions_survive f _⟩

theorem region_safe : downset maxima = safe aut 1 := by
  funext f
  simp only [downset, maxima, List.any_cons, List.any_nil, Bool.or_false, below,
    maximumState, safe, aut, Bool.false_eq_true, ↓reduceIte]
  rfl

theorem reached : PickerReach aut 1 false plan.generators (downset maxima) := by
  rw [region_safe]
  exact .start (by decide)

theorem closed : BooleanClosed aut := by
  intro q r v he
  simp [Automaton.enabled, aut] at he

/-- Actual cached picker exhaustion returns the plain winner on a fixture
    that satisfies the production representative-path gates. -/
theorem completed : downset maxima = final aut 1 false ∧ winner aut 1 false = true :=
  (picker_completed closed 1 (by decide) false plan maxima reached rows retained compatible exhausted).2

/-- Arbitrary priority counts retain the same successful no-input verdict. -/
theorem priority_completed : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  let queue := rows.map fun row => ((-7 : Int), row)
  have he : queue.map Prod.snd = rows := by simp [queue, List.map_map, Function.comp_def]
  apply priority_picker_completed closed 1 (by decide) false plan maxima reached queue
  · simpa only [he] using retained
  · simpa only [he] using compatible
  · rw [criticalPQCached_none, criticalPQ_none, he]
    exact (criticalPicker_none _ _ _).mp ((criticalCached_none _ _ _).mp exhausted)

end Symmetry.RepresentativeWitness

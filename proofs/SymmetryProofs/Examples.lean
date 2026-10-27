import SymmetryProofs.BuilderExamples
import SymmetryProofs.Mona

namespace Symmetry
private def finiteForall (U : List α) (full : ∀ x, x ∈ U) (P : α → Prop)
    [DecidablePred P] : Decidable (∀ x, P x) :=
  decidable_of_iff (∀ x ∈ U, P x) ⟨fun h x => h x (full x), fun h x _ => h x⟩
local instance (P : (Fin n → Bool) → Prop) [DecidablePred P] : Decidable (∀ f, P f) :=
  finiteForall (letters n) mem_letters P
local instance (P : (Fin n → Fin m) → Prop) [DecidablePred P] : Decidable (∀ f, P f) :=
  finiteForall (vectors (List.finRange m) n) (mem_vectors _ List.mem_finRange) P

private def noAPPlan (A : Automaton n 0) : SweepPlan A false where
  generators := []
  orbits := [⟨fun p => Fin.elim0 p, [Verified.identity A]⟩]
  member_generated := by
    intro O ho t ht
    simp only [List.mem_singleton] at ho; subst O
    simp only [List.mem_singleton] at ht; subst t
    exact .identity
  orbit_closed := by simp
  covers := by
    intro i; refine ⟨⟨fun p => Fin.elim0 p, [Verified.identity A]⟩, by simp, ?_⟩
    simp only [orbitLetters, List.map_cons, List.map_nil, List.mem_singleton]
    funext p; exact Fin.elim0 p

-- Seven-state #331 fixture; equivariant_pre_test.cc:510–553.
namespace Reproducer331
def aut : Automaton 7 3 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => true
  isInput := fun p => p != 1
  edges := [⟨0, .atom 0, 1⟩, ⟨0, .atom 1, 2⟩, ⟨0, .atom 2, 3⟩,
    ⟨1, .atom 0, 4⟩, ⟨2, .atom 1, 5⟩, ⟨3, .atom 2, 6⟩, ⟨4, .top, 4⟩, ⟨5, .top, 5⟩, ⟨6, .top, 6⟩]
def candidate (i j : Fin 3) : Candidate 7 3 where
  phi := fun q => swap (⟨i.val + 1, by omega⟩ : Fin 7) ⟨j.val + 1, by omega⟩
    (swap (⟨i.val + 4, by omega⟩ : Fin 7) ⟨j.val + 4, by omega⟩ q)
  phiInv := fun q => swap (⟨i.val + 1, by omega⟩ : Fin 7) ⟨j.val + 1, by omega⟩
    (swap (⟨i.val + 4, by omega⟩ : Fin 7) ⟨j.val + 4, by omega⟩ q)
  pi := swap i j
  piInv := swap i j
  families := [(true, [0, 1, 2])]
theorem old_accepts_all : ∀ i j : Fin 3, unguardedCheck aut (candidate i j) = true := by decide
theorem family_rejected : ∀ tag : Bool,
    familyGuard aut { candidate 0 1 with families := [(tag, [0, 1, 2])] } = false := by decide
theorem guarded_rejects_crossing : ∀ i j : Fin 3, aut.isInput i ≠ aut.isInput j →
    check aut (candidate i j) = false := by decide
theorem whole_mixed_family_rejected : ∀ i j : Fin 3, check aut (candidate i j) = false := by decide
def stateSwap : Perm (Fin 7) where
  toFun := (candidate 0 1).phi
  invFun := (candidate 0 1).phiInv
  left_inv := by decide
  right_inv := by decide
def avoid0 : Region 7 1 := fun f => (f 1).val == 0
theorem old_cpre_fails : cpre aut 1 false (image stateSwap avoid0) (initial aut 1) = true ∧
    image stateSwap (cpre aut 1 false avoid0) (initial aut 1) = false := by decide
theorem old_swapped_cpre_fails : cpre aut 1 true (image stateSwap avoid0) (initial aut 1) = false ∧
    image stateSwap (cpre aut 1 true avoid0) (initial aut 1) = true := by decide

end Reproducer331

namespace PickerWitnesses
def letter : Fin 0 → Bool := fun p => Fin.elim0 p
def aut : Automaton 1 0 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => false
  isInput := fun p => Fin.elim0 p
  edges := [⟨0, .top, 0⟩]
def maximumState : State 1 1 := fun _ => 1
def maxima : List (State 1 1) := [maximumState]
def rows : List ((Fin 0 → Bool) × List (Fin 0 → Bool)) := [(letter, [letter])]
def plan : SweepPlan aut false := noAPPlan aut
theorem closed : BooleanClosed aut := by unfold BooleanClosed; decide
theorem cached_exhausted : (criticalCached (fun f v => storedSucc aut 1 f v) maxima rows).1 = none := by decide
theorem compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge aut false row.1 o := by intro row _ v _; exact ⟨v, funext fun p => Fin.elim0 p⟩
theorem retained : ∀ O ∈ plan.orbits, ∃ row ∈ rows, row.1 = O.representative := by
  intro O ho; simp only [plan, noAPPlan, List.mem_singleton] at ho
  subst O; exact ⟨(letter, [letter]), by simp [rows], rfl⟩
theorem reached : PickerReach aut 1 false plan.generators (downset maxima) := by
  have he : downset maxima = safe aut 1 := by
    funext f; simp [downset, maxima, below, maximumState, safe, aut]
    rfl
  rw [he]; exact .start (by decide)
theorem reached_success : winner aut 1 false = true :=
  (picker_completed closed 1 (by decide) false plan maxima reached rows retained compatible cached_exhausted).2.2
theorem priority_completed : downset maxima = final aut 1 false ∧ winner aut 1 false = true :=
  priority_picker_completed closed 1 (by decide) false plan maxima reached [(0, rows[0])]
    (by simpa [rows] using retained) (by simpa [rows] using compatible)
    (by rw [criticalPQCached_none, criticalPQ_none]; decide)
theorem caching : scanCriticalRows (fun n : Nat => n == 1) [(7, [0, 1, 2])] =
    (none, [(7, [1, 0, 2])]) := by decide
def bad : Automaton 1 0 := { aut with rejecting := fun _ => true }
-- Presence storage hides rejecting overflow; critical.hh:49–78, X_and_boolvec.hh:69–71.
theorem completion_counterexample : safe bad 1 maximumState = true ∧
    (criticalCached (fun f v => storedSucc bad 1 f v) maxima rows).1 = none ∧
    (criticalPQCached (fun f v => storedSucc bad 1 f v) maxima [(0, rows[0])]).1 = none ∧
    backwardT bad 1 false maxima letter maximumState = false ∧
    cpre bad 1 false (downset maxima) maximumState = false ∧ winner bad 1 false = false := by
  refine ⟨by decide, by decide, ?_, by decide, by decide, by decide⟩
  rw [criticalPQCached_none, criticalPQ_none]; decide
theorem counterexample_bridge : initial bad 1 = maximumState ∧ succ bad 1 maximumState letter 0 = 2 ∧
    storedSucc bad 1 maximumState letter 0 = 1 ∧ backward bad 1 maximumState letter 0 = 0 ∧
    ¬ BooleanClosed bad := by unfold BooleanClosed; decide
-- Preprocessing excludes the rejecting Boolean graph; forward_saturation.hh:60–87.
theorem missing_input_mutant :
    criticalPicker (fun f v => succ bad 1 f v) maxima ([] : List (Nat × List (Fin 0 → Bool))) = none ∧
    backwardT bad 1 false maxima letter maximumState = false := by decide
theorem preprocessing_excludes_mutant :
    BooleanClosed (preprocessBoolean bad) ∧ preprocessBoolean bad ≠ bad := by
  refine ⟨preprocessBoolean_closed bad, ?_⟩
  intro he; have hc := preprocessBoolean_closed bad
  rw [he] at hc; exact counterexample_bridge.2.2.2.2 hc
theorem saturation_witnesses : saturationPass aut 0 (fun _ => 0) = (fun _ => 0) ∧
    saturationPass bad 1 (fun _ => 2) = (fun _ => 2) ∧ saturationPass bad 1 (fun _ => 1) 0 = 2 := by decide
theorem saturation_zero : (saturation aut).2.val = (fun _ : Fin 1 => 0) := by
  simp only [saturation, aut, Bool.false_eq_true, and_false, ↓reduceIte]
  change (saturateRanks aut 0 (fun _ => 0) (by intro q; omega)).val = _
  rw [saturateRanks]
  rw [dite_eq_left (show ∀ q : Fin 1, saturationPass aut 0 (fun _ => 0) q = 0 from by decide)]
theorem preprocessing_fixed : preprocessBoolean aut = aut := by
  unfold preprocessBoolean; simp only [saturation_zero]
  change (⟨_, _, _, _, _⟩ : Automaton 1 0) = aut
  unfold aut; congr 1 <;> decide
theorem preprocessed_success : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  have complete := preprocessed_picker_completed aut
  rw [preprocessing_fixed] at complete
  exact complete 1 (by decide) false plan maxima reached rows retained compatible cached_exhausted
theorem priority_hit :
    ((criticalPQCached (fun f v => succ bad 1 f v) maxima [(0, (letter, [letter]))]).2.map Prod.fst) = [-1] := by
  simp only [criticalPQCached, List.mergeSort_singleton]
  decide
def zeroAut : Automaton 1 0 := { aut with counting := fun _ => true }
def zeroMaximum : State 1 0 := fun _ => 0
theorem zero_bound_mutant : BooleanClosed zeroAut ∧ safe zeroAut 0 zeroMaximum = true ∧
    (criticalCached (fun f v => storedSucc zeroAut 0 f v) [zeroMaximum] rows).1 = none ∧
    winner zeroAut 0 false = false := by unfold BooleanClosed; decide

end PickerWitnesses

namespace TableWitnesses
open ActionTable
def zero : Letter 2 := fun _ => false
def one : Letter 2 := fun _ => true
def mixed : Letter 2 := fun p => p = 0
def aut : Automaton 2 2 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => true
  isInput := fun _ => true
  edges := [⟨0, .top, 0⟩, ⟨0, .disj (.conj (.atom 0) (.neg (.atom 1)))
    (.conj (.neg (.atom 0)) (.atom 1)), 1⟩]
def reps : List (Letter 2) := [zero, one]
-- Equal input rows can lose a representative key without losing its action class; standard.hh:75–118.
theorem input_row_dedup : zero ≠ one ∧ (grouped aut false).length = 4 ∧
    (restrict reps (grouped aut false)).length = 2 ∧ (physical aut false reps).length = 1 ∧
    (∀ row ∈ physical aut false reps, row.1 one = false) ∧
    responses aut false zero = responses aut false one := by decide
theorem precomputer_row_dedup :
    let D := precomputerDiagram aut false
    (precomputerRows D).length = 4 ∧ (restrict reps (precomputerRows D)).length = 2 ∧
    (precomputerTable D reps).length = 1 ∧ (∀ row ∈ precomputerTable D reps, row.1 one = false) := by decide
theorem quotient_covers : Refines aut false reps (physical aut false reps) :=
  physical_refinement aut false reps
theorem grouped_partial_cube :
    let A := { aut with edges := [⟨0, .top, 0⟩, ⟨0, .atom 0, 1⟩] }
    (grouped A false).length = 2 ∧
    ∃ row ∈ grouped A false, row.1 zero = true ∧ row.1 (fun p => p = 1) = true := by decide
def plan : SweepPlan aut false where
  generators := []
  orbits := (letters 2).map fun i => ⟨i, [Verified.identity aut]⟩
  member_generated := by
    intro O ho s hs; obtain ⟨i, _, rfl⟩ := List.mem_map.mp ho
    simp only [List.mem_singleton] at hs; subst s
    exact .identity
  orbit_closed := by simp
  covers := by
    intro i; refine ⟨⟨i, [Verified.identity aut]⟩, List.mem_map.mpr ⟨i, mem_letters i, rfl⟩, ?_⟩
    simp only [orbitLetters, List.map_cons, List.map_nil, List.mem_singleton]; rfl
def maximumState : State 2 1 := fun _ => 1
def maxima : List (State 2 1) := [maximumState]
def table : List (Row 2 2) := physical aut false (plan.orbits.map InputOrbit.representative)
theorem region_safe : downset maxima = safe aut 1 := by
  funext f; simp only [downset, maxima, List.any_cons, List.any_nil, Bool.or_false, below,
    maximumState, safe, aut, ↓reduceIte]
  rfl
theorem reached : PickerReach aut 1 false plan.generators (downset maxima) := by rw [region_safe]; exact .start (by decide)
theorem closed : BooleanClosed aut := by intro q r v _ h; contradiction
theorem exhausted : (criticalCached (applyAction aut 1) maxima table).1 = none := by decide
theorem completed : downset maxima = final aut 1 false ∧ winner aut 1 false = true :=
  physical_completed aut closed 1 (by decide) false plan maxima reached exhausted
theorem precomputer_success : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  apply precomputer_completed aut closed 1 (by decide) false plan maxima (picker_reach_invariants reached)
    (precomputerDiagram aut false) (precomputer_denotes aut false)
  decide
theorem selected : ∀ row ∈ table, ∃ i ∈ plan.orbits.map InputOrbit.representative, row.1 i = true ∧
      update aut 1 plan.generators maxima row = representativeUpdate 1 false plan.generators i (downset maxima) := by
  intro row hr; exact selected_refinement aut 1 (by decide) false plan.generators maxima
    (by decide) _ _ (physical_refinement aut false _) row hr
theorem update_reachable : ∀ row ∈ table,
    Subset (final aut 1 false) (update aut 1 plan.generators maxima row) ∧
    PickerReach aut 1 false plan.generators (update aut 1 plan.generators maxima row) := by
  have hin : ∀ row ∈ table, update aut 1 plan.generators maxima row (initial aut 1) = true := by decide
  intro row hr; exact physical_update_reach aut 1 (by decide) false plan maxima reached row hr (hin row hr)
def outputAut : Automaton 2 2 := { aut with isInput := fun p => p = 0 }
theorem full_responses :
    (responses outputAut false zero).any (fun t => downset maxima (applyAction outputAut 1 maximumState t)) =
      computeT outputAut 1 false (downset maxima) zero maximumState ∧
    classBackward outputAut 1 maxima (responses outputAut false zero) = backwardT outputAut 1 false maxima zero :=
  ⟨class_predecessor_exact outputAut 1 false _ _ zero (mem_responses outputAut false zero) _,
    class_backward_exact outputAut 1 false _ _ zero (mem_responses outputAut false zero)⟩
def queue : List (Int × Row 2 2) := table.map fun row => (-7, row)
theorem queue_refines : Refines aut false (plan.orbits.map InputOrbit.representative) (queue.map Prod.snd) := by
  simpa [queue, table, List.map_map, Function.comp_def] using physical_refinement aut false
    (plan.orbits.map InputOrbit.representative)
theorem cache_refines :
    Refines aut false (plan.orbits.map InputOrbit.representative) (criticalCached (applyAction aut 1) maxima table).2 ∧
    Refines aut false (plan.orbits.map InputOrbit.representative)
      ((criticalPQCached (applyAction aut 1) maxima queue).2.map Prod.snd) :=
  ⟨(physical_refinement aut false _).scan 1 maxima, queue_refines.priority 1 maxima queue⟩
def bad : Automaton 2 2 := { aut with rejecting := fun q => q = 1 }
-- Dropping a different action class creates false exhaustion; standard.hh:75–118.
theorem wrong_class_mutant :
    let rows := physical bad false [zero]
    (criticalCached (applyAction bad 1) maxima rows).1 = none ∧
    (∀ row ∈ rows, row.2 ≠ responses bad false mixed) ∧
    backwardT bad 1 false maxima mixed maximumState = false ∧
    cpre bad 1 false (downset maxima) maximumState = false := by decide
theorem mutant_not_refinement : ¬ Refines bad false [zero, mixed] (physical bad false [zero]) := by
  intro h; obtain ⟨row, hr, hc⟩ := h.covers mixed (by simp)
  have ha := (hc (action bad mixed)).mpr ⟨zero, by decide⟩
  have hn : ∀ row ∈ physical bad false [zero], action bad mixed ∉ row.2 := by decide
  exact hn row hr ha

end TableWitnesses

namespace MonaWitnesses
open ActionTable
def empty : Action 1 := fun _ _ => false
def live : Action 1 := fun _ _ => true
def zero : Letter 2 := fun _ => false
def one : Letter 2 := fun _ => true
def aut : Automaton 1 2 where
  init := 0
  rejecting := fun _ => true
  counting := fun _ => true
  isInput := fun p => p = 0
  edges := [⟨0, .conj (.atom 0) (.neg (.atom 1)), 0⟩]
def diagram := precomputerDiagram aut false
-- Pre-#336 skipped false residuals at both frontiers; mona.hh:142–190.
def preFixTable (quotient : Bool) (reps : List (Letter 2)) : List (Row 1 2) :=
  dedup (restrict reps ((diagram.rows.filter (fun row => match row.2 with
    | .leaf t => t != empty | .branch _ _ _ => true)).map
    fun row => (row.1, (monaOutputs quotient row.2).filter (· != empty))))
theorem preFix_not_refines (quotient : Bool) :
    (¬ Refines aut false [zero] (preFixTable quotient [zero])) ∧
    (¬ Refines aut false [one] (preFixTable quotient [one])) := by
  constructor
  · intro h; obtain ⟨row, hr, _⟩ := h.covers zero (by simp)
    have he : preFixTable quotient [zero] = [] := by cases quotient <;> decide
    simp [he] at hr
  · intro h; obtain ⟨row, hr, hc⟩ := h.covers one (by simp)
    have ha := (hc empty).mpr ⟨one, by decide⟩
    have hn : ∀ row ∈ preFixTable quotient [one], empty ∉ row.2 := by cases quotient <;> decide
    exact hn row hr ha
-- The old explicit path left empty rows unencoded; mona.hh:384–394.
def preFixExplicit (reps : List (Letter 2)) : List (Row 1 2) :=
  dedup (restrict reps (explicitRows (diagram.map (Diagram.map fun t => if t == empty then none else some t))))
theorem preFix_explicit_not_refines :
    (¬ Refines aut false [zero] (preFixExplicit [zero])) ∧
    (¬ Refines aut false [one] (preFixExplicit [one])) := by
  have hz : preFixExplicit [zero] = preFixTable true [zero] := by decide
  have ho : preFixExplicit [one] = preFixTable true [one] := by decide
  rw [hz, ho]; exact preFix_not_refines true
theorem fixed_refines (quotient : Bool) :
    Refines aut false [zero, one] (monaTable quotient diagram [zero, one]) :=
  mona_refinement aut false quotient diagram (precomputer_realizable aut false) (precomputer_denotes aut false) _
theorem explicit_refines : Refines aut false [zero, one] (explicitTable aut false [zero, one]) :=
  explicit_refinement aut false _
theorem empty_terminals :
    (monaRows false diagram).map Prod.snd = [[empty], [empty, live]] ∧
    (explicitTable aut false [zero, one]).map Prod.snd = [[empty], [empty, live]] := by decide
-- High-first order, first occurrence and per-input reset; mona.hh:146–188,406–435.
theorem quotient_order :
    let outputs : Diagram 3 (Action 1) := .branch 1 (.leaf empty) (.branch 2 (.leaf live) (.leaf empty))
    let D := Diagram.branch 0 (.leaf outputs) (.leaf (.leaf empty))
    (monaRows false D).map Prod.snd = [[empty, live, empty], [empty]] ∧
    (monaRows true D).map Prod.snd = [[empty, live], [empty]] := by decide
def plan : SweepPlan aut false where
  generators := []
  orbits := (letters 2).map fun i => ⟨i, [Verified.identity aut]⟩
  member_generated := by
    intro O ho s hs; obtain ⟨i, _, rfl⟩ := List.mem_map.mp ho
    simp only [List.mem_singleton] at hs; subst s; exact .identity
  orbit_closed := by simp
  covers := by
    intro i; refine ⟨⟨i, [Verified.identity aut]⟩, List.mem_map.mpr ⟨i, mem_letters i, rfl⟩, ?_⟩
    simp only [orbitLetters, List.map_cons, List.map_nil, List.mem_singleton]; rfl
def maxima : List (State 1 1) := [fun _ => 1]
theorem reached : PickerReach aut 1 false plan.generators (downset maxima) := by
  have he : ∀ f, downset maxima f = safe aut 1 f := by decide
  rw [funext he]; exact .start (by decide)
theorem completion (quotient : Bool) : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  apply mona_completed aut (by intro q r v _ h; contradiction) 1 (by decide) false plan maxima reached
    quotient diagram (precomputer_realizable aut false) (precomputer_denotes aut false)
  cases quotient <;> decide
theorem explicit_completion : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  apply explicit_completed aut (by intro q r v _ h; contradiction) 1 (by decide) false plan maxima reached
  decide
theorem lost_response (quotient : Bool) :
    (criticalCached (applyAction aut 1) maxima (preFixTable quotient [one])).1 ≠ none ∧
    cpre aut 1 false (downset maxima) (initial aut 1) = true := by cases quotient <;> decide

end MonaWitnesses

namespace AdaptiveWitnesses
open ActionTable
def letter : Letter 0 := fun p => Fin.elim0 p
def aut : Automaton 2 0 where
  init := 0
  rejecting := fun q => q = 1
  counting := fun _ => true
  isInput := fun p => Fin.elim0 p
  edges := [⟨0, .top, 1⟩]
def plan : SweepPlan aut false := noAPPlan aut
def startMax : State 2 1 := fun _ => 1
def oldMax : State 2 1 := fun q => if q = 0 then 0 else 1
theorem start_region : downset [startMax] = safe aut 1 := by
  funext f
  simp only [downset, List.any_cons, List.any_nil, Bool.or_false, below, safe, aut, ↓reduceIte, startMax]; rfl
theorem updated : downset [oldMax] = representativeUpdate 1 false plan.generators letter (downset [startMax]) := by
  unfold representativeUpdate close; rw [iterate_of_fixed (closePass plan.generators) _ (by rfl)]
  funext f; have h : ∀ x y : Fin 3, let f : State 2 1 := fun q => if q = 0 then x else y
      downset [oldMax] f =
        (downset [startMax] f && computeT aut 1 false (downset [startMax]) letter f) := by decide
  have he : f = (fun q => if q = 0 then f 0 else f 1) := by
    funext q; have hq : q = 0 ∨ q = 1 := by omega
    rcases hq with rfl | rfl <;> rfl
  conv => lhs; rw [he]
  conv => rhs; rw [he]
  exact h (f 0) (f 1)
theorem old_contains : Subset (final aut 1 false) (downset [oldMax]) := by
  rw [updated]
  exact selected_preserves 1 false [] letter _ (by rw [start_region]; exact stage_safe 1 false _)
-- A physical post-refutation region remains sound after lifting; equivariant_k_bounded_safety_aut.hh:963–976.
theorem raised : Subset (final aut 2 false) (downset ([oldMax].map (bump aut 1 1))) ∧
    Subset (downset ([oldMax].map (bump aut 1 1))) (safe aut 2) ∧
    Symmetric (A := aut) [] (downset ([oldMax].map (bump aut 1 1))) :=
  bound_raise_invariant 1 1 (by decide) false [] [oldMax] old_contains (by decide) (by simp [Symmetric])
theorem restored : downset ([oldMax].map (bump aut 1 1)) (initial aut 2) = true :=
  raised_initial 1 1 (by decide) (by decide) false [oldMax] old_contains
theorem reached : AdaptiveReach aut false plan.generators 1 (downset [startMax]) := by rw [start_region]; exact .start 1 (by decide)
theorem raised_reach : AdaptiveReach aut false plan.generators 2 (downset ([oldMax].map (bump aut 1 1))) :=
  .raise reached letter [oldMax] updated (by decide) 1 (by decide)
def table : List (Row 2 0) := precomputerTable (precomputerDiagram aut false) [letter]
theorem refines : Refines aut false (plan.orbits.map InputOrbit.representative) table :=
  precomputer_refinement aut false (precomputerDiagram aut false) (precomputer_denotes aut false) _
def selectedRow : Row 2 0 := table[0]'(by decide)
theorem physical_updated : downset [oldMax] = update aut 1 plan.generators [startMax] selectedRow := by
  obtain ⟨i, _, _, he⟩ := selected_refinement aut 1 (by decide) false plan.generators [startMax]
    (by decide) _ table refines selectedRow (by decide)
  have hi : i = letter := funext fun p => Fin.elim0 p
  rw [hi] at he; exact updated.trans he.symm
theorem stopping_branches : (sharedNext 1 2 1 = some 2 ∧
      AdaptiveReach aut false plan.generators 2 (downset ([oldMax].map (bump aut 1 1)))) ∧
    winner aut 1 false = false := by
  refine ⟨shared_loop_raise false plan [startMax] reached table refines selectedRow (by decide)
    [oldMax] physical_updated (by decide) 2 1 (by decide) (by decide), ?_⟩
  apply adaptive_stopped false plan [startMax] reached table refines selectedRow (by decide)
    ?_ 1 (by decide) (by decide)
  rw [← physical_updated]; decide
theorem completed :
    downset ([oldMax].map (bump aut 1 1)) = final aut 2 false ∧ winner aut 2 false = true := by
  apply adaptive_completed (by intro q r v _ h; contradiction) false plan _ raised_reach table refines; decide
theorem sweep_updated : downset [oldMax] = orbitUpdate aut 1 false
    ⟨letter, [Verified.identity aut]⟩ (downset [startMax]) := by
  rw [updated]; unfold representativeUpdate close
  rw [iterate_of_fixed (closePass plan.generators) _ (by rfl)]; rfl
theorem sweep_raised_reach : AdaptiveReach aut false plan.generators 2
    (downset ([oldMax].map (bump aut 1 1))) :=
  .orbit_raise reached plan rfl ⟨letter, [Verified.identity aut]⟩ (by exact List.mem_cons_self)
    [oldMax] sweep_updated (by decide) 1 (by decide)
theorem sweep_branches :
    (downset ([oldMax].map (bump aut 1 1)) = final aut 2 false ∧ winner aut 2 false = true) ∧
    winner aut 1 false = false := by
  refine ⟨adaptive_sweep_completed false plan _ sweep_raised_reach ?_, ?_⟩
  · funext f
    have he : ∀ f, sweep aut 2 false plan (downset ([oldMax].map (bump aut 1 1))) f =
        downset ([oldMax].map (bump aut 1 1)) f := by decide
    exact he f
  · apply adaptive_orbit_out false plan _ reached ⟨letter, [Verified.identity aut]⟩
    rw [← sweep_updated]; decide
-- An unchanged-rank warm start omits a new-bound winner; k_bounded_safety_aut.hh:220–238.
theorem no_lift_mutant :
    winner aut 2 false = true ∧ downset ([oldMax].map (embed 1 1)) (initial aut 2) = false :=
  ⟨completed.2, by decide⟩
def chain : Automaton 3 0 where
  init := 0
  rejecting := fun q => q != 0
  counting := fun _ => true
  isInput := fun p => Fin.elim0 p
  edges := [⟨0, .top, 1⟩, ⟨1, .top, 2⟩]
def chainMax : State 3 3 := fun q => ⟨q.val + 1, by have := q.isLt; omega⟩
theorem chain_wins : winner chain 3 false = true := by
  have hs : Subset (downset [chainMax]) (safe chain 3) := by
    apply downset_safe; intro m hm
    simp only [List.mem_singleton] at hm; subst m
    decide
  have hm : below (succ chain 3 chainMax letter) chainMax = true := by decide
  have hp : Subset (downset [chainMax]) (cpre chain 3 false (downset [chainMax])) := by
    intro f hf; apply (cpre_iff _ _ _ _ _).mpr
    refine ⟨hf, fun i => ⟨letter, ?_⟩⟩
    have hi : merge chain false i letter = letter := funext fun p => Fin.elim0 p
    rw [hi]; have hb : below f chainMax = true := by simpa [downset] using hf
    exact List.any_eq_true.mpr ⟨chainMax, by simp, below_trans (succ_mono 3 f chainMax hb letter) hm⟩
  exact final_greatest 3 false _ hs hp _ (by decide)
private theorem chain_next (f : State 3 2) (hf : final chain 2 false f = true) :
    final chain 2 false (succ chain 2 f letter) = true := by
  have hp : cpre chain 2 false (final chain 2 false) f = true := by rw [final_fixed]; exact hf
  obtain ⟨o, ho⟩ := ((cpre_iff _ _ _ _ _).mp hp).2 letter
  have he : merge chain false letter o = letter := funext fun p => Fin.elim0 p; simpa only [he] using ho
theorem chain_loses : winner chain 2 false = false := by
  cases hw : winner chain 2 false with
  | false => rfl
  | true =>
    have hh := chain_next _ (chain_next _ hw)
    have hs := stage_safe (A := chain) 2 false _ _ hh
    have hn : safe chain 2 (succ chain 2 (succ chain 2 (initial chain 2) letter) letter) = false := by decide
    rw [hn] at hs; contradiction
-- Shared overshoot wins at 3; clamped execution loses at 2; k_schedule.hh:8–14.
theorem schedule_verdict_mutant : sharedNext 1 2 2 = some 3 ∧ ordinaryNext 1 2 2 = some 2 ∧
    winner chain 3 false = true ∧ winner chain 2 false = false :=
  ⟨rfl, rfl, chain_wins, chain_loses⟩

end AdaptiveWitnesses

end Symmetry

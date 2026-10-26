import SymmetryProofs.AdaptiveK

namespace Symmetry.TableWitnesses
open ActionTable

def zero : Letter 2 := fun _ => false
def one : Letter 2 := fun _ => true
def mixed : Letter 2 := fun p => p = 0

/-- An unconditional edge keeps every input residual nonempty; unequal
    inputs add a second destination. Four paths have two pairs of equal lists. -/
def aut : Automaton 2 2 where
  init := 0
  rejecting := fun _ => false
  counting := fun _ => true
  isInput := fun _ => true
  edges := [⟨0, .top, 0⟩, ⟨0, .disj (.conj (.atom 0) (.neg (.atom 1)))
    (.conj (.neg (.atom 0)) (.atom 1)), 1⟩]

def reps : List (Letter 2) := [zero, one]

/-- Filtering keeps two distinct keys, then input-row dedup actually drops
    one. Its representative is still covered by the retained action class. -/
theorem input_row_dedup :
    zero ≠ one ∧ (grouped aut false).length = 4 ∧
    (restrict reps (grouped aut false)).length = 2 ∧
    (physical aut false reps).length = 1 ∧
    (∀ row ∈ physical aut false reps, row.1 one = false) ∧
    responses aut false zero = responses aut false one := by decide

/-- The input/output precomputer traversal exercises the same discarded key,
    including repeated residuals before the action-list quotient. -/
theorem precomputer_row_dedup :
    let D := precomputerDiagram aut false
    (precomputerRows D).length = 4 ∧
    (restrict reps (precomputerRows D)).length = 2 ∧
    (precomputerTable D reps).length = 1 ∧
    (∀ row ∈ precomputerTable D reps, row.1 one = false) := by decide

theorem quotient_covers : Refines aut false reps (physical aut false reps) :=
  physical_refinement aut false reps

theorem payload_equality :
    action aut zero = action aut one ∧
    Builders.responseAction aut zero = Builders.responseAction aut one ∧
    action aut zero ≠ action aut mixed := by decide

/-- Skipping an irrelevant input variable produces a genuine partial cube. -/
theorem grouped_partial_cube :
    let A := { aut with edges := [⟨0, .top, 0⟩, ⟨0, .atom 0, 1⟩] }
    (grouped A false).length = 2 ∧
    ∃ row ∈ grouped A false, row.1 zero = true ∧ row.1 (fun p => p = 1) = true := by decide

def plan : SweepPlan aut false where
  generators := []
  orbits := (letters 2).map fun i => ⟨i, [Verified.identity aut]⟩
  member_generated := by
    intro O ho s hs
    obtain ⟨i, _, rfl⟩ := List.mem_map.mp ho
    simp only [List.mem_singleton] at hs
    subst s
    exact .identity
  orbit_closed := by simp
  covers := by
    intro i
    refine ⟨⟨i, [Verified.identity aut]⟩, List.mem_map.mpr ⟨i, mem_letters i, rfl⟩, ?_⟩
    simp only [orbitLetters, List.map_cons, List.map_nil, List.mem_singleton]
    rfl

def maximumState : State 2 1 := fun _ => 1
def maxima : List (State 2 1) := [maximumState]
def table : List (Row 2 2) := physical aut false (plan.orbits.map InputOrbit.representative)

theorem region_safe : downset maxima = safe aut 1 := by
  funext f
  simp only [downset, maxima, List.any_cons, List.any_nil, Bool.or_false, below,
    maximumState, safe, aut, ↓reduceIte]
  rfl

theorem reached : PickerReach aut 1 false plan.generators (downset maxima) := by
  rw [region_safe]
  exact .start (by decide)

theorem closed : BooleanClosed aut := by intro q r v _ h; contradiction
theorem exhausted : (criticalCached (applyAction aut 1) maxima table).1 = none := by decide

/-- The physical table, including its row quotient, instantiates completion. -/
theorem completed : downset maxima = final aut 1 false ∧ winner aut 1 false = true :=
  physical_completed aut closed 1 (by decide) false plan maxima reached exhausted

theorem precomputer_success : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  apply precomputer_completed aut closed 1 (by decide) false plan maxima (picker_reach_invariants reached)
    (precomputerDiagram aut false) (precomputer_denotes aut false)
  decide

/-- The same construction instantiates the literal selected-update theorem. -/
theorem selected : ∀ row ∈ table,
    ∃ i ∈ plan.orbits.map InputOrbit.representative, row.1 i = true ∧
      update aut 1 plan.generators maxima row = representativeUpdate 1 false plan.generators i (downset maxima) := by
  intro row hr
  exact selected_refinement aut 1 (by decide) false plan.generators maxima
    (by decide) _ _ (physical_refinement aut false _) row hr

/-- Every retained physical input can take the continuing fixed-K branch. -/
theorem update_reachable : ∀ row ∈ table,
    Subset (final aut 1 false) (update aut 1 plan.generators maxima row) ∧
    PickerReach aut 1 false plan.generators (update aut 1 plan.generators maxima row) := by
  have hin : ∀ row ∈ table, update aut 1 plan.generators maxima row (initial aut 1) = true := by decide
  intro row hr
  exact physical_update_reach aut 1 (by decide) false plan maxima reached row hr (hin row hr)

def outputAut : Automaton 2 2 := { aut with isInput := fun p => p = 0 }

/-- Output traversal retains both distinct responses. Opposite input paths
    reverse their lists, so literal list dedup keeps both physical rows. -/
theorem output_paths :
    (physical outputAut false [zero, mixed]).length = 2 ∧
    (∀ row ∈ physical outputAut false [zero, mixed], row.2.length = 2) := by decide

/-- Full forward and backward response unions instantiate the class theorems
    on a graph with a real protagonist choice. -/
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

/-- Both cache forms retain the derived relation, including the next queue. -/
theorem cache_refines :
    Refines aut false (plan.orbits.map InputOrbit.representative) (criticalCached (applyAction aut 1) maxima table).2 ∧
    Refines aut false (plan.orbits.map InputOrbit.representative)
      ((criticalPQCached (applyAction aut 1) maxima queue).2.map Prod.snd) :=
  ⟨(physical_refinement aut false _).scan 1 maxima, queue_refines.priority 1 maxima queue⟩

theorem priority_completed : downset maxima = final aut 1 false ∧ winner aut 1 false = true := by
  apply refined_completed aut closed 1 (by decide) false plan maxima
    (picker_reach_invariants reached) (queue.map Prod.snd) queue_refines
  rw [criticalCached_none, criticalPicker_none]
  have he : (criticalPQCached (applyAction aut 1) maxima queue).1 = none := by
    rw [criticalPQCached_none, criticalPQ_none]
    have hh : queue.map Prod.snd = table := by simp [queue, List.map_map, Function.comp_def]
    rw [hh]
    exact (criticalPicker_none _ _ _).mp ((criticalCached_none _ _ _).mp exhausted)
  exact (criticalPQ_none _ _ _).mp ((criticalPQCached_none _ _ _).mp he)

def bad : Automaton 2 2 := { aut with rejecting := fun q => q = 1 }

/-- Mutant: discard a different action class as though its lists were equal.
    Exhaustion becomes a false fixed-point report; the omitted legal input
    has a strictly stronger backward constraint. This is not C++ deduplication. -/
theorem wrong_class_mutant :
    let rows := physical bad false [zero]
    (criticalCached (applyAction bad 1) maxima rows).1 = none ∧
    (∀ row ∈ rows, row.2 ≠ responses bad false mixed) ∧
    backwardT bad 1 false maxima mixed maximumState = false ∧
    cpre bad 1 false (downset maxima) maximumState = false := by decide

theorem mutant_not_refinement : ¬ Refines bad false [zero, mixed] (physical bad false [zero]) := by
  intro h
  obtain ⟨row, hr, hc⟩ := h.covers mixed (by simp)
  have ha := (hc (action bad mixed)).mpr ⟨zero, by decide⟩
  have hn : ∀ row ∈ physical bad false [zero], action bad mixed ∉ row.2 := by decide
  exact hn row hr ha

end Symmetry.TableWitnesses

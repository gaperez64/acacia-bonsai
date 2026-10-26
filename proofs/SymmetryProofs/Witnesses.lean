import SymmetryProofs.Solver
import SymmetryProofs.Reproducer331

namespace Symmetry.Witnesses

/-- Finite truth-table quantification, used only to kernel-check the witnesses. -/
def finiteForall (U : List α) (full : ∀ x, x ∈ U) (P : α → Prop)
    [DecidablePred P] : Decidable (∀ x, P x) :=
  decidable_of_iff (∀ x ∈ U, P x) ⟨fun h x => h x (full x), fun h x _ => h x⟩

instance (P : (Fin n → Bool) → Prop) [DecidablePred P] : Decidable (∀ f, P f) :=
  finiteForall (letters n) mem_letters P

instance (P : (Fin n → Fin m) → Prop) [DecidablePred P] : Decidable (∀ f, P f) :=
  finiteForall (vectors (List.finRange m) n) (mem_vectors _ List.mem_finRange) P

instance [DecidableEq β] : DecidableEq (Fin n → β) := fun f g =>
  decidable_of_iff (∀ i, f i = g i) ⟨funext, fun h i => congrFun h i⟩

/-- Two exchangeable clients. Client states are rejecting, so increments and
    overflow are exercised. APs are both engine inputs; the swapped frame lets
    the protagonist choose them. C++: symmetry.hh:9–25 and #331 witness obligation. -/
def aut : Automaton 3 2 where
  init := 0
  rejecting := fun q => q != 0
  counting := fun q => q != 0
  isInput := fun _ => true
  edges := [⟨0, .atom 0, 1⟩, ⟨0, .atom 1, 2⟩,
    ⟨1, .atom 0, 1⟩, ⟨2, .atom 1, 2⟩]

/-- C++: symmetry.hh:260–264. Nonidentity state and AP transpositions. -/
def candidate : Candidate 3 2 where
  phi := swap 1 2
  phiInv := swap 1 2
  pi := swap 0 1
  piInv := swap 0 1
  families := [(true, [0, 1])]

/-- C++: symmetry.hh:260–264. Identity is also a generated symmetry. -/
def identity : Candidate 3 2 where
  phi := fun q => q
  phiInv := fun q => q
  pi := fun p => p
  piInv := fun p => p
  families := [(true, [0, 1])]

/-- Coverage witness for accepted_sound and the guarded source correspondence. -/
theorem accepted : check aut candidate = true ∧ check aut identity = true ∧ Generated candidate := by
  unfold Generated
  decide

/-- Every detector hypothesis can be distinguished from the others by a small
    mutant: nonbijection, moved init, changed rejection, changed cap, missing edge,
    wrong labels, or crossing I/O. Soundness note:9–17 and #331. -/
theorem guard_mutants :
    check aut { candidate with phi := fun _ => 0 } = false ∧
    check { aut with init := 1 } candidate = false ∧
    check { aut with rejecting := fun q => q == 1 } candidate = false ∧
    check { aut with counting := fun q => q != 1 } candidate = false ∧
    check { aut with edges := aut.edges.take 3 } candidate = false ∧
    check aut { candidate with pi := fun p => p, piInv := fun p => p } = false ∧
    check { aut with isInput := fun p => p == 0 } candidate = false := by decide

/-- Two pure I/O families exercise a nonidentity permutation on BOTH sides. -/
def ioAut : Automaton 3 4 where
  init := 0
  rejecting := fun q => q != 0
  counting := fun q => q != 0
  isInput := fun p => p.val < 2
  edges := [⟨0, .conj (.atom 0) (.neg (.atom 2)), 1⟩,
    ⟨0, .conj (.atom 1) (.neg (.atom 3)), 2⟩,
    ⟨1, .neg (.atom 2), 1⟩, ⟨2, .neg (.atom 3), 2⟩]

/-- C++: symmetry.hh:260–264. Requests and grants move within their own families. -/
def ioCandidate : Candidate 3 4 where
  phi := swap 1 2
  phiInv := swap 1 2
  pi := fun p => swap 0 1 (swap 2 3 p)
  piInv := fun p => swap 0 1 (swap 2 3 p)
  families := [(true, [0, 1]), (false, [2, 3])]

/-- Positive guard witness with actual inputs AND outputs. -/
theorem ioAccepted : check ioAut ioCandidate = true ∧ Generated ioCandidate := by
  unfold Generated
  decide

/-- Checked nonidentity certificate, built from the executable detector. -/
def sym : Verified aut := accepted_sound aut candidate accepted.1

/-- Checked identity certificate. -/
def ident : Verified aut := accepted_sound aut identity accepted.2.1

/-- C++: equivariant:727–744. Three input count orbits: 00, 01/10, 11. -/
def orbit (i j : Bool) : InputOrbit aut where
  representative := fun p => if p = 0 then i else j
  members := [ident, sym]

/-- Every plan assumption below is kernel-checked on the nontrivial example. -/
def plan (side : Bool) : SweepPlan aut side where
  generators := [ident, sym]
  orbits := [orbit false false, orbit false true, orbit true true]
  member_generated := by
    intro O hO s hs
    simp only [List.mem_cons, List.not_mem_nil, or_false] at hO
    rcases hO with rfl | rfl | rfl <;> exact .generator s hs
  orbit_closed := by
    intro O hO s hs
    simp only [List.mem_cons, List.not_mem_nil, or_false] at hO hs
    rcases hO with rfl | rfl | rfl <;> rcases hs with rfl | rfl <;> cases side <;> decide
  covers := by
    have h : ∀ i : Fin 2 → Bool,
        opponentProjection aut side i ∈ orbitLetters side (orbit false false) ∨
          opponentProjection aut side i ∈ orbitLetters side (orbit false true) ∨
          opponentProjection aut side i ∈ orbitLetters side (orbit true true) := by cases side <;> decide
    intro i
    rcases h i with h | h | h
    · exact ⟨_, by simp, h⟩
    · exact ⟨_, by simp, h⟩
    · exact ⟨_, by simp, h⟩

/-- Asymmetric downward closed source exercises full CPre equivariance. -/
def region : Region 3 1 := fun f => safe aut 1 f && decide ((f 1).val = 0)

/-- Live state in a nontrivial state orbit. -/
def live : State 3 1 := fun q => if q = 1 then 1 else 0

/-- Witness for structural edge preservation, including its converse. -/
theorem edges : ∀ q r : Fin 3, ∀ v : Fin 2 → Bool,
    aut.enabled (sym.states q) (sym.states r) (act sym.aps v) = aut.enabled q r v := by decide

/-- Witness for rejecting increments, saturation, and the successor theorem. -/
theorem successor : ∀ f : State 3 1, ∀ v : Fin 2 → Bool,
    succ aut 1 (act sym.states f) (act sym.aps v) = act sym.states (succ aut 1 f v) := by decide

/-- Witness for safety, caps, and the fixed initial vector. -/
theorem safety : (∀ f : State 3 1, safe aut 1 (act sym.states f) = safe aut 1 f) ∧
    act sym.states (initial aut 1) = initial aut 1 := by decide

/-- Witness for CPre in BOTH frames on an asymmetric source. -/
theorem predecessor : ∀ side : Bool, ∀ f : State 3 1,
    cpre aut 1 side (image sym.states region) f = image sym.states (cpre aut 1 side region) f := by decide

/-- Witness for a nontrivial stage in both frames. -/
theorem stages : ∀ side : Bool, ∀ f : State 3 1,
    stage aut 1 side 1 (act sym.states f) = stage aut 1 side 1 f := by decide

/-- Witness for the actual backward scalar operation on all concrete bounds. -/
theorem backward_step : ∀ m : State 3 1, ∀ v : Fin 2 → Bool,
    backward aut 1 (act sym.states m) (act sym.aps v) = act sym.states (backward aut 1 m v) := by decide

/-- Witness for the preimage bridge with a safe maximal vector. -/
theorem backward_bridge : ∀ f : State 3 1, ∀ v : Fin 2 → Bool,
    below f (backward aut 1 (initial aut 1) v) = true ↔
      safe aut 1 f = true ∧ below (succ aut 1 f v) (initial aut 1) = true := by decide

/-- The apply-to-maxima union bridge is exercised on every safe/unsafe source. -/
theorem backward_union : ∀ side : Bool, ∀ i : Fin 2 → Bool, ∀ f : State 3 1,
    backwardT aut 1 side [initial aut 1] i f =
      (safe aut 1 f && computeT aut 1 side (downset [initial aut 1]) i f) := by decide

/-- Witness that the reused T is exactly the other client's T. -/
theorem reused : ∀ side : Bool, ∀ f : State 3 1,
    reuseT aut 1 side sym (safe aut 1) (fun p => p == 0) f =
      computeT aut 1 side (safe aut 1) (act sym.aps (fun p => p == 0)) f := by decide

/-- Witness that the literal snapshot sweep is a plain orbit intersection. -/
theorem orbit_update : ∀ side : Bool, ∀ f : State 3 1,
    orbitUpdate aut 1 side (orbit false true) (safe aut 1) f =
      plainOrbit aut 1 side (orbit false true) (safe aut 1) f := by decide

/-- Witness for generator intersection closure on an asymmetric region. -/
theorem closure_hypotheses : ∀ f : State 3 1,
    closePass [ident, sym] region (act sym.states f) = closePass [ident, sym] region f := by decide

/-- A nonempty safe postfixed region witnesses final_greatest's hypotheses. -/
def bottomOnly : Region 3 1 := fun f => decide (∀ q, (f q).val = 0)

theorem greatest_hypotheses : ∀ side : Bool,
    Subset bottomOnly (safe aut 1) ∧ Subset bottomOnly (cpre aut 1 side bottomOnly) := by
  unfold Subset
  decide

/-- Final-region invariance is instantiated on a genuine accepted state swap. -/
theorem final_region (side : Bool) : Invariant sym.states (final aut 1 side) :=
  final_invariant sym 1 side

/-- Concrete fixed-point and greatest-point obligations; all hypotheses above
    were proved by decide, without assumed certificates or empty regions. -/
theorem fixed (side : Bool) : cpre aut 1 side (final aut 1 side) = final aut 1 side :=
  final_fixed 1 side

theorem greatest (side : Bool) : Subset bottomOnly (final aut 1 side) :=
  final_greatest 1 side bottomOnly (greatest_hypotheses side).1 (greatest_hypotheses side).2

/-- Both concrete implementations instantiate the same final-set theorem. -/
theorem final_sets (side : Bool) : sweepFinal aut 1 side (plan side) = final aut 1 side ∧
    representativeFinal 1 side (plan side) = final aut 1 side :=
  ⟨sweepFinal_eq 1 side (plan side), representativeFinal_eq 1 side (plan side)⟩

/-- The concrete normal and swapped frames instantiate the verdict theorem. -/
theorem verdicts (side : Bool) :
    sweepFinal aut 1 side (plan side) (initial aut 1) = winner aut 1 side ∧
    representativeFinal 1 side (plan side) (initial aut 1) = winner aut 1 side :=
  verdicts_equal 1 side (plan side)

/-- Witness for the actual engine partition exchange, with both AP sides. -/
theorem engine_partition : ∀ i o : Fin 4 → Bool, ∀ p : Fin 4,
    merge (swapFrame ioAut) false i o p = merge ioAut true i o p := by decide

/-- Witness for bound widening on both Boolean and counting coordinates. -/
theorem bound_raise : ∀ m : State 3 1, ∀ q : Fin 3,
    bump aut 1 2 (act sym.states m) q = act sym.states (bump aut 1 2 m) q := by decide

/-- Maximal-element transport is exercised on an asymmetric downset. -/
theorem maxima_image : ∀ f : State 3 1,
    downset ([live].map (act sym.states)) f = image sym.states (downset [live]) f := by decide

/-- A nontrivial verdict witness: the normal frame loses at K=1 because the
    environment can request a rejecting client. -/
theorem normal_loses : winner aut 1 false = false := by
  apply Bool.eq_false_iff.mpr
  intro h
  have hf : cpre aut 1 false (final aut 1 false) (initial aut 1) = true := by
    rw [final_fixed]; exact h
  have hs := cpre_mono aut 1 false (stage_safe 1 false (states 3 1).length) _ hf
  have hl : cpre aut 1 false (safe aut 1) (initial aut 1) = false := by decide
  rw [hl] at hs
  contradiction

/-- The swapped frame wins: the protagonist can leave both request APs false. -/
theorem swapped_wins : winner aut 1 true = true := by
  let W : Region 3 1 := fun f => safe aut 1 f && decide ((f 1).val = 0 ∧ (f 2).val = 0)
  have hs : Subset W (safe aut 1) := by unfold Subset; decide
  have hp : Subset W (cpre aut 1 true W) := by unfold Subset; decide
  have hi : W (initial aut 1) = true := by decide
  exact final_greatest 1 true W hs hp _ hi

/-- Early stopping is instantiated with the same accepted nontrivial plan. -/
theorem executed (side : Bool) :
    runUntilInitialOut (initial aut 1) (sweep aut 1 side (plan side)) (safe aut 1) (states 3 1).length =
      winner aut 1 side ∧
    runUntilInitialOut (initial aut 1) (representativeRound 1 side (plan side)) (safe aut 1) (states 3 1).length =
      winner aut 1 side :=
  executed_verdicts_equal 1 side (plan side)

end Symmetry.Witnesses

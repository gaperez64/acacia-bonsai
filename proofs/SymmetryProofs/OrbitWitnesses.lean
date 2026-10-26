import SymmetryProofs.Witnesses

namespace Symmetry.OrbitWitnesses
open Witnesses (finiteForall)

/-- Three clients with requests 0..2 and grants 3..5. This is the smallest
    client count accepted by the C++ block-layout path (symmetry_blocks.hh:74–88).
    Like the two-client fixture, it has rejecting and Boolean coordinates. -/
def aut : Automaton 4 6 where
  init := 0
  rejecting := fun q => q != 0
  counting := fun q => q != 0
  isInput := fun p => p.val < 3
  edges := [⟨0, .conj (.atom 0) (.neg (.atom 3)), 1⟩,
    ⟨0, .conj (.atom 1) (.neg (.atom 4)), 2⟩,
    ⟨0, .conj (.atom 2) (.neg (.atom 5)), 3⟩,
    ⟨1, .neg (.atom 3), 1⟩, ⟨2, .neg (.atom 4), 2⟩, ⟨3, .neg (.atom 5), 3⟩]

/-- C++: symmetry_blocks.hh:36–48. One client-state block, initial state shared. -/
def stateLayout : BlockLayout 4 3 1 where
  locate q := if h : q.val > 0 then some (0, ⟨q.val - 1, by have := q.isLt; omega⟩) else none
  state _ j := ⟨j.val + 1, by have := j.isLt; omega⟩
  locate_state := by decide
  state_locate := by decide

/-- C++: symmetry.hh:260–264. Requests and grants are separate pure families,
    each with the same three client slots. -/
def apLayout : BlockLayout 6 3 2 where
  locate p := some ((if p.val < 3 then 0 else 1), ⟨p.val % 3, by omega⟩)
  state b j := ⟨b.val * 3 + j.val, by have := b.isLt; have := j.isLt; omega⟩
  locate_state := by decide
  state_locate := by decide

/-- C++: symmetry.hh:260–277,532–559. A star swap constructed from the layout. -/
def candidate (j : Fin 3) : Candidate 4 6 where
  phi := stateLayout.phiFromSigma (transposition 0 j)
  phiInv := (stateLayout.phiFromSigma (transposition 0 j)).invFun
  pi := apLayout.phiFromSigma (transposition 0 j)
  piInv := (apLayout.phiFromSigma (transposition 0 j)).invFun
  families := [(true, [0, 1, 2]), (false, [3, 4, 5])]

/-- Nontrivial accepted stars, with both I/O families passing the guard. -/
theorem accepted : check aut (candidate 1) = true ∧ check aut (candidate 2) = true := by decide

/-- C++: symmetry.hh:554–559. Only the two root swaps are stored. -/
def s01 : Verified aut := accepted_sound aut (candidate 1) accepted.1

/-- C++: symmetry.hh:554–559. Second stored root swap. -/
def s02 : Verified aut := accepted_sound aut (candidate 2) accepted.2

/-- C++: symmetry.hh:558. The original list, also passed unchanged to `close`. -/
def generators : List (Verified aut) := [s01, s02]

/-- C++: equivariant:734–736. This transport is absent from the stored list. -/
def s12 : Verified aut := s01.comp (s02.comp s01)

/-- The (1,2) transport identified in the review is generated and absent from
    the stored list. -/
theorem missing_transport : GeneratedBy generators s12.states s12.aps ∧ s12 ∉ generators := by
  constructor
  · exact .comp (.generator s01 (by simp [generators]))
      (.comp (.generator s02 (by simp [generators])) (.generator s01 (by simp [generators])))
  · intro h
    rcases List.mem_cons.mp h with h | h
    · have he := congrArg (fun s : Verified aut => s.states 1) h
      have hn : s12.states 1 ≠ s01.states 1 := by decide
      exact hn he
    · rcases List.mem_cons.mp h with h | h
      · have he := congrArg (fun s : Verified aut => s.states 2) h
        have hn : s12.states 2 ≠ s02.states 2 := by decide
        exact hn he
      · exact List.not_mem_nil h

/-- A kernel-checked nonempty invariant region exercises composition invariance. -/
theorem composition_invariant : ∀ f : State 4 1,
    safe aut 1 (act s12.states f) = safe aut 1 f := by decide

/-- The stored swaps satisfy the exact layout-agreement hypothesis. -/
theorem stars : ∀ j : Fin 3, j ≠ 0 → ∃ s ∈ generators,
    s.states = stateLayout.phiFromSigma (transposition 0 j) ∧
    s.aps = apLayout.phiFromSigma (transposition 0 j) := by
  intro j hj
  have h : j = 1 ∨ j = 2 := by omega
  rcases h with rfl | rfl
  · exact ⟨s01, by simp [generators], rfl, rfl⟩
  · exact ⟨s02, by simp [generators], rfl, rfl⟩

/-- The generation theorem covers every slot permutation, including match_slots
    results absent from the generator list; symmetry.hh:14–17, #331. -/
theorem layout_generated (sigma : Perm (Fin 3)) :
    GeneratedBy generators (stateLayout.phiFromSigma sigma) (apLayout.phiFromSigma sigma) :=
  phiFromSigma_generated stateLayout apLayout generators 0 stars sigma

/-- A checked witness that phi_from_sigma for (1,2) equals the composed
    transport used below, on both states and APs. -/
theorem layout_transport :
    (∀ q, s12.states q = stateLayout.phiFromSigma (transposition 1 2) q) ∧
    (∀ p, s12.aps p = apLayout.phiFromSigma (transposition 1 2) p) := by decide

/-- C++: equivariant:151–193. Assign only the engine-input family. In the
    swapped frame it is the original grant family; unused bits are false. -/
def representative (side i j k : Bool) : Fin 6 → Bool := fun p =>
  if aut.isInput p = side then false else
    if p.val % 3 = 0 then i else if p.val % 3 = 1 then j else k

/-- C++: equivariant:253–270,731–744. The sorted binary types 000,001,011,111.
    Members are in next_permutation order, with its stable match_slots maps:
    001→010 uses (1,2), 001→100 uses [1,2,0], 011→110 uses [2,0,1]. -/
def orbits (side : Bool) : List (InputOrbit aut) :=
  [⟨representative side false false false, [Verified.identity aut]⟩,
   ⟨representative side false false true, [Verified.identity aut, s12, s02.comp s01]⟩,
   ⟨representative side false true true, [Verified.identity aut, s01, s01.comp s02]⟩,
   ⟨representative side true true true, [Verified.identity aut]⟩]

/-- Every concrete orbit member is the layout-induced map of its slot
    permutation, on both states and APs. This proves the transport property
    used by `plan`; members need not occur in the stored generator list. -/
theorem member_layout (side : Bool) (O : InputOrbit aut) (ho : O ∈ orbits side)
    (s : Verified aut) (hs : s ∈ O.members) :
    ∃ sigma : Perm (Fin 3), s.states = stateLayout.phiFromSigma sigma ∧
      s.aps = apLayout.phiFromSigma sigma := by
  have hid : ∃ sigma : Perm (Fin 3),
      (Verified.identity aut).states = stateLayout.phiFromSigma sigma ∧
      (Verified.identity aut).aps = apLayout.phiFromSigma sigma :=
    ⟨Perm.id, (phiFromSigma_id stateLayout).symm, (phiFromSigma_id apLayout).symm⟩
  simp only [orbits, List.mem_cons, List.not_mem_nil, or_false] at ho
  rcases ho with rfl | rfl | rfl | rfl
  · simp only [List.mem_cons, List.not_mem_nil, or_false] at hs
    subst s; exact hid
  · simp only [List.mem_cons, List.not_mem_nil, or_false] at hs
    rcases hs with rfl | rfl | rfl
    · exact hid
    · exact ⟨transposition 1 2, Perm.ext layout_transport.1, Perm.ext layout_transport.2⟩
    · refine ⟨(transposition 0 2).comp (transposition 0 1), Perm.ext ?_, Perm.ext ?_⟩ <;> decide
  · simp only [List.mem_cons, List.not_mem_nil, or_false] at hs
    rcases hs with rfl | rfl | rfl
    · exact hid
    · exact ⟨transposition 0 1, rfl, rfl⟩
    · refine ⟨(transposition 0 1).comp (transposition 0 2), Perm.ext ?_, Perm.ext ?_⟩ <;> decide
  · simp only [List.mem_cons, List.not_mem_nil, or_false] at hs
    subst s; exact hid

/-- Every hypothesis of the final solver theorems has a concrete witness in
    both frames, using three clients, transports absent from the stored list,
    and input and output APs. Coverage is required only after opponentProjection,
    as #331 requires. -/
def plan (side : Bool) : SweepPlan aut side where
  generators := generators
  orbits := orbits side
  member_generated := by
    intro O hO s hs
    obtain ⟨sigma, hS, hI⟩ := member_layout side O hO s hs
    rw [hS, hI]
    exact layout_generated sigma
  orbit_closed := by
    intro O hO s hs
    simp only [orbits, List.mem_cons, List.not_mem_nil, or_false] at hO
    simp only [generators, List.mem_cons, List.not_mem_nil, or_false] at hs
    rcases hO with rfl | rfl | rfl | rfl <;> rcases hs with rfl | rfl <;> cases side <;> decide
  covers := by
    have h : ∀ i : Fin 6 → Bool, ∃ O ∈ orbits side,
        opponentProjection aut side i ∈ orbitLetters side O := by cases side <;> decide
    exact h

/-- Every representative leaves protagonist APs false, so coverage requires
    no extra output assignments. Each frame has APs on both sides. -/
theorem input_only : ∀ side : Bool,
    (∀ O ∈ orbits side, ∀ p, aut.isInput p = side → O.representative p = false) ∧
    (∃ p, aut.isInput p = side) ∧ (∃ p, aut.isInput p ≠ side) := by decide

/-- The final-set theorems use this plan with the original two-star list for
    generator closure. symmetry.hh:14–17, #331. -/
theorem final_sets (side : Bool) :
    sweepFinal aut 1 side (plan side) = final aut 1 side ∧
    representativeFinal 1 side (plan side) = final aut 1 side :=
  ⟨sweepFinal_eq 1 side (plan side), representativeFinal_eq 1 side (plan side)⟩

/-- Both verdict corollaries use engine-input representatives and the stored
    star generator list, in either frame. symmetry.hh:14–17, #331. -/
theorem verdicts (side : Bool) :
    sweepFinal aut 1 side (plan side) (initial aut 1) = winner aut 1 side ∧
    representativeFinal 1 side (plan side) (initial aut 1) = winner aut 1 side :=
  verdicts_equal 1 side (plan side)

theorem executed (side : Bool) :
    runUntilInitialOut (initial aut 1) (sweep aut 1 side (plan side)) (safe aut 1) (states 4 1).length =
      winner aut 1 side ∧
    runUntilInitialOut (initial aut 1) (representativeRound 1 side (plan side)) (safe aut 1) (states 4 1).length =
      winner aut 1 side :=
  executed_verdicts_equal 1 side (plan side)

end Symmetry.OrbitWitnesses

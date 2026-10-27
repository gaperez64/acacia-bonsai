import SymmetryProofs.AdaptiveK

namespace Symmetry.OrbitWitnesses
-- Three clients and two blocks meet the production gate; symmetry_blocks.hh:74–88.
def aut : Automaton 7 6 where
  init := 0
  rejecting := fun q => q.val > 0 && q.val < 4
  counting := fun q => q.val > 0 && q.val < 4
  isInput := fun p => p.val < 3
  edges := [⟨0, .conj (.atom 0) (.neg (.atom 3)), 1⟩, ⟨0, .conj (.atom 1) (.neg (.atom 4)), 2⟩,
    ⟨0, .conj (.atom 2) (.neg (.atom 5)), 3⟩,
    ⟨1, .neg (.atom 3), 1⟩, ⟨2, .neg (.atom 4), 2⟩, ⟨3, .neg (.atom 5), 3⟩,
    ⟨4, .top, 4⟩, ⟨5, .top, 5⟩, ⟨6, .top, 6⟩]
def stateLayout : BlockLayout 7 3 2 where
  locate q := if h : q.val > 0 then some
    (⟨(q.val - 1) / 3, by have := q.isLt; omega⟩, ⟨(q.val - 1) % 3, by omega⟩) else none
  state b j := ⟨b.val * 3 + j.val + 1, by have := b.isLt; have := j.isLt; omega⟩
  locate_state := by decide
  state_locate := by decide
def apLayout : BlockLayout 6 3 2 where
  locate p := some ((if p.val < 3 then 0 else 1), ⟨p.val % 3, by omega⟩)
  state b j := ⟨b.val * 3 + j.val, by have := b.isLt; have := j.isLt; omega⟩
  locate_state := by decide
  state_locate := by decide
def candidate (j : Fin 3) : Candidate 7 6 where
  phi := stateLayout.phiFromSigma (transposition 0 j)
  phiInv := (stateLayout.phiFromSigma (transposition 0 j)).invFun
  pi := apLayout.phiFromSigma (transposition 0 j)
  piInv := (apLayout.phiFromSigma (transposition 0 j)).invFun
  families := [(true, [0, 1, 2]), (false, [3, 4, 5])]
theorem accepted : check aut (candidate 1) = true ∧ check aut (candidate 2) = true := by decide
def s01 : Verified aut := accepted_sound aut (candidate 1) accepted.1
def s02 : Verified aut := accepted_sound aut (candidate 2) accepted.2
def generators : List (Verified aut) := [s01, s02]
def s12 : Verified aut := s01.comp (s02.comp s01)
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
theorem stars : ∀ j : Fin 3, j ≠ 0 → ∃ s ∈ generators,
    s.states = stateLayout.phiFromSigma (transposition 0 j) ∧
    s.aps = apLayout.phiFromSigma (transposition 0 j) := by
  intro j hj; have h : j = 1 ∨ j = 2 := by omega
  rcases h with rfl | rfl
  · exact ⟨s01, by simp [generators], rfl, rfl⟩
  · exact ⟨s02, by simp [generators], rfl, rfl⟩
theorem layout_generated (sigma : Perm (Fin 3)) :
    GeneratedBy generators (stateLayout.phiFromSigma sigma) (apLayout.phiFromSigma sigma) :=
  phiFromSigma_generated stateLayout apLayout generators 0 stars sigma

theorem edges (x : EdgeKey 7 6) : keyPerm s01 x ∈ aut.edges.map edgeKey ↔ x ∈ aut.edges.map edgeKey :=
  edges_both_directions s01 x
theorem equivariance (K : Nat) (side : Bool) (X : Region 7 K) (f : State 7 K) (v : Fin 6 → Bool) :
    succ aut K (act s01.states f) (act s01.aps v) = act s01.states (succ aut K f v) ∧
    safe aut K (act s01.states f) = safe aut K f ∧
    cpre aut K side (image s01.states X) = image s01.states (cpre aut K side X) :=
  ⟨succ_commutes s01 K f v, safe_commutes s01 K f, cpre_commutes s01 K side X⟩
theorem stages (K k : Nat) (side : Bool) :
    Invariant s01.states (stage aut K side k) ∧ Invariant s01.states (final aut K side) ∧
    final aut K side (act s01.states (initial aut K)) = winner aut K side :=
  ⟨stage_invariant s01 K side k, final_invariant s01 K side, winner_invariant s01 K side⟩
theorem backward_bridge (f : State 7 1) (v : Fin 6 → Bool) :
    below f (backward aut 1 (initial aut 1) v) = true ↔
      safe aut 1 f = true ∧ below (succ aut 1 f v) (initial aut 1) = true :=
  backward_preimage 1 (by decide) f _ (initial_safe 1 (by decide)) v
theorem reused (side : Bool) (i : Fin 6 → Bool) :
    reuseT aut 1 side s01 (safe aut 1) i = computeT aut 1 side (safe aut 1) (act s01.aps i) :=
  reuse_exact s01 1 side _ (safe_commutes s01 1) i
theorem guard_mutants :
    check aut { candidate 1 with phi := fun _ => 0 } = false ∧
    check { aut with init := 1 } (candidate 1) = false ∧
    check { aut with rejecting := fun q => q == 1 } (candidate 1) = false ∧
    check { aut with counting := fun q => q != 1 } (candidate 1) = false ∧
    check { aut with edges := aut.edges.take 7 } (candidate 1) = false ∧
    check aut { candidate 1 with pi := fun p => p, piInv := fun p => p } = false ∧
    check { aut with isInput := fun p => p == 0 } (candidate 1) = false := by decide

theorem initial_vector (K : Nat) : act s01.states (initial aut K) = initial aut K := initial_fixed s01 K
theorem fixed (K : Nat) (side : Bool) : cpre aut K side (final aut K side) = final aut K side := final_fixed K side
theorem greatest (Y : Region 7 1) (side : Bool)
    (hs : Subset Y (safe aut 1)) (hp : Subset Y (cpre aut 1 side Y)) : Subset Y (final aut 1 side) :=
  final_greatest 1 side Y hs hp
theorem backward_step (m : State 7 1) (v : Fin 6 → Bool) :
    backward aut 1 (act s01.states m) (act s01.aps v) = act s01.states (backward aut 1 m v) :=
  backward_commutes s01 1 m v
theorem maxima_image (ms : List (State 7 1)) :
    downset (ms.map (act s01.states)) = image s01.states (downset ms) := image_downset s01.states ms
theorem nonuniform_mutant :
    ¬ Invariant s01.states (downset ([fun q => if q = 1 then 2 else 1] : List (State 7 2))) := by
  intro h
  have he := h (fun q => if q = 1 then 2 else 1)
  have hn : downset ([fun q => if q = 1 then 2 else 1] : List (State 7 2))
      (act s01.states (fun q => if q = 1 then 2 else 1)) ≠ true := by decide
  exact hn he

end Symmetry.OrbitWitnesses

namespace Symmetry.BuilderWitnesses
open Builders OrbitWitnesses
def rawFamilies : FamilyTable 6 3 where
  rows := [(true, apLayout.state 0), (false, apLayout.state 1)]
  unique := by decide
theorem families_accept : (buildFamilies aut rawFamilies).isSome = true := by decide
def checkedFamilies := (buildFamilies aut rawFamilies).get families_accept
theorem stars_checked : ∀ j : Fin 3, j ≠ 0 →
    check aut (sourceCandidate checkedFamilies.2 0 j (candidate j)) = true := by
  intro j hj; have he : j = 1 ∨ j = 2 := by omega
  rcases he with rfl | rfl <;> decide
def checkedStars : SourceStars aut 3 checkedFamilies.1 where
  families := checkedFamilies.2
  pivot := 0
  star j := if hj : j ≠ 0 then
    accepted_sound aut (sourceCandidate checkedFamilies.2 0 j (candidate j)) (stars_checked j hj)
    else Verified.identity aut
  ap_star := by
    intro j hj; rw [dite_eq_left hj]
    apply Perm.ext; intro p
    rfl
theorem checkedStars_result : buildSourceStars checkedFamilies.2 0 candidate = some checkedStars := by
  unfold buildSourceStars; rw [dite_eq_left stars_checked]
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
-- Freeze the checked root table for later lookups; symmetry_blocks.hh:128–187.
def recognized : AlignedLayout 7 3 := by
  have h := layout_accepts; simp only [computeBlockLayout, recognizeLayout, roots] at h
  exact Option.get _ h
theorem recognized_result :
    computeBlockLayout (checkedStars.generators.map Verified.states) checkedStars.indexSignature = some recognized := by
  have h := layout_accepts; simp only [computeBlockLayout, recognizeLayout, roots] at h ⊢
  exact (Option.some_get h).symm
theorem recognized_eq : recognized = (⟨2, stateLayout, Perm.id⟩ : AlignedLayout 7 3) := by
  unfold recognized; change (⟨2, _, _⟩ : AlignedLayout 7 3) = _
  congr 1
  · apply layout_ext
    decide
  · apply Perm.ext
    decide
-- Keep executable fields explicit to avoid repeated detector reduction; symmetry_blocks.hh:274–302.
def aligned : LayoutGroup aut 3 2 checkedFamilies.1 where
  states := stateLayout
  families := checkedFamilies.2
  pivot := 0
  star := checkedStars.star
  ap_star := checkedStars.ap_star
  state_star := by
    intro j hj; apply Perm.ext
    intro q; change (if h : j ≠ 0 then accepted_sound aut
      (sourceCandidate checkedFamilies.2 0 j (candidate j)) (stars_checked j h)
      else Verified.identity aut).states q = _
    rw [dite_eq_left hj]; rfl
theorem aligned_result :
    alignGroup checkedStars (⟨2, stateLayout, Perm.id⟩ : AlignedLayout 7 3) = some aligned := by
  unfold alignGroup checkLayoutGroup; dsimp only
  have h : ∀ j : Fin 3, j ≠ checkedStars.pivot → ∀ q,
      (checkedStars.star j).states q = stateLayout.phiFromSigma (transposition checkedStars.pivot j) q :=
    fun j hj q => congrArg (fun p : Perm (Fin 7) => p q) (aligned.state_star j hj)
  split
  · apply congrArg some
    unfold aligned; congr 1
    congr 1
    exact reindexLayout_id _
  · next hn => exact False.elim (hn h)
theorem caps_accept (side : Bool) : (buildPlan aligned side 3).isSome = true := by cases side <;> decide
def produced (side : Bool) : SweepPlan aut side := builtPlan aligned side
-- Successful stages compose into a raw build in either frame; equivariant_k_bounded_safety_aut.hh:800–869.
theorem builder_result (side : Bool) :
    buildReduction aut rawFamilies 0 candidate side 3 = some (produced side) :=
  reduction_of_stages aut rawFamilies 0 candidate side 3
    checkedFamilies checkedStars ⟨2, stateLayout, Perm.id⟩ aligned
    (Option.some_get families_accept).symm checkedStars_result
    (recognized_result.trans (congrArg some recognized_eq)) aligned_result (caps_accept side)
theorem original_generators (side : Bool) : (produced side).generators = checkedStars.generators :=
  alignGroup_generators checkedStars ⟨2, stateLayout, Perm.id⟩ aligned_result
theorem final_sets (side : Bool) :
    sweepFinal aut 1 side (produced side) = final aut 1 side ∧
      representativeFinal 1 side (produced side) = final aut 1 side := by
  have he := reduction_final_eq aut rawFamilies 0 candidate side 3 1
    (builder_result side)
  exact ⟨he.1, he.2.1⟩
theorem verdicts (side : Bool) :
    sweepFinal aut 1 side (produced side) (initial aut 1) = winner aut 1 side ∧
    representativeFinal 1 side (produced side) (initial aut 1) = winner aut 1 side :=
  verdicts_equal 1 side (produced side)
theorem executed (side : Bool) :
    runUntilInitialOut (initial aut 1) (sweep aut 1 side (produced side)) (safe aut 1) (states 7 1).length =
      winner aut 1 side ∧
    runUntilInitialOut (initial aut 1) (representativeRound 1 side (produced side)) (safe aut 1) (states 7 1).length =
      winner aut 1 side := executed_verdicts_equal 1 side (produced side)
theorem orbit_update (side : Bool) (O : InputOrbit aut) :
    orbitUpdate aut 1 side O (safe aut 1) = plainOrbit aut 1 side O (safe aut 1) :=
  orbitUpdate_exact 1 side O _ (fun t _ => safe_commutes t 1)
theorem closure (side : Bool) : Symmetric (produced side).generators (close (produced side).generators (safe aut 1)) :=
  close_invariant _ _
theorem enumeration : typeCounts 2 3 = [[0, 3], [1, 2], [2, 1], [3, 0]] ∧
    permutations [0, 0, 1] = [[0, 0, 1], [0, 1, 0], [1, 0, 0]] ∧ (permutations [0, 1, 2]).length = 6 ∧
    takeType 1 [(0, 0), (1, 1), (2, 1)] = some ((1, 1), [(0, 0), (2, 1)]) := by decide
theorem enumeration_mutants : [0, 0, 1] ∉ permutations [1, 0, 0] ∧
    matchEntries [0, 1] [(0, 0), (1, 0)] = none := by decide
theorem layout_mutants :
    (computeBlockLayout ([] : List (Perm (Fin 7))) (fun _ : Fin 3 => 0)).isNone = true ∧
    (checkLayoutGroup (reindexLayout stateLayout (transposition 1 2))
      checkedStars.families checkedStars.pivot checkedStars.star checkedStars.ap_star).isNone = true := by decide
theorem responses (side : Bool) (i : Fin 6 → Bool) (o : Fin 6 → Bool) :
    responseAction aut (merge aut side i o) ∈
      responseActions aut side i :=
  (responseActions_complete _ _ _ _).mpr ⟨o, rfl⟩

end Symmetry.BuilderWitnesses

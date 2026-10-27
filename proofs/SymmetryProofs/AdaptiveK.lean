import SymmetryProofs.ActionTable
import SymmetryProofs.Closure

namespace Symmetry
variable {A : Automaton n a}
-- Floor shifted coordinates after subtracting the actual K increase; k_bounded_safety_aut.hh:220–238.
def lower (K delta : Nat) (f : State n (K + delta)) : State n K :=
  fun q => ⟨(f q).val - delta, by have := (f q).isLt; omega⟩
theorem lower_commutes (p : Perm (Fin n)) (K delta : Nat) (f : State n (K + delta)) :
    lower K delta (act p f) = act p (lower K delta f) := rfl
theorem lower_safe (K delta : Nat) (f : State n (K + delta))
    (hf : safe A (K + delta) f = true) : safe A K (lower K delta f) = true := by
  apply decide_eq_true; intro q
  have h := (of_decide_eq_true hf) q
  change (f q).val - delta ≤ if A.counting q then K else 1
  cases hc : A.counting q <;> simp only [hc, Bool.false_eq_true, ↓reduceIte] at h ⊢ <;> omega
theorem lower_succ (K delta : Nat) (f : State n (K + delta)) (v : Fin a → Bool) :
    below (succ A K (lower K delta f) v) (lower K delta (succ A (K + delta) f v)) = true := by
  apply decide_eq_true; intro r -- Lowering a new successor dominates the old-bound successor; k_bounded_safety_aut.hh:220–238.
  change maxStates (fun q => contribution A K (lower K delta f) v q r) ≤
    maxStates (fun q => contribution A (K + delta) f v q r) - delta
  apply (maxStates_le _ _).mpr; intro q
  have h := le_maxStates (fun q => contribution A (K + delta) f v q r) q
  by_cases he : A.enabled q r v = true
  · by_cases hx : (f q).val > delta
    · have hp : (f q).val > 0 := by omega
      have hl : (lower K delta f q).val > 0 := by change (f q).val - delta > 0; omega
      change (f q).val - delta > 0 at hl
      simp only [contribution, lower, Fin.val_mk, he, Bool.true_and, decide_eq_true_eq,
        hp, hl, ↓reduceIte] at h ⊢
      omega
    · have hl : ¬ (lower K delta f q).val > 0 := by change ¬ (f q).val - delta > 0; omega
      simp [contribution, hl]
  · simp [contribution, Bool.eq_false_iff.mpr he]
-- New-bound winners lower into the old winning region; k_bounded_safety_aut.hh:220–238.
theorem final_lower (K delta : Nat) (side : Bool) (f : State n (K + delta))
    (hf : final A (K + delta) side f = true) : final A K side (lower K delta f) = true := by
  have go : ∀ j, ∀ f : State n (K + delta), final A (K + delta) side f = true →
      ∀ g : State n K, below g (lower K delta f) = true → stage A K side j g = true := by
    intro j; induction j with
    | zero =>
      intro f hf g hg; have hs := lower_safe K delta f (stage_safe (K + delta) side _ f hf)
      apply decide_eq_true; intro q
      exact Nat.le_trans ((of_decide_eq_true hg) q) ((of_decide_eq_true hs) q)
    | succ j ih =>
      intro f hf g hg; change cpre A K side (stage A K side j) g = true
      apply (cpre_iff _ _ _ _ _).mpr; refine ⟨ih f hf g hg, ?_⟩
      intro i; have hp : cpre A (K + delta) side (final A (K + delta) side) f = true := by
        rw [final_fixed]; exact hf
      obtain ⟨o, ho⟩ := ((cpre_iff _ _ _ _ _).mp hp).2 i
      refine ⟨o, ih _ ho _ ?_⟩
      exact below_trans (succ_mono K g (lower K delta f) hg _) (lower_succ K delta f _)
  exact go _ f hf _ (decide_eq_true (fun _ => Nat.le_refl _))
theorem bump_safe (K delta : Nat) (m : State n K) (hm : safe A K m = true) :
    safe A (K + delta) (bump A K delta m) = true := by
  apply decide_eq_true; intro q
  have h := (of_decide_eq_true hm) q
  cases hc : A.counting q <;> simp [bump, hc] at h ⊢ <;> omega
theorem raised_membership (K delta : Nat) (positive : 1 ≤ delta)
    (maxima : List (State n K)) (f : State n (K + delta))
    (hf : safe A (K + delta) f = true) :
    downset (maxima.map (bump A K delta)) f = downset maxima (lower K delta f) := by
  have he : ∀ m : State n K, below f (bump A K delta m) = below (lower K delta f) m := by
    intro m; apply Bool.eq_iff_iff.mpr
    simp only [below, decide_eq_true_eq]
    apply forall_congr'; intro q
    have hs := (of_decide_eq_true hf) q
    cases hc : A.counting q <;> simp [bump, lower, hc] at hs ⊢ <;> omega
  simp only [downset, List.any_map, Function.comp_def, he]
-- Uniform lift preserves new-bound winners, safety and symmetry; equivariant_k_bounded_safety_aut.hh:963–976.
theorem bound_raise_invariant (K delta : Nat) (positive : 1 ≤ delta) (side : Bool)
    (G : List (Verified A)) (maxima : List (State n K))
    (contains : Subset (final A K side) (downset maxima))
    (safeMax : ∀ m ∈ maxima, safe A K m = true)
    (symmetric : Symmetric G (downset maxima)) :
    Subset (final A (K + delta) side) (downset (maxima.map (bump A K delta))) ∧
    Subset (downset (maxima.map (bump A K delta))) (safe A (K + delta)) ∧
    Symmetric G (downset (maxima.map (bump A K delta))) := by
  have hs : Subset (downset (maxima.map (bump A K delta))) (safe A (K + delta)) := by
    apply downset_safe; intro m hm
    obtain ⟨old, ho, rfl⟩ := List.mem_map.mp hm; exact bump_safe K delta old (safeMax old ho)
  refine ⟨?_, hs, ?_⟩
  · intro f hf
    rw [raised_membership K delta positive maxima f (stage_safe _ side _ f hf)]
    exact contains _ (final_lower K delta side f hf)
  · intro s hmem f
    apply Bool.eq_iff_iff.mpr; constructor
    · intro hf
      have hsf := hs _ hf
      have hsafe : safe A (K + delta) f = true := by
        rw [← safe_commutes s (K + delta) f]; exact hsf
      rw [raised_membership K delta positive maxima _ hsafe]
      rw [raised_membership K delta positive maxima _ hsf, lower_commutes, symmetric s hmem] at hf
      exact hf
    · intro hf
      have hsf := hs _ hf
      have hsafe : safe A (K + delta) (act s.states f) = true := by
        rw [safe_commutes]; exact hsf
      rw [raised_membership K delta positive maxima _ hsafe, lower_commutes, symmetric s hmem]
      rw [raised_membership K delta positive maxima _ hsf] at hf; exact hf
def absent (n K : Nat) : State n K := fun _ => ⟨0, by omega⟩
theorem succ_absent (K : Nat) (v : Fin a → Bool) : succ A K (absent n K) v = absent n K := by
  funext r; apply Fin.ext
  apply Nat.eq_zero_of_le_zero; exact (maxStates_le _ 0).mpr (fun q => by simp [contribution, absent])
theorem absent_final (K : Nat) (side : Bool) : final A K side (absent n K) = true := by
  have go : ∀ j, stage A K side j (absent n K) = true := by
    intro j; induction j with
    | zero => exact decide_eq_true (fun q => by simp [absent])
    | succ j ih =>
      apply (cpre_iff _ _ _ _ _).mpr; exact ⟨ih, fun _ => ⟨fun _ => false, by rw [succ_absent]; exact ih⟩⟩
  exact go _
-- Every positive actual lift restores init before the next call; equivariant_k_bounded_safety_aut.hh:963–976.
theorem raised_initial (K delta : Nat) (positiveK : 1 ≤ K) (positive : 1 ≤ delta)
    (side : Bool) (maxima : List (State n K))
    (contains : Subset (final A K side) (downset maxima)) :
    downset (maxima.map (bump A K delta)) (initial A (K + delta)) = true := by
  rw [raised_membership K delta positive maxima _ (initial_safe _ (by omega))]
  have he : lower K delta (initial A (K + delta)) = absent n K := by
    funext q; apply Fin.ext
    simp only [lower, initial, absent]
    split <;> simp <;> omega
  rw [he]; exact contains _ (absent_final K side)
def embed (K delta : Nat) (f : State n K) : State n (K + delta) :=
  fun q => ⟨(f q).val, by have := (f q).isLt; omega⟩
theorem embed_safe (K delta : Nat) (f : State n K) (hf : safe A K f = true) :
    safe A (K + delta) (embed K delta f) = true := by
  apply decide_eq_true; intro q
  have h := (of_decide_eq_true hf) q
  cases hc : A.counting q <;> simp [embed, hc] at h ⊢ <;> omega
theorem succ_embed (K delta : Nat) (positive : 1 ≤ K) (f : State n K) (v : Fin a → Bool)
    (hs : safe A K (succ A K f v) = true) :
    succ A (K + delta) (embed K delta f) v = embed K delta (succ A K f v) := by
  funext r; apply Fin.ext
  have hr : (succ A K f v r).val ≤ K := by
    have h := (of_decide_eq_true hs) r
    cases hc : A.counting r <;> simp [hc] at h <;> omega
  change maxStates (fun q => contribution A (K + delta) (embed K delta f) v q r) =
    maxStates (fun q => contribution A K f v q r)
  congr 1
  funext q; have hq := Nat.le_trans (le_maxStates (fun q => contribution A K f v q r) q) hr
  change (if A.enabled q r v && decide ((f q).val > 0) then
    min (K + delta + 1) ((f q).val + if A.rejecting r then 1 else 0) else 0) =
    (if A.enabled q r v && decide ((f q).val > 0) then
    min (K + 1) ((f q).val + if A.rejecting r then 1 else 0) else 0)
  unfold contribution at hq; by_cases he : (A.enabled q r v && decide ((f q).val > 0)) = true
  · simp only [he, ↓reduceIte] at hq ⊢
    omega
  · simp [he]
theorem final_embed (K delta : Nat) (positive : 1 ≤ K) (side : Bool) (f : State n K)
    (hf : final A K side f = true) : final A (K + delta) side (embed K delta f) = true := by
  have go : ∀ j, ∀ f, final A K side f = true → stage A (K + delta) side j (embed K delta f) = true := by
    intro j; induction j with
    | zero => intro f hf; exact embed_safe K delta f (stage_safe K side _ f hf)
    | succ j ih =>
      intro f hf; apply (cpre_iff _ _ _ _ _).mpr
      refine ⟨ih f hf, ?_⟩
      intro i; have hp : cpre A K side (final A K side) f = true := by rw [final_fixed]; exact hf
      obtain ⟨o, ho⟩ := ((cpre_iff _ _ _ _ _).mp hp).2 i
      refine ⟨o, ?_⟩
      rw [succ_embed K delta positive f _ (stage_safe K side _ _ ho)]; exact ih _ ho
  exact go _ f hf
-- Winners persist when the bound grows; k_bounded_safety_aut.hh:190–238.
theorem winner_mono (K delta : Nat) (positive : 1 ≤ K) (side : Bool)
    (hw : winner A K side = true) : winner A (K + delta) side = true := by
  have he : embed K delta (initial A K) = initial A (K + delta) := by
    funext q; apply Fin.ext
    simp only [embed, initial]
    split <;> rfl
  change final A (K + delta) side (initial A (K + delta)) = true
  rw [← he]; exact final_embed K delta positive side _ hw
-- Picker/orbit histories include post-update raises and initial-out tests; equivariant_k_bounded_safety_aut.hh:751–760,951–976.
inductive AdaptiveReach (A : Automaton n a) (side : Bool) (G : List (Verified A)) :
    (K : Nat) → Region n K → Prop
  | start (K : Nat) (positive : 1 ≤ K) : AdaptiveReach A side G K (safe A K)
  | update {K X} (prior : AdaptiveReach A side G K X) (i : Fin a → Bool)
      (initial_in : representativeUpdate K side G i X (initial A K) = true) :
      AdaptiveReach A side G K (representativeUpdate K side G i X)
  | raise {K X} (prior : AdaptiveReach A side G K X) (i : Fin a → Bool)
      (maxima : List (State n K))
      (updated : downset maxima = representativeUpdate K side G i X)
      (initial_out : downset maxima (initial A K) = false) (delta : Nat) (positive : 1 ≤ delta) :
      AdaptiveReach A side G (K + delta) (downset (maxima.map (bump A K delta)))
  | orbit {K X} (prior : AdaptiveReach A side G K X) (P : SweepPlan A side)
      (generators : P.generators = G) (O : InputOrbit A) (member : O ∈ P.orbits)
      (initial_in : orbitUpdate A K side O X (initial A K) = true) :
      AdaptiveReach A side G K (orbitUpdate A K side O X)
  | orbit_raise {K X} (prior : AdaptiveReach A side G K X) (P : SweepPlan A side)
      (generators : P.generators = G) (O : InputOrbit A) (member : O ∈ P.orbits)
      (maxima : List (State n K)) (updated : downset maxima = orbitUpdate A K side O X)
      (initial_out : downset maxima (initial A K) = false) (delta : Nat) (positive : 1 ≤ delta) :
      AdaptiveReach A side G (K + delta) (downset (maxima.map (bump A K delta)))
theorem adaptive_invariants (h : AdaptiveReach A side G K X) :
    1 ≤ K ∧ Subset (final A K side) X ∧ Subset X (safe A K) ∧ Symmetric G X ∧ X (initial A K) = true := by
  induction h with -- All continuing calls retain the completion invariants; equivariant_k_bounded_safety_aut.hh:951–976.
  | start K positive => exact ⟨positive, picker_reach_invariants (.start positive)⟩
  | update _ i hin ih =>
    refine ⟨ih.1, selected_preserves _ _ _ i _ ih.2.1, ?_, close_invariant G _, hin⟩
    exact fun f hf => ih.2.2.1 f (representativeUpdate_shrinks _ _ _ i _ f hf)
  | @raise K X _ i maxima updated _ delta positive ih =>
    have contains : Subset (final A K side) (downset maxima) := by
      rw [updated]; exact selected_preserves K side G i X ih.2.1
    have hm := maxima_safe (A := A) (maxima := maxima) (by
      rw [updated]; exact fun f hf => ih.2.2.1 f (representativeUpdate_shrinks K side G i X f hf))
    have sym : Symmetric G (downset maxima) := by rw [updated]; exact close_invariant G _
    obtain ⟨hc, hs, hy⟩ := bound_raise_invariant K delta positive side G maxima contains hm sym
    exact ⟨by omega, hc, hs, hy, raised_initial K delta ih.1 positive side maxima contains⟩
  | orbit _ P generators O member hin ih =>
    refine ⟨ih.1, orbit_preserves_winner _ _ O _ ih.2.1, ?_, ?_, hin⟩
    · exact fun f hf => ih.2.2.1 f (orbitUpdate_shrinks _ _ O _ f hf)
    · rw [← generators] at ih ⊢
      exact orbitUpdate_symmetric _ _ P O member _ ih.2.2.2.1
  | @orbit_raise K X _ P generators O member maxima updated _ delta positive ih =>
    have contains : Subset (final A K side) (downset maxima) := by
      rw [updated]; exact orbit_preserves_winner K side O X ih.2.1
    have hm := maxima_safe (A := A) (maxima := maxima) (by
      rw [updated]; exact fun f hf => ih.2.2.1 f (orbitUpdate_shrinks K side O X f hf))
    have sym : Symmetric G (downset maxima) := by
      rw [updated, ← generators]; rw [← generators] at ih
      exact orbitUpdate_symmetric K side P O member X ih.2.2.2.1
    obtain ⟨hc, hs, hy⟩ := bound_raise_invariant K delta positive side G maxima contains hm sym
    exact ⟨by omega, hc, hs, hy, raised_initial K delta ih.1 positive side maxima contains⟩
-- An unchanged warm-start sweep returns the exact final region; equivariant_k_bounded_safety_aut.hh:721–774.
theorem adaptive_sweep_completed (side : Bool) (P : SweepPlan A side) (X : Region n K)
    (reached : AdaptiveReach A side P.generators K X) (unchanged : sweep A K side P X = X) :
    X = final A K side ∧ winner A K side = true := by
  obtain ⟨_, contains, safeRegion, symmetric, hin⟩ := adaptive_invariants reached
  have fixed := sweep_fixed_cpre K side P X symmetric unchanged
  have he : X = final A K side := subset_antisymm
    (final_greatest K side X safeRegion (by rw [fixed]; exact fun _ h => h)) contains
  refine ⟨he, ?_⟩
  change final A K side (initial A K) = true
  rw [← he]; exact hin
-- The test after one orbit refutes its actual bound; equivariant_k_bounded_safety_aut.hh:746–750.
theorem adaptive_orbit_out (side : Bool) (P : SweepPlan A side) (X : Region n K)
    (reached : AdaptiveReach A side P.generators K X) (O : InputOrbit A)
    (out : orbitUpdate A K side O X (initial A K) = false) : winner A K side = false := by
  have hc := orbit_preserves_winner K side O X (adaptive_invariants reached).2.1
  cases hw : winner A K side with
  | false => rfl
  | true => have hh := hc _ hw; rw [out] at hh; contradiction
theorem adaptive_completed (closed : BooleanClosed A) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K)) (reached : AdaptiveReach A side P.generators K (downset maxima))
    (rows : List (ActionTable.Row n a))
    (refines : ActionTable.Refines A side (P.orbits.map InputOrbit.representative) rows)
    (exhausted : (criticalCached (ActionTable.applyAction A K) maxima rows).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  have h := adaptive_invariants reached -- No-input success certifies the actual returned K; equivariant_k_bounded_safety_aut.hh:936–976.
  exact ActionTable.refined_completed A closed K h.1 side P maxima h.2 rows refines exhausted
-- Selected physical classes implement both initial-out branches; equivariant_k_bounded_safety_aut.hh:945–956.
theorem adaptive_selected (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : AdaptiveReach A side P.generators K (downset maxima))
    (rows : List (ActionTable.Row n a))
    (refines : ActionTable.Refines A side (P.orbits.map InputOrbit.representative) rows)
    (row : ActionTable.Row n a) (hr : row ∈ rows) :
    (ActionTable.update A K P.generators maxima row (initial A K) = false → winner A K side = false) ∧
    (ActionTable.update A K P.generators maxima row (initial A K) = true →
      AdaptiveReach A side P.generators K (ActionTable.update A K P.generators maxima row)) := by
  have h := adaptive_invariants reached
  have hm := maxima_safe h.2.2.1
  obtain ⟨i, _, _, he⟩ := ActionTable.selected_refinement A K h.1 side P.generators maxima hm _ rows refines row hr
  rw [he]; constructor
  · intro out
    cases hw : winner A K side with
    | false => rfl
    | true =>
      have hc := selected_preserves K side P.generators i _ h.2.1 _ hw
      rw [out] at hc; contradiction
  · exact fun hin => .update reached i hin
-- Reuse the physical post-update maxima at the larger bound; equivariant_k_bounded_safety_aut.hh:963–976.
theorem adaptive_selected_raise (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : AdaptiveReach A side P.generators K (downset maxima))
    (rows : List (ActionTable.Row n a))
    (refines : ActionTable.Refines A side (P.orbits.map InputOrbit.representative) rows)
    (row : ActionTable.Row n a) (hr : row ∈ rows) (updated : List (State n K))
    (he : downset updated = ActionTable.update A K P.generators maxima row)
    (out : downset updated (initial A K) = false) (delta : Nat) (positive : 1 ≤ delta) :
    AdaptiveReach A side P.generators (K + delta) (downset (updated.map (bump A K delta))) := by
  have h := adaptive_invariants reached
  have hm := maxima_safe h.2.2.1
  obtain ⟨i, _, _, hi⟩ := ActionTable.selected_refinement A K h.1 side P.generators maxima hm _ rows refines row hr
  exact .raise reached i updated (he.trans hi) out delta positive
-- Shared scheduling adds kinc without clamping; equivariant_k_bounded_safety_aut.hh:963–969.
def sharedNext (K limit inc : Nat) : Option Nat := if K < limit then some (K + inc) else none
def ordinaryNext (K limit inc : Nat) : Option Nat := -- Ordinary scheduling clamps to kmax; k_schedule.hh:8–14.
  if K < limit ∧ 0 < inc then some (min limit (K + inc)) else none
theorem schedules_raise (K limit inc : Nat) (belowLimit : K < limit) (positive : 1 ≤ inc) :
    sharedNext K limit inc = some (K + inc) ∧ K < K + inc ∧
    ordinaryNext K limit inc = some (min limit (K + inc)) ∧ K < min limit (K + inc) := by
  refine ⟨by simp [sharedNext, belowLimit], by omega, -- Both valid schedules supply a positive actual increase; k_schedule.hh:8–14.
    by simp [ordinaryNext, belowLimit, show 0 < inc by omega], ?_⟩
  omega
-- The shared loop lifts by exactly kinc; equivariant_k_bounded_safety_aut.hh:963–969.
theorem shared_loop_raise (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : AdaptiveReach A side P.generators K (downset maxima))
    (rows : List (ActionTable.Row n a))
    (refines : ActionTable.Refines A side (P.orbits.map InputOrbit.representative) rows)
    (row : ActionTable.Row n a) (hr : row ∈ rows) (updated : List (State n K))
    (he : downset updated = ActionTable.update A K P.generators maxima row)
    (out : downset updated (initial A K) = false) (limit inc : Nat)
    (belowLimit : K < limit) (positive : 1 ≤ inc) : sharedNext K limit inc = some (K + inc) ∧
      AdaptiveReach A side P.generators (K + inc) (downset (updated.map (bump A K inc))) :=
  ⟨(schedules_raise K limit inc belowLimit positive).1,
    adaptive_selected_raise side P maxima reached rows refines row hr updated he out inc positive⟩
-- The ordinary lift uses its smaller clamped delta; k_bounded_safety_aut.hh:220–238.
theorem ordinary_loop_delta (K limit inc : Nat) (belowLimit : K < limit) (positive : 1 ≤ inc) :
    let delta := min limit (K + inc) - K
    1 ≤ delta ∧ ordinaryNext K limit inc = some (K + delta) := by
  have h := schedules_raise K limit inc belowLimit positive
  dsimp only; refine ⟨by omega, ?_⟩
  rw [h.2.2.1]; congr 1
  omega
theorem schedules_agree (K limit inc : Nat) (positive : 1 ≤ inc) (within : K + inc ≤ limit) :
    sharedNext K limit inc = ordinaryNext K limit inc := by
  have hb : K < limit := by omega -- Schedules coincide when the increment lands within kmax; k_schedule.hh:8–14.
  simp [sharedNext, ordinaryNext, hb, show 0 < inc by omega, Nat.min_eq_right within]
theorem refuted_at_limit (K limit : Nat) (positive : 1 ≤ limit) (atLimit : limit ≤ K)
    (side : Bool) (out : winner A K side = false) : winner A limit side = false := by
  cases hw : winner A limit side with -- Overshooting refutation implies refutation at kmax; k_schedule.hh:8–14.
  | false => rfl
  | true =>
    have hh := winner_mono limit (K - limit) positive side hw
    have he : limit + (K - limit) = K := by omega
    rw [he, out] at hh; contradiction
-- The final initial-out branch refutes the configured maximum; equivariant_k_bounded_safety_aut.hh:951–976.
theorem adaptive_stopped (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : AdaptiveReach A side P.generators K (downset maxima))
    (rows : List (ActionTable.Row n a))
    (refines : ActionTable.Refines A side (P.orbits.map InputOrbit.representative) rows)
    (row : ActionTable.Row n a) (hr : row ∈ rows)
    (out : ActionTable.update A K P.generators maxima row (initial A K) = false)
    (limit : Nat) (positive : 1 ≤ limit) (atLimit : limit ≤ K) : winner A limit side = false :=
  refuted_at_limit K limit positive atLimit side ((adaptive_selected side P maxima reached rows refines row hr).1 out)

end Symmetry

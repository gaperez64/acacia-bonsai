import SymmetryProofs.Reuse

namespace Symmetry
variable {A : Automaton n a}

/-- C++: actioners/standard.hh:175–181. Meet of all destination constraints. -/
def minimum (b : Nat) : List Nat → Nat
  | [] => b
  | x :: xs => min x (minimum b xs)

theorem le_minimum (b z : Nat) (xs : List Nat) :
    z ≤ minimum b xs ↔ z ≤ b ∧ ∀ x ∈ xs, z ≤ x := by
  induction xs with
  | nil => simp [minimum]
  | cons x xs ih => simp only [minimum, Nat.le_min, ih, List.mem_cons, forall_eq_or_imp]; grind

/-- C++: actioners/standard.hh:175–181. All successor rows constrain each source. -/
def minStates (b : Nat) (g : Fin n → Nat) : Nat :=
  minimum b ((List.finRange n).map g)

theorem le_minStates (b z : Nat) (g : Fin n → Nat) :
    z ≤ minStates b g ↔ z ≤ b ∧ ∀ q, z ≤ g q := by
  simp [minStates, le_minimum]

theorem minStates_perm (b : Nat) (p : Perm (Fin n)) (f g : Fin n → Nat)
    (h : ∀ q, f (p q) = g q) : minStates b f = minStates b g := by
  have one : ∀ (p : Perm (Fin n)) (f g : Fin n → Nat),
      (∀ q, f (p q) = g q) → minStates b f ≤ minStates b g := by
    intro p f g h
    have hf := (le_minStates b (minStates b f) f).mp (Nat.le_refl _)
    apply (le_minStates _ _ _).mpr
    exact ⟨hf.1, fun q => (h q) ▸ hf.2 (p q)⟩
  apply Nat.le_antisymm (one p f g h)
  apply one p.symm g f
  intro q
  have := h (p.invFun q)
  simpa [Perm.symm] using this.symm

/-- C++: actioners/standard.hh:52–56, 141–145. Reset bound before backward apply. -/
def safeCap (A : Automaton n a) (K : Nat) (q : Fin n) : Nat :=
  if A.counting q then K else 1

/-- C++: actioners/standard.hh:175–181. Shifted max(-1,m[dst]-increment). -/
def backwardConstraint (A : Automaton n a) (K : Nat) (m : State n K)
    (v : Fin a → Bool) (q r : Fin n) : Nat :=
  if A.enabled q r v then (m r).val - (if A.rejecting r then 1 else 0) else K + 1

/-- C++: actioners/standard.hh:153–157, 174–195. Start at the safe box and
    tighten each source by min; zero (-1) is absorbing, so early skips are exact. -/
def backward (A : Automaton n a) (K : Nat) (m : State n K) (v : Fin a → Bool) : State n K :=
  fun q => ⟨min (safeCap A K q) (minStates (K + 1) (fun r => backwardConstraint A K m v q r)), by
    have hc : safeCap A K q ≤ K + 1 := by unfold safeCap; split <;> omega
    have := Nat.min_le_left (safeCap A K q) (minStates (K + 1) (fun r => backwardConstraint A K m v q r))
    omega⟩

/-- C++: posets coordinate order; equivariant:510–515. Downset membership. -/
def below (f m : State n K) : Bool := decide (∀ q, (f q).val ≤ (m q).val)

/-- C++: SetOfStates maximal-element representation, k_bounded:278–291. -/
def downset (maxima : List (State n K)) : Region n K :=
  fun f => maxima.any fun m => below f m

/-- C++: equivariant_k_bounded_safety_aut.hh:434–466. Apply-to-maxima union.
    Duplicate outputs/actions have no effect on this membership predicate. -/
def backwardT (A : Automaton n a) (K : Nat) (side : Bool)
    (maxima : List (State n K)) (i : Fin a → Bool) : Region n K :=
  fun f => (letters a).any fun o => maxima.any fun m => below f (backward A K m (merge A side i o))

private theorem scalar_preimage (x y K d : Nat) (hy : y ≤ K) (hd : d ≤ 1) :
    (if x > 0 then min (K + 1) (x + d) else 0) ≤ y ↔ x ≤ y - d := by
  by_cases hx : x > 0 <;> simp only [hx, ite_true, ite_false] <;> omega

private theorem contribution_preimage (A : Automaton n a) (K : Nat)
    (f m : State n K) (v : Fin a → Bool) (q r : Fin n) (hm : (m r).val ≤ K) :
    contribution A K f v q r ≤ (m r).val ↔ (f q).val ≤ backwardConstraint A K m v q r := by
  by_cases he : A.enabled q r v = true
  · simp only [contribution, backwardConstraint, he, Bool.true_and, decide_eq_true_eq, ite_true]
    exact scalar_preimage _ _ _ _ hm (by split <;> omega)
  · have he' : A.enabled q r v = false := Bool.eq_false_iff.mpr he
    simp only [contribution, backwardConstraint, he', Bool.false_and, Bool.false_eq_true, ite_false,
      Nat.zero_le, true_iff]
    have := (f q).isLt
    omega

/-- On the safe box, the ported min/subtract backward operation is the preimage
    of the max/increment successor, connecting antichain apply to game CPre.
    K≥1 is the C++ bound contract; symmetry.hh:14–17, issue #331. -/
theorem backward_preimage (K : Nat) (hk : 1 ≤ K) (f m : State n K)
    (hm : safe A K m = true) (v : Fin a → Bool) :
    below f (backward A K m v) = true ↔
      safe A K f = true ∧ below (succ A K f v) m = true := by
  have hm' : ∀ r, (m r).val ≤ K := by
    intro r
    have hr := (of_decide_eq_true hm) r
    cases hc : A.counting r <;> simp [hc] at hr <;> omega
  simp only [below, decide_eq_true_eq]
  constructor
  · intro h
    have hsafe : safe A K f = true := by
      apply decide_eq_true
      intro q
      exact Nat.le_trans (h q) (Nat.min_le_left ..)
    refine ⟨hsafe, fun r => ?_⟩
    apply (maxStates_le _ _).mpr
    intro q
    apply (contribution_preimage A K f m v q r (hm' r)).mpr
    have hmin := Nat.le_trans (h q) (Nat.min_le_right ..)
    exact ((le_minStates _ _ _).mp hmin).2 r
  · intro ⟨hf, h⟩ q
    apply Nat.le_min.mpr
    refine ⟨(of_decide_eq_true hf) q, (le_minStates _ _ _).mpr ⟨?_, ?_⟩⟩
    · have := (f q).isLt; omega
    · intro r
      apply (contribution_preimage A K f m v q r (hm' r)).mp
      exact Nat.le_trans (le_maxStates (fun q => contribution A K f v q r) q) (h r)

/-- Applying backward to maximal elements and unioning over outputs computes
    the game's predecessor on safe vectors. This proves the downset semantics
    of compute_T, equivariant:434–466; symmetry.hh:14–17, #331. -/
theorem backwardT_exact (K : Nat) (hk : 1 ≤ K) (side : Bool)
    (maxima : List (State n K)) (hm : ∀ m ∈ maxima, safe A K m = true)
    (i : Fin a → Bool) (f : State n K) :
    backwardT A K side maxima i f = (safe A K f && computeT A K side (downset maxima) i f) := by
  apply Bool.eq_iff_iff.mpr
  simp only [backwardT, Bool.and_eq_true, computeT_iff, downset, List.any_eq_true]
  constructor
  · intro ⟨o, _, m, hmem, h⟩
    obtain ⟨hs, hp⟩ := (backward_preimage K hk f m (hm m hmem) _).mp h
    exact ⟨hs, o, m, hmem, hp⟩
  · intro ⟨hs, o, m, hmem, hp⟩
    exact ⟨o, mem_letters o, m, hmem, (backward_preimage K hk f m (hm m hmem) _).mpr ⟨hs, hp⟩⟩

/-- The backward min/subtract computation used by both solvers commutes,
    including the bool/counting reset caps;
    symmetry.hh:9–17, equivariant:434–466, issue #331. -/
theorem backward_commutes (s : Verified A) (K : Nat) (m : State n K) (v : Fin a → Bool) :
    backward A K (act s.states m) (act s.aps v) = act s.states (backward A K m v) := by
  funext q
  apply Fin.ext
  rw [← s.states.right_inv q]
  simp only [backward, act_apply, safeCap, s.caps]
  congr 1
  apply minStates_perm (K + 1) s.states
  intro r
  simp only [backwardConstraint, act_apply, enabled_commutes, s.acceptance]

/-- A coordinate permutation is an order isomorphism, so maximal-vector
    transport preserves the represented downset. This justifies the trusted
    bulk antichain factory in equivariant:388–416, symmetry.hh:14–17, #331. -/
theorem below_commutes (p : Perm (Fin n)) (f m : State n K) :
    below (act p f) (act p m) = below f m := by
  apply Bool.eq_iff_iff.mpr
  simp only [below, decide_eq_true_eq]
  constructor
  · intro h q
    simpa using h (p q)
  · intro h q
    exact h (p.invFun q)

/-- Permuting antichain maxima gives exactly the image of its downset;
    equivariant:388–432, symmetry.hh:14–17, issue #331. -/
theorem image_downset (p : Perm (Fin n)) (maxima : List (State n K)) :
    downset (maxima.map (act p)) = image p (downset maxima) := by
  funext f
  have hb : ∀ m, below f (act p m) = below (act p.symm f) m := by
    intro m
    have := below_commutes p (act p.symm f) m
    simpa using this
  simp [downset, image, List.any_map, Function.comp_def, hb]

theorem downset_safe (K : Nat) (maxima : List (State n K))
    (hm : ∀ m ∈ maxima, safe A K m = true) : Subset (downset maxima) (safe A K) := by
  intro f hf
  obtain ⟨m, hmem, hb⟩ := List.any_eq_true.mp hf
  apply decide_eq_true
  intro q
  exact Nat.le_trans ((of_decide_eq_true hb) q) ((of_decide_eq_true (hm m hmem)) q)

/-- The antichain apply/union/intersection update equals the game's shrinking
    input update at k_bounded:244–301 and equivariant:597–628. This proves the
    backward preimage obligation of #331, symmetry.hh:14–17. -/
theorem backward_update_exact (K : Nat) (hk : 1 ≤ K) (side : Bool)
    (maxima : List (State n K)) (hm : ∀ m ∈ maxima, safe A K m = true)
    (i : Fin a → Bool) (f : State n K) :
    (downset maxima f && backwardT A K side maxima i f) =
      (downset maxima f && computeT A K side (downset maxima) i f) := by
  rw [backwardT_exact K hk side maxima hm]
  cases hx : downset maxima f with
  | false => rfl
  | true => simp only [downset_safe K maxima hm f hx, Bool.true_and]

end Symmetry

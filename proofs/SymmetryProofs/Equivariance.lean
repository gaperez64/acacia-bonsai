import SymmetryProofs.Game

namespace Symmetry
variable {A : Automaton n a}

/-- All universally enabled successors, including rejecting increments and
    saturation, commute with a verified symmetry for either choice of
    protagonist, as required by symmetry.hh:9–17 and #331. -/
theorem succ_commutes (s : Verified A) (K : Nat) (f : State n K) (v : Fin a → Bool) :
    succ A K (act s.states f) (act s.aps v) = act s.states (succ A K f v) := by
  funext r
  apply Fin.ext
  change maxStates _ = _
  rw [← s.states.right_inv r]
  simp only [act_apply]
  apply maxStates_perm s.states
  intro q
  simp only [contribution, act_apply, enabled_commutes, s.acceptance]

/-- The symmetric safe box stays unchanged; acceptance and cap preservation
    are essential to symmetry.hh:14–17 and issue #331. -/
theorem safe_commutes (s : Verified A) (K : Nat) (f : State n K) :
    safe A K (act s.states f) = safe A K f := by
  apply Bool.eq_iff_iff.mpr
  simp only [safe, decide_eq_true_eq]
  constructor
  · intro h q
    have := h (s.states q)
    simpa [s.caps q] using this
  · intro h q
    have := h (s.states.invFun q)
    have hc := s.caps (s.states.invFun q)
    simp only [s.states.right_inv] at hc
    simpa only [act, hc] using this

/-- The initial vector is fixed, so region invariance preserves the verdict;
    symmetry.hh:12–17, issue #331. -/
theorem initial_fixed (s : Verified A) (K : Nat) :
    act s.states (initial A K) = initial A K := by
  funext q
  have hi : s.states.invFun A.init = A.init := by
    have := congrArg s.states.invFun s.init_fixed
    simpa using this.symm
  have he : s.states.invFun q = A.init ↔ q = A.init := by
    constructor
    · intro h
      have := congrArg s.states h
      simpa [s.init_fixed] using this
    · intro h; simpa [h] using hi
  simp only [act, initial, he]

/-- Partition preservation makes the input/output composition commute.
    PR #330 supplies this hypothesis, required by #331, symmetry.hh:9–17. -/
theorem merge_commutes (s : Verified A) (side : Bool) (i o : Fin a → Bool) :
    merge A side (act s.aps i) (act s.aps o) = act s.aps (merge A side i o) := by
  funext p
  have hp := s.partition (s.aps.invFun p)
  simp only [s.aps.right_inv] at hp
  simp only [merge, act, hp]

/-- Reusing a representative's union of backward sets is exact on a symmetric
    source region. It ranges over every output, as equivariant:434–466 requires;
    symmetry.hh:14–17 and issue #331. -/
theorem computeT_commutes (s : Verified A) (K : Nat) (side : Bool)
    (X : Region n K) (i : Fin a → Bool) (f : State n K) :
    computeT A K side (image s.states X) (act s.aps i) (act s.states f) =
      computeT A K side X i f := by
  apply Bool.eq_iff_iff.mpr
  simp only [computeT_iff]
  constructor
  · intro ⟨o, ho⟩
    refine ⟨act s.aps.symm o, ?_⟩
    have hm := merge_commutes s side i (act s.aps.symm o)
    simp only [act_inverse'] at hm
    rw [hm, succ_commutes] at ho
    simpa [image] using ho
  · intro ⟨o, ho⟩
    refine ⟨act s.aps o, ?_⟩
    rw [merge_commutes, succ_commutes]
    simpa [image] using ho

/-- CPre commutes in either frame: environment choices remain universal and
    protagonist choices existential because pi preserves I/O, as required by
    symmetry.hh:14–17 and issue #331. -/
theorem cpre_commutes (s : Verified A) (K : Nat) (side : Bool) (X : Region n K) :
    cpre A K side (image s.states X) = image s.states (cpre A K side X) := by
  have h : ∀ g : State n K,
      cpre A K side (image s.states X) (act s.states g) = cpre A K side X g := by
    intro g
    simp only [cpre]
    have hi : image s.states X (act s.states g) = X g := by simp [image]
    rw [hi]
    congr 1
    apply Bool.eq_iff_iff.mpr
    simp only [List.all_eq_true]
    constructor
    · intro h i _
      have ht := h (act s.aps i) (mem_letters _)
      simpa only [computeT_commutes] using ht
    · intro h i _
      have ht := computeT_commutes s K side X (act s.aps.symm i) g
      simp only [act_inverse'] at ht
      rw [ht]
      exact h _ (mem_letters _)
  funext f
  have ht := h (act s.states.symm f)
  simpa [image] using ht

/-- C++: equivariant_k_bounded_safety_aut.hh:518–553. Semantic group invariance. -/
def Invariant (p : Perm (Fin n)) (X : Region n K) : Prop :=
  ∀ f, X (act p f) = X f

theorem image_eq (p : Perm (Fin n)) (X : Region n K) (h : Invariant p X) : image p X = X := by
  funext f
  have := h (act p.symm f)
  simpa [image] using this.symm

/-- Every descending simultaneous stage is symmetric, starting at the safe
    box, as required by symmetry.hh:14–17 and the stage obligation of issue #331. -/
theorem stage_invariant (s : Verified A) (K : Nat) (side : Bool) (k : Nat) :
    Invariant s.states (stage A K side k) := by
  induction k with
  | zero => exact safe_commutes s K
  | succ k ih =>
    intro f
    have h := congrFun (cpre_commutes s K side (stage A K side k)) (act s.states f)
    rw [image_eq _ _ ih] at h
    simpa [stage, iterate, image] using h

/-- The terminating final region is symmetric; canonical representatives
    therefore preserve membership. Soundness note:14–17, issue #331. -/
theorem final_invariant (s : Verified A) (K : Nat) (side : Bool) :
    Invariant s.states (final A K side) :=
  stage_invariant s K side _

/-- Initial membership and its renamed/canonicalized test give the same winner,
    in either frame. Soundness note:12–17, issue #331. -/
theorem winner_invariant (s : Verified A) (K : Nat) (side : Bool) :
    final A K side (act s.states (initial A K)) = winner A K side :=
  final_invariant s K side _

/-- Original system controls outputs. C++: k_bounded_safety_aut.hh:244–247;
    symmetry.hh:9–17, issue #331's normal frame. -/
theorem normal_frame (s : Verified A) (K : Nat) (X : Region n K) :
    cpre A K false (image s.states X) = image s.states (cpre A K false X) :=
  cpre_commutes s K false X

/-- In the unreal formula worker, engine inputs are original outputs and the
    safety protagonist controls original inputs. C++: solver_invoker.cc:1159–1165;
    symmetry.hh:9–17, issue #331's swapped frame. -/
theorem swapped_frame (s : Verified A) (K : Nat) (X : Region n K) :
    cpre A K true (image s.states X) = image s.states (cpre A K true X) :=
  cpre_commutes s K true X

end Symmetry

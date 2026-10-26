import SymmetryProofs.Equivariance

namespace Symmetry

/-- C++: equivariant_k_bounded_safety_aut.hh:510–515. Set inclusion. -/
def Subset (X Y : α → Bool) : Prop := ∀ f, X f = true → Y f = true

theorem computeT_mono (A : Automaton n a) (K : Nat) (side : Bool)
    {X Y : Region n K} (h : Subset X Y) (i : Fin a → Bool) :
    Subset (computeT A K side X i) (computeT A K side Y i) := by
  intro f hf
  obtain ⟨o, ho⟩ := (computeT_iff _ _ _ _ _ _).mp hf
  exact (computeT_iff _ _ _ _ _ _).mpr ⟨o, h _ ho⟩

theorem cpre_mono (A : Automaton n a) (K : Nat) (side : Bool)
    {X Y : Region n K} (h : Subset X Y) : Subset (cpre A K side X) (cpre A K side Y) := by
  intro f hf
  obtain ⟨hx, hs⟩ := (cpre_iff _ _ _ _ _).mp hf
  apply (cpre_iff _ _ _ _ _).mpr
  exact ⟨h _ hx, fun i => let ⟨o, ho⟩ := hs i; ⟨o, h _ ho⟩⟩

theorem cpre_shrinks (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K) :
    Subset (cpre A K side X) X := by
  intro f hf
  exact ((cpre_iff _ _ _ _ _).mp hf).1

theorem iterate_shift (F : (α → Bool) → α → Bool) (X : α → Bool) (k : Nat) :
    iterate F X (k + 1) = iterate F (F X) k := by
  induction k with
  | zero => rfl
  | succ k ih =>
    change F (iterate F X (k + 1)) = F (iterate F (F X) k)
    rw [ih]

theorem iterate_of_fixed (F : (α → Bool) → α → Bool) (X : α → Bool)
    (h : F X = X) (k : Nat) : iterate F X k = X := by
  induction k with
  | zero => rfl
  | succ k ih => simp only [iterate, ih, h]

theorem filter_strict (U : List α) (full : ∀ f, f ∈ U) (X Y : α → Bool)
    (shrink : Subset Y X) (f : α) (hx : X f = true) (hy : Y f = false) :
    (U.filter Y).length < (U.filter X).length := by
  have he : U.filter Y = (U.filter X).filter Y := by
    rw [List.filter_filter]
    congr 1
    funext g
    cases h : Y g <;> simp_all [shrink g]
  rw [he]
  apply List.length_filter_lt_length_iff_exists.mpr
  exact ⟨f, by simp [full f, hx], by simp [hy]⟩

/-- Descending iteration terminates within the number of concrete game states;
    this proves the stopping condition, rather than assuming it. It justifies
    the finite executable loop, symmetry.hh:14–17 and issue #331. -/
theorem iterate_stable (U : List α) (full : ∀ f, f ∈ U)
    (F : (α → Bool) → α → Bool) (shrink : ∀ X, Subset (F X) X)
    (fuel : Nat) (X : α → Bool) (bound : (U.filter X).length ≤ fuel) :
    F (iterate F X fuel) = iterate F X fuel := by
  induction fuel generalizing X with
  | zero =>
    have hx : X = fun _ => false := by
      funext f
      have hm : f ∉ U.filter X := by
        have he : U.filter X = [] := List.length_eq_zero_iff.mp (by omega)
        simp [he]
      cases he : X f with
      | false => rfl
      | true => exact False.elim (hm (by simp [full f, he]))
    have hf : F X = X := by
      funext f
      have hs := shrink X f
      rw [hx] at hs ⊢
      cases he : F (fun _ => false) f <;> simp_all
    exact hf
  | succ fuel ih =>
    by_cases he : F X = X
    · rw [iterate_of_fixed F X he]
      exact he
    · have hw : ∃ f, X f = true ∧ F X f = false := by
        apply Classical.byContradiction
        intro hn
        apply he
        funext f
        cases hf : F X f with
        | true => exact (shrink X f hf).symm
        | false =>
          cases hx : X f with
          | false => rfl
          | true => exact False.elim (hn ⟨f, hx, hf⟩)
      obtain ⟨f, hx, hf⟩ := hw
      have ht := filter_strict U full X (F X) (shrink X) f hx hf
      have hb : (U.filter (F X)).length ≤ fuel := by omega
      simpa only [iterate_shift] using ih (F X) hb

variable {A : Automaton n a}

/-- The final stage really is a fixed point, not merely a bounded approximation.
    This is the final-fixpoint obligation in symmetry.hh:14–17 and issue #331. -/
theorem final_fixed (K : Nat) (side : Bool) :
    cpre A K side (final A K side) = final A K side :=
  iterate_stable (states n K) mem_states (cpre A K side) (cpre_shrinks A K side)
    _ _ (List.length_filter_le ..)

/-- Any postfixed safe region survives every stage. Thus the final region is
    the greatest safe fixed point, as required by symmetry.hh:14–17 and #331. -/
theorem final_greatest (K : Nat) (side : Bool) (Y : Region n K)
    (hsafe : Subset Y (safe A K)) (hpost : Subset Y (cpre A K side Y)) :
    Subset Y (final A K side) := by
  have h : ∀ k, Subset Y (stage A K side k) := by
    intro k
    induction k with
    | zero => exact hsafe
    | succ k ih =>
      intro f hf
      exact cpre_mono A K side ih f (hpost f hf)
  exact h _

theorem stage_safe (K : Nat) (side : Bool) (k : Nat) :
    Subset (stage A K side k) (safe A K) := by
  induction k with
  | zero => exact fun _ h => h
  | succ k ih => exact fun f h => ih f (cpre_shrinks A K side _ f h)

end Symmetry

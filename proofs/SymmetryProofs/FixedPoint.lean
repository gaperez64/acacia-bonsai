import SymmetryProofs.Equivariance

namespace Symmetry
def Subset (X Y : α → Bool) : Prop := ∀ f, X f = true → Y f = true
theorem computeT_mono (A : Automaton n a) (K : Nat) (side : Bool)
    {X Y : Region n K} (h : Subset X Y) (i : Fin a → Bool) :
    Subset (computeT A K side X i) (computeT A K side Y i) := by
  intro f hf; obtain ⟨o, ho⟩ := (computeT_iff _ _ _ _ _ _).mp hf
  exact (computeT_iff _ _ _ _ _ _).mpr ⟨o, h _ ho⟩
theorem cpre_mono (A : Automaton n a) (K : Nat) (side : Bool)
    {X Y : Region n K} (h : Subset X Y) : Subset (cpre A K side X) (cpre A K side Y) := by
  intro f hf; obtain ⟨hx, hs⟩ := (cpre_iff _ _ _ _ _).mp hf
  apply (cpre_iff _ _ _ _ _).mpr; exact ⟨h _ hx, fun i => let ⟨o, ho⟩ := hs i; ⟨o, h _ ho⟩⟩
theorem cpre_shrinks (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K) :
    Subset (cpre A K side X) X := by intro f hf; exact ((cpre_iff _ _ _ _ _).mp hf).1
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
theorem subset_antisymm {X Y : α → Bool} (h : Subset X Y) (h' : Subset Y X) : X = Y := by funext f; exact Bool.eq_iff_iff.mpr ⟨h f, h' f⟩
theorem iterate_preserves (F : (α → Bool) → α → Bool) (P : (α → Bool) → Prop)
    (step : ∀ X, P X → P (F X)) (X : α → Bool) (hx : P X) (k : Nat) : P (iterate F X k) := by
  induction k with
  | zero => exact hx
  | succ k ih => exact step _ ih
theorem iterate_shrinks (F : (α → Bool) → α → Bool) (shrink : ∀ X, Subset (F X) X)
    (X : α → Bool) (k : Nat) : Subset (iterate F X k) X := by
  induction k with
  | zero => exact fun _ h => h
  | succ k ih => exact fun f h => ih f (shrink _ f h)
theorem filter_strict (U : List α) (X Y : α → Bool)
    (shrink : Subset Y X) (f : α) (mem : f ∈ U) (hx : X f = true) (hy : Y f = false) :
    (U.filter Y).length < (U.filter X).length := by
  have he : U.filter Y = (U.filter X).filter Y := by
    rw [List.filter_filter]; congr 1
    funext g; cases h : Y g <;> simp_all [shrink g]
  rw [he]; apply List.length_filter_lt_length_iff_exists.mpr
  exact ⟨f, by simp [mem, hx], by simp [hy]⟩
theorem iterate_stable (U : List α) (full : ∀ f, f ∈ U)
    (F : (α → Bool) → α → Bool) (shrink : ∀ X, Subset (F X) X)
    (fuel : Nat) (X : α → Bool) (bound : (U.filter X).length ≤ fuel) :
    F (iterate F X fuel) = iterate F X fuel := by
  induction fuel generalizing X with -- A shrinking finite loop terminates within the state count; k_bounded_safety_aut.hh:129–165.
  | zero =>
    have hx : X = fun _ => false := by
      funext f; have hm : f ∉ U.filter X := by
        have he : U.filter X = [] := List.length_eq_zero_iff.mp (by omega); simp [he]
      cases he : X f with
      | false => rfl
      | true => exact False.elim (hm (by simp [full f, he]))
    have hf : F X = X := by
      funext f; have hs := shrink X f
      rw [hx] at hs ⊢; cases he : F (fun _ => false) f <;> simp_all
    exact hf
  | succ fuel ih =>
    by_cases he : F X = X
    · rw [iterate_of_fixed F X he]
      exact he
    · have hw : ∃ f, X f = true ∧ F X f = false := by
        apply Classical.byContradiction; intro hn
        apply he; funext f
        cases hf : F X f with
        | true => exact (shrink X f hf).symm
        | false =>
          cases hx : X f with
          | false => rfl
          | true => exact False.elim (hn ⟨f, hx, hf⟩)
      obtain ⟨f, hx, hf⟩ := hw
      have ht := filter_strict U X (F X) (shrink X) f (full f) hx hf
      have hb : (U.filter (F X)).length ≤ fuel := by omega
      simpa only [iterate_shift] using ih (F X) hb

variable {A : Automaton n a}
-- The last simultaneous stage is CPre-fixed; symmetry.hh:14–17.
theorem final_fixed (K : Nat) (side : Bool) : cpre A K side (final A K side) = final A K side :=
  iterate_stable (states n K) mem_states (cpre A K side) (cpre_shrinks A K side)
    _ _ (List.length_filter_le ..)
theorem final_greatest (K : Nat) (side : Bool) (Y : Region n K) -- Every safe postfixed region survives; symmetry.hh:14–17.
    (hsafe : Subset Y (safe A K)) (hpost : Subset Y (cpre A K side Y)) : Subset Y (final A K side) := by
  have h : ∀ k, Subset Y (stage A K side k) := by
    intro k; induction k with
    | zero => exact hsafe
    | succ k ih =>
      intro f hf; exact cpre_mono A K side ih f (hpost f hf)
  exact h _
theorem stage_safe (K : Nat) (side : Bool) (k : Nat) : Subset (stage A K side k) (safe A K) :=
  iterate_shrinks _ (cpre_shrinks A K side) _ k
-- Share the shrinking-loop greatest-fixed-point argument; k_bounded_safety_aut.hh:129–165.
theorem iterate_eq_final (K : Nat) (side : Bool) (F : Region n K → Region n K)
    (shrink : ∀ X, Subset (F X) X)
    (preserves : ∀ X, Subset (final A K side) X → Subset (final A K side) (F X))
    (post : ∀ k, let X := iterate F (safe A K) k; F X = X → cpre A K side X = X) :
    iterate F (safe A K) (states n K).length = final A K side := by
  have contains := iterate_preserves F (Subset (final A K side)) preserves (safe A K) (stage_safe K side _) (states n K).length
  have bounded := iterate_shrinks F shrink (safe A K) (states n K).length
  have fixed := post _ (iterate_stable (states n K) mem_states F shrink _ _ (List.length_filter_le ..))
  apply subset_antisymm _ contains; apply final_greatest K side _ bounded
  rw [fixed]; exact fun _ h => h

end Symmetry

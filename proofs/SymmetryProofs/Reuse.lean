import SymmetryProofs.Transports

namespace Symmetry
variable {α β : Type}
variable {A : Automaton n a}
def actionPerm (p : Perm α) : Perm (α → β) where
  toFun := act p
  invFun := act p.symm
  left_inv := act_inverse p
  right_inv := act_inverse' p
def reuseT (A : Automaton n a) (K : Nat) (side : Bool) (s : Verified A)
    (X : Region n K) (i : Fin a → Bool) : Region n K :=
  image s.states (computeT A K side X i) -- Transport the representative predecessor; equivariant_k_bounded_safety_aut.hh:388–432.
-- A symmetric source makes transported Pre exact; equivariant_k_bounded_safety_aut.hh:730–736.
theorem reuse_exact (s : Verified A) (K : Nat) (side : Bool) (X : Region n K)
    (hx : Invariant s.states X) (i : Fin a → Bool) :
    reuseT A K side s X i = computeT A K side X (act s.aps i) := by
  funext f; have h := computeT_commutes s K side X i (act s.states.symm f)
  rw [image_eq _ _ hx] at h; simpa [reuseT, image] using h.symm
structure InputOrbit (A : Automaton n a) where
  representative : Fin a → Bool -- Representative with its layout-induced transports; equivariant_k_bounded_safety_aut.hh:727–744.
  members : List (Verified A)
-- Snapshot T once, then intersect every transport; equivariant_k_bounded_safety_aut.hh:727–744.
def orbitUpdate (A : Automaton n a) (K : Nat) (side : Bool)
    (O : InputOrbit A) (X : Region n K) : Region n K :=
  let T := computeT A K side X O.representative
  O.members.foldl (fun Y s => fun f => Y f && image s.states T f) X
def plainOrbit (A : Automaton n a) (K : Nat) (side : Bool)
    (O : InputOrbit A) (X : Region n K) : Region n K :=
  fun f => X f && O.members.all (fun s => computeT A K side X (act s.aps O.representative) f)
theorem fold_meets (ss : List β) (T : β → α → Bool) (X : α → Bool) :
    ss.foldl (fun Y s => fun f => Y f && T s f) X = fun f => X f && ss.all (fun s => T s f) := by
  induction ss generalizing X with
  | nil => simp
  | cons s ss ih =>
    rw [List.foldl_cons, ih]; funext f
    simp only [List.all_cons, Bool.and_assoc]
-- Snapshot intersections equal the member predecessors; equivariant_k_bounded_safety_aut.hh:727–744.
theorem orbitUpdate_exact (K : Nat) (side : Bool) (O : InputOrbit A) (X : Region n K)
    (h : ∀ s ∈ O.members, Invariant s.states X) : orbitUpdate A K side O X = plainOrbit A K side O X := by
  rw [orbitUpdate, fold_meets]; funext f
  congr 1
  apply Bool.eq_iff_iff.mpr; simp only [List.all_eq_true]
  constructor
  · intro hall s hs
    have he := congrFun (reuse_exact s K side X (h s hs) O.representative) f; exact he ▸ hall s hs
  · intro hall s hs
    have he := congrFun (reuse_exact s K side X (h s hs) O.representative) f
    change reuseT A K side s X O.representative f = true
    rw [he]; exact hall s hs
def orbitLetters (side : Bool) (O : InputOrbit A) : List (Fin a → Bool) :=
  O.members.map fun s => opponentProjection A side (act s.aps O.representative)
-- Generated transports, orbit closure and projected coverage; equivariant_k_bounded_safety_aut.hh:217–334.
structure SweepPlan (A : Automaton n a) (side : Bool) where
  generators : List (Verified A)
  orbits : List (InputOrbit A)
  member_generated : ∀ O ∈ orbits, ∀ s ∈ O.members, GeneratedBy generators s.states s.aps
  orbit_closed : ∀ O ∈ orbits, ∀ s ∈ generators, ∀ i ∈ orbitLetters side O, act s.aps i ∈ orbitLetters side O
  covers : ∀ i, ∃ O ∈ orbits, opponentProjection A side i ∈ orbitLetters side O
-- Take a fresh snapshot for each successive orbit; equivariant_k_bounded_safety_aut.hh:727–744.
def sweep (A : Automaton n a) (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) : Region n K :=
  P.orbits.foldl (fun Y O => orbitUpdate A K side O Y) X
def Symmetric (G : List (Verified A)) (X : Region n K) : Prop :=
  ∀ s ∈ G, Invariant s.states X
theorem orbitUpdate_shrinks (K : Nat) (side : Bool) (O : InputOrbit A) (X : Region n K) :
    Subset (orbitUpdate A K side O X) X := by rw [orbitUpdate, fold_meets]; exact fun _ h => (Bool.and_eq_true_iff.mp h).1
theorem fold_preserves (xs : List β) (F : β → α → α) (P : α → Prop)
    (step : ∀ b ∈ xs, ∀ X, P X → P (F b X)) (X : α) (hx : P X) : P (xs.foldl (fun Y b => F b Y) X) := by
  induction xs generalizing X with
  | nil => exact hx
  | cons b bs ih => exact ih (fun c hc => step c (List.mem_cons_of_mem b hc)) _ (step b (by simp) X hx)
theorem fold_shrinks (xs : List β) (F : β → (α → Bool) → α → Bool)
    (h : ∀ b X, Subset (F b X) X) (X : α → Bool) : Subset (xs.foldl (fun Y b => F b Y) X) X := by
  induction xs generalizing X with
  | nil => exact fun _ hf => hf
  | cons b xs ih => exact fun f hf => h b X f (ih (F b X) f hf)
theorem fold_fixed (xs : List β) (F : β → (α → Bool) → α → Bool)
    (h : ∀ b X, Subset (F b X) X) (X : α → Bool)
    (fixed : xs.foldl (fun Y b => F b Y) X = X) : ∀ b ∈ xs, F b X = X := by
  induction xs generalizing X with
  | nil => simp
  | cons b xs ih =>
    have hb : F b X = X := subset_antisymm (h b X) (by
      intro f hf; rw [← fixed] at hf
      exact fold_shrinks xs F h (F b X) f hf)
    have ht : xs.foldl (fun Y b => F b Y) X = X := by simpa [hb] using fixed
    intro c hc; rcases List.mem_cons.mp hc with rfl | hc
    · exact hb
    · exact ih X ht c hc
theorem sweep_shrinks (K : Nat) (side : Bool) (P : SweepPlan A side) (X : Region n K) :
    Subset (sweep A K side P X) X :=
  fold_shrinks P.orbits (fun O Y => orbitUpdate A K side O Y) (orbitUpdate_shrinks K side) X
theorem plainOrbit_invariant (K : Nat) (side : Bool) (O : InputOrbit A)
    (X : Region n K) (s : Verified A) (hx : Invariant s.states X)
    (closed : ∀ i ∈ orbitLetters side O, act s.aps i ∈ orbitLetters side O) :
    Invariant s.states (plainOrbit A K side O X) := by
  intro f; have ht : ∀ i, computeT A K side X (act s.aps i) (act s.states f) = computeT A K side X i f := by
    intro i; have h := computeT_commutes s K side X i f
    rw [image_eq _ _ hx] at h; exact h
  have hc := finite_closed_iff (orbitLetters side O) (actionPerm s.aps) closed
  have hall : ∀ g, O.members.all (fun t => computeT A K side X (act t.aps O.representative) g) = true ↔
      ∀ i ∈ orbitLetters side O, computeT A K side X i g = true := by
    intro g; simp [orbitLetters, List.all_eq_true, computeT_projected]
  apply Bool.eq_iff_iff.mpr; simp only [plainOrbit, Bool.and_eq_true, hx f, hall]
  apply and_congr_right; intro _
  constructor
  · intro h i hi
    rw [← ht i]; exact h _ (closed i hi)
  · intro h i hi
    have hm : act s.aps.symm i ∈ orbitLetters side O :=
      (hc (act s.aps.symm i)).mp (by simpa [actionPerm] using hi)
    have hh := ht (act s.aps.symm i); simp only [act_inverse'] at hh
    rw [hh]; exact h _ hm
theorem orbitUpdate_symmetric (K : Nat) (side : Bool) (P : SweepPlan A side)
    (O : InputOrbit A) (ho : O ∈ P.orbits) (X : Region n K)
    (hx : Symmetric P.generators X) : Symmetric P.generators (orbitUpdate A K side O X) := by
  rw [orbitUpdate_exact K side O X (fun s hs => generated_invariant (P.member_generated O ho s hs) hx)]
  intro s hs; exact plainOrbit_invariant K side O X s (hx s hs) (P.orbit_closed O ho s hs)
theorem sweep_symmetric (K : Nat) (side : Bool) (P : SweepPlan A side) (X : Region n K)
    (hx : Symmetric P.generators X) : Symmetric P.generators (sweep A K side P X) :=
  fold_preserves P.orbits (fun O Y => orbitUpdate A K side O Y) (Symmetric P.generators)
    (fun O ho Y hy => orbitUpdate_symmetric K side P O ho Y hy) X hx
theorem orbit_preserves_winner (K : Nat) (side : Bool) (O : InputOrbit A)
    (X : Region n K) (contains : Subset (final A K side) X) :
    Subset (final A K side) (orbitUpdate A K side O X) := by
  rw [orbitUpdate, fold_meets]; intro f hf -- One snapshot update preserves all winners; equivariant_k_bounded_safety_aut.hh:727–744.
  apply Bool.and_eq_true_iff.mpr; refine ⟨contains f hf, List.all_eq_true.mpr ?_⟩
  intro s _; change computeT A K side X O.representative (act s.states.symm f) = true
  apply computeT_mono A K side contains; have hw : final A K side (act s.states.symm f) = true := by
    have hi := final_invariant s K side (act s.states.symm f); simpa [hf] using hi.symm
  have hc : cpre A K side (final A K side) (act s.states.symm f) = true := by
    rw [final_fixed]; exact hw
  exact (computeT_iff _ _ _ _ _ _).mpr (((cpre_iff _ _ _ _ _).mp hc).2 O.representative)
-- All in-place orbit updates preserve winners; equivariant_k_bounded_safety_aut.hh:721–774.
theorem sweep_preserves_winner (K : Nat) (side : Bool) (P : SweepPlan A side)
    (X : Region n K) (hx : Subset (final A K side) X) : Subset (final A K side) (sweep A K side P X) :=
  fold_preserves P.orbits (fun O Y => orbitUpdate A K side O Y) (Subset (final A K side))
    (fun O _ Y hy => orbit_preserves_winner K side O Y hy) X hx
def sweepFinal (A : Automaton n a) (K : Nat) (side : Bool) (P : SweepPlan A side) : Region n K :=
  iterate (sweep A K side P) (safe A K) (states n K).length
theorem sweep_fixed_cpre (K : Nat) (side : Bool) (P : SweepPlan A side) (X : Region n K)
    (hx : Symmetric P.generators X) (hf : sweep A K side P X = X) : cpre A K side X = X := by
  apply subset_antisymm (cpre_shrinks A K side X); intro f hxf
  apply (cpre_iff _ _ _ _ _).mpr; refine ⟨hxf, fun i => ?_⟩
  obtain ⟨O, ho, hi⟩ := P.covers i
  obtain ⟨s, hs, he⟩ := List.mem_map.mp hi
  have hO := fold_fixed P.orbits (fun O Y => orbitUpdate A K side O Y)
    (orbitUpdate_shrinks K side) X hf O ho
  rw [orbitUpdate_exact K side O X (fun s hs => generated_invariant (P.member_generated O ho s hs) hx)] at hO
  have hm : plainOrbit A K side O X f = true := by rw [hO]; exact hxf
  have ht := List.all_eq_true.mp (Bool.and_eq_true_iff.mp hm).2 s hs
  have hp : computeT A K side X (opponentProjection A side i) f = true := by
    rw [← he, computeT_projected]; exact ht
  rw [computeT_projected] at hp; exact (computeT_iff _ _ _ _ _ _).mp hp
-- The snapshot sweep returns the plain greatest fixed point; equivariant_k_bounded_safety_aut.hh:721–774.
theorem sweepFinal_eq (K : Nat) (side : Bool) (P : SweepPlan A side) :
    sweepFinal A K side P = final A K side := by
  apply iterate_eq_final K side _ (sweep_shrinks K side P) (sweep_preserves_winner K side P); intro k
  exact sweep_fixed_cpre K side P _ (iterate_preserves _ (Symmetric P.generators)
    (fun X hx => sweep_symmetric K side P X hx) _ (fun s _ => safe_commutes s K) k)

end Symmetry

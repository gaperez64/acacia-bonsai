import SymmetryProofs.FixedPoint

namespace Symmetry
variable {A : Automaton n a}
def Verified.identity (A : Automaton n a) : Verified A where
  states := Perm.id -- Identity is a valid first transport; equivariant_k_bounded_safety_aut.hh:731–736.
  aps := Perm.id
  init_fixed := rfl
  acceptance _ := rfl
  partition _ := rfl
  caps _ := rfl
  forward e he := ⟨e, he, rfl, rfl, fun _ => rfl⟩
def Verified.comp (s t : Verified A) : Verified A where -- Compose verified state and AP maps; symmetry_blocks.hh:274–302.
  states := s.states.comp t.states
  aps := s.aps.comp t.aps
  init_fixed := by simp [Perm.comp, s.init_fixed, t.init_fixed]
  acceptance q := (s.acceptance (t.states q)).trans (t.acceptance q)
  partition p := (s.partition (t.aps p)).trans (t.partition p)
  caps q := (s.caps (t.states q)).trans (t.caps q)
  forward e he := by
    obtain ⟨d, hd, hds, hdt, hdl⟩ := t.forward e he
    obtain ⟨f, hf, hfs, hft, hfl⟩ := s.forward d hd; exact ⟨f, hf, by simp [Perm.comp, hfs, hds],
      by simp [Perm.comp, hft, hdt], fun v => by rw [act_comp, hfl, hdl]⟩
-- Products of stored generators, including identity; equivariant_k_bounded_safety_aut.hh:558–581.
inductive GeneratedBy (G : List (Verified A)) : Perm (Fin n) → Perm (Fin a) → Prop
  | identity : GeneratedBy G Perm.id Perm.id
  | generator (s : Verified A) (h : s ∈ G) : GeneratedBy G s.states s.aps
  | comp {p q r t} : GeneratedBy G p r → GeneratedBy G q t →
      GeneratedBy G (p.comp q) (r.comp t)
theorem generated_verified {G : List (Verified A)} {p r} (h : GeneratedBy G p r) :
    ∃ s : Verified A, s.states = p ∧ s.aps = r := by
  induction h with -- Generated maps retain automorphism evidence; symmetry.hh:9–17.
  | identity => exact ⟨Verified.identity A, rfl, rfl⟩
  | generator s _ => exact ⟨s, rfl, rfl⟩
  | comp _ _ ih₁ ih₂ =>
    obtain ⟨s, hs, hi⟩ := ih₁
    obtain ⟨t, ht, hj⟩ := ih₂
    exact ⟨s.comp t, by simp only [Verified.comp, hs, ht], by simp only [Verified.comp, hi, hj]⟩
theorem generated_invariant {G : List (Verified A)} {p r} {X : Region n K}
    (h : GeneratedBy G p r) (hx : ∀ s ∈ G, Invariant s.states X) : Invariant p X := by
  induction h with -- Stored-generator invariance extends to products; symmetry.hh:14–17.
  | identity => intro f; rfl
  | generator s hs => exact hx s hs
  | comp _ _ ih₁ ih₂ => intro f; rw [act_comp, ih₁, ih₂]
theorem opponentProjection_commutes (s : Verified A) (side : Bool) (i : Fin a → Bool) :
    opponentProjection A side (act s.aps i) = act s.aps (opponentProjection A side i) := by
  funext p; have hp := s.partition (s.aps.invFun p) -- Guarded renaming preserves input projection; symmetry.hh:14–17.
  simp only [s.aps.right_inv] at hp; simp only [opponentProjection, act, hp]
structure BlockLayout (n clients blocks : Nat) where -- Block/slot table with both inverse laws; symmetry_blocks.hh:36–48,170–185.
  locate : Fin n → Option (Fin blocks × Fin clients)
  state : Fin blocks → Fin clients → Fin n
  locate_state : ∀ b j, locate (state b j) = some (b, j)
  state_locate : ∀ q b j, locate q = some (b, j) → state b j = q
def BlockLayout.phiFromSigma (L : BlockLayout n c b) (sigma : Perm (Fin c)) : Perm (Fin n) where
  toFun q := match L.locate q with -- Permute client slots; fix shared coordinates; equivariant_k_bounded_safety_aut.hh:374–383.
    | none => q
    | some (blk, j) => L.state blk (sigma j)
  invFun q := match L.locate q with
    | none => q
    | some (blk, j) => L.state blk (sigma.invFun j)
  left_inv q := by
    cases h : L.locate q with
    | none => simp [h]
    | some bj => rcases bj with ⟨blk, j⟩; simp [L.locate_state, L.state_locate q blk j h]
  right_inv q := by
    cases h : L.locate q with
    | none => simp [h]
    | some bj => rcases bj with ⟨blk, j⟩; simp [L.locate_state, L.state_locate q blk j h]
@[simp] theorem phiFromSigma_id (L : BlockLayout n c b) : L.phiFromSigma Perm.id = Perm.id := by
  apply Perm.ext; intro q
  cases h : L.locate q with
  | none => simp [BlockLayout.phiFromSigma, h, Perm.id]
  | some bj =>
    rcases bj with ⟨blk, j⟩
    simpa [BlockLayout.phiFromSigma, h, Perm.id] using L.state_locate q blk j h
theorem phiFromSigma_comp (L : BlockLayout n c b) (p q : Perm (Fin c)) :
    L.phiFromSigma (p.comp q) = (L.phiFromSigma p).comp (L.phiFromSigma q) := by
  apply Perm.ext; intro x -- Layout transport respects composition; symmetry_blocks.hh:274–302.
  cases h : L.locate x with
  | none => simp [BlockLayout.phiFromSigma, Perm.comp, h]
  | some bj => rcases bj with ⟨blk, j⟩; simp [BlockLayout.phiFromSigma, Perm.comp, h, L.locate_state]
-- Finite permutations decompose into transpositions; symmetry_blocks.hh:274–302.
theorem transpositions_generate [DecidableEq α] (U : List α) (full : ∀ x, x ∈ U)
    (P : Perm α → Prop) (hid : P Perm.id) (hswap : ∀ x y, P (transposition x y))
    (hcomp : ∀ p q, P p → P q → P (p.comp q)) (sigma : Perm α) : P sigma := by
  have go : ∀ xs : List α, ∀ p : Perm α, (∀ x, x ∉ xs → p x = x) → P p := by
    intro xs; induction xs with
    | nil =>
      intro p hp; have he : p = Perm.id := Perm.ext (fun x => hp x (by simp))
      simpa [he] using hid
    | cons x xs ih =>
      intro p hp; have hy : p x ∈ x :: xs := by
        by_cases h : p x ∈ x :: xs
        · exact h
        · have he : p x = x := p.injective (hp (p x) h)
          exact False.elim (h (by rw [he]; simp))
      let t := transposition x (p x)
      have ht : ∀ z, z ∉ xs → (t.comp p) z = z := by
        intro z hz; by_cases he : z = x
        · subst z; simp [t, Perm.comp, transposition, swap]
        · have hnot : z ∉ x :: xs := by simp [he, hz]
          have hne : z ≠ p x := fun h => hnot (h ▸ hy)
          simp [t, Perm.comp, transposition, hp z hnot, swap, he, hne]
      have he : t.comp (t.comp p) = p := by
        apply Perm.ext; intro z
        exact swap_swap x (p x) (p z)
      exact he ▸ hcomp t (t.comp p) (hswap _ _) (ih _ ht)
  exact go U sigma (fun x hx => False.elim (hx (full x)))
theorem transposition_comm [DecidableEq α] (x y : α) : transposition x y = transposition y x := by
  apply Perm.ext; intro z
  by_cases hx : z = x <;> by_cases hy : z = y <;> simp_all [transposition, swap]
theorem transposition_star [DecidableEq α] (pivot x y : α) (hx : x ≠ pivot) (hy : y ≠ pivot)
    (hne : x ≠ y) : transposition x y =
      (transposition pivot x).comp ((transposition pivot y).comp (transposition pivot x)) := by
  apply Perm.ext; intro z
  by_cases hp : z = pivot <;> by_cases hzx : z = x <;> by_cases hzy : z = y <;>
    simp_all [transposition, swap, Perm.comp, Ne.symm hne]
-- Stored star swaps generate every layout transport; symmetry_blocks.hh:274–302.
theorem phiFromSigma_generated (S : BlockLayout n c bs) (I : BlockLayout a c bi)
    (G : List (Verified A)) (pivot : Fin c)
    (stars : ∀ j, j ≠ pivot → ∃ s ∈ G, s.states = S.phiFromSigma (transposition pivot j) ∧
      s.aps = I.phiFromSigma (transposition pivot j)) (sigma : Perm (Fin c)) :
    GeneratedBy G (S.phiFromSigma sigma) (I.phiFromSigma sigma) := by
  have hid : GeneratedBy G (S.phiFromSigma Perm.id) (I.phiFromSigma Perm.id) := by
    simpa using (GeneratedBy.identity (G := G))
  have hc : ∀ p q, GeneratedBy G (S.phiFromSigma p) (I.phiFromSigma p) →
      GeneratedBy G (S.phiFromSigma q) (I.phiFromSigma q) →
      GeneratedBy G (S.phiFromSigma (p.comp q)) (I.phiFromSigma (p.comp q)) := by
    intro p q hp hq; simpa only [phiFromSigma_comp] using GeneratedBy.comp hp hq
  have hs : ∀ j, j ≠ pivot → GeneratedBy G (S.phiFromSigma (transposition pivot j))
        (I.phiFromSigma (transposition pivot j)) := by
    intro j hj; obtain ⟨s, hm, hS, hI⟩ := stars j hj
    rw [← hS, ← hI]; exact .generator s hm
  apply transpositions_generate (List.finRange c) List.mem_finRange _ hid _ hc sigma; intro x y
  by_cases he : x = y
  · subst y
    have he : transposition x x = Perm.id := by
      apply Perm.ext; intro z
      by_cases h : z = x <;> simp [transposition, swap, Perm.id, h]
    simpa only [he] using hid
  by_cases hx : x = pivot
  · subst x; exact hs y (Ne.symm he)
  by_cases hy : y = pivot
  · subst y; rw [transposition_comm]; exact hs x hx
  rw [transposition_star pivot x y hx hy he]; exact hc _ _ (hs x hx) (hc _ _ (hs y hy) (hs x hx))

end Symmetry

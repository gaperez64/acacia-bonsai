import SymmetryProofs.FixedPoint

namespace Symmetry
variable {A : Automaton n a}

/-- C++: equivariant_k_bounded_safety_aut.hh:731–736. The first slot matching
    is identity, including on shared states/APs. -/
def Verified.identity (A : Automaton n a) : Verified A where
  states := Perm.id
  aps := Perm.id
  init_fixed := rfl
  acceptance _ := rfl
  partition _ := rfl
  caps _ := rfl
  forward e he := ⟨e, he, rfl, rfl, fun _ => rfl⟩

/-- C++: symmetry_blocks.hh:274–302. Compositions of verified swaps remain
    verified automorphisms; the C++ uses their layout-induced state maps. -/
def Verified.comp (s t : Verified A) : Verified A where
  states := s.states.comp t.states
  aps := s.aps.comp t.aps
  init_fixed := by simp [Perm.comp, s.init_fixed, t.init_fixed]
  acceptance q := (s.acceptance (t.states q)).trans (t.acceptance q)
  partition p := (s.partition (t.aps p)).trans (t.partition p)
  caps q := (s.caps (t.states q)).trans (t.caps q)
  forward e he := by
    obtain ⟨d, hd, hds, hdt, hdl⟩ := t.forward e he
    obtain ⟨f, hf, hfs, hft, hfl⟩ := s.forward d hd
    exact ⟨f, hf, by simp [Perm.comp, hfs, hds],
      by simp [Perm.comp, hft, hdt], fun v => by rw [act_comp, hfl, hdl]⟩

/-- C++: equivariant_k_bounded_safety_aut.hh:558–581. Products use only the
    stored generators; they do not enlarge the list used by `close`. -/
inductive GeneratedBy (G : List (Verified A)) : Perm (Fin n) → Perm (Fin a) → Prop
  | identity : GeneratedBy G Perm.id Perm.id
  | generator (s : Verified A) (h : s ∈ G) : GeneratedBy G s.states s.aps
  | comp {p q r t} : GeneratedBy G p r → GeneratedBy G q t →
      GeneratedBy G (p.comp q) (r.comp t)

/-- Generation evidence can itself supply the verified certificate for a
    layout-induced transport. Soundness note symmetry.hh:9–17 and #331. -/
theorem generated_verified {G : List (Verified A)} {p r} (h : GeneratedBy G p r) :
    ∃ s : Verified A, s.states = p ∧ s.aps = r := by
  induction h with
  | identity => exact ⟨Verified.identity A, rfl, rfl⟩
  | generator s _ => exact ⟨s, rfl, rfl⟩
  | comp _ _ ih₁ ih₂ =>
    obtain ⟨s, hs, hi⟩ := ih₁
    obtain ⟨t, ht, hj⟩ := ih₂
    exact ⟨s.comp t, by simp only [Verified.comp, hs, ht],
      by simp only [Verified.comp, hi, hj]⟩

/-- Invariance under stored swaps extends to their compositions, as required
    by symmetry.hh:14–17 and #331; no extra closure intersections are needed. -/
theorem generated_invariant {G : List (Verified A)} {p r} {X : Region n K}
    (h : GeneratedBy G p r) (hx : ∀ s ∈ G, Invariant s.states X) : Invariant p X := by
  induction h with
  | identity => intro f; rfl
  | generator s hs => exact hx s hs
  | comp _ _ ih₁ ih₂ => intro f; rw [act_comp, ih₁, ih₂]

/-- Renaming an engine-input cube stays on the opponent side because
    the guard preserves I/O; symmetry.hh:9–17 and #331. -/
theorem opponentProjection_commutes (s : Verified A) (side : Bool) (i : Fin a → Bool) :
    opponentProjection A side (act s.aps i) = act s.aps (opponentProjection A side i) := by
  funext p
  have hp := s.partition (s.aps.invFun p)
  simp only [s.aps.right_inv] at hp
  simp only [opponentProjection, act, hp]

/-- C++: symmetry_blocks.hh:36–48,170–185. A valid block/slot table, with
    inverse lookup; states outside the blocks are shared. Construction/checking
    of these tables is a separate correspondence obligation. -/
structure BlockLayout (n clients blocks : Nat) where
  locate : Fin n → Option (Fin blocks × Fin clients)
  state : Fin blocks → Fin clients → Fin n
  locate_state : ∀ b j, locate (state b j) = some (b, j)
  state_locate : ∀ q b j, locate q = some (b, j) → state b j = q

/-- C++: equivariant_k_bounded_safety_aut.hh:374–383. Initialize to identity,
    then send block[b][j] to block[b][sigma[j]]. Also used for indexed AP families
    (symmetry.hh:260–264); APs outside the families remain fixed. -/
def BlockLayout.phiFromSigma (L : BlockLayout n c b) (sigma : Perm (Fin c)) : Perm (Fin n) where
  toFun q := match L.locate q with
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
  apply Perm.ext
  intro q
  cases h : L.locate q with
  | none => simp [BlockLayout.phiFromSigma, h, Perm.id]
  | some bj =>
    rcases bj with ⟨blk, j⟩
    simpa [BlockLayout.phiFromSigma, h, Perm.id] using L.state_locate q blk j h

/-- Layout transport respects composition: the C++ `phi_from_sigma` of a
    product is the product of the stored state swaps. symmetry.hh:14–17, #331. -/
theorem phiFromSigma_comp (L : BlockLayout n c b) (p q : Perm (Fin c)) :
    L.phiFromSigma (p.comp q) = (L.phiFromSigma p).comp (L.phiFromSigma q) := by
  apply Perm.ext
  intro x
  cases h : L.locate x with
  | none => simp [BlockLayout.phiFromSigma, Perm.comp, h]
  | some bj => rcases bj with ⟨blk, j⟩; simp [BlockLayout.phiFromSigma, Perm.comp, h, L.locate_state]

/-- Finite permutations are products of transpositions. The support shrinks
    by fixing its first element at each induction step. -/
theorem transpositions_generate [DecidableEq α] (U : List α) (full : ∀ x, x ∈ U)
    (P : Perm α → Prop) (hid : P Perm.id) (hswap : ∀ x y, P (transposition x y))
    (hcomp : ∀ p q, P p → P q → P (p.comp q)) (sigma : Perm α) : P sigma := by
  have go : ∀ xs : List α, ∀ p : Perm α, (∀ x, x ∉ xs → p x = x) → P p := by
    intro xs
    induction xs with
    | nil =>
      intro p hp
      have he : p = Perm.id := Perm.ext (fun x => hp x (by simp))
      simpa [he] using hid
    | cons x xs ih =>
      intro p hp
      have hy : p x ∈ x :: xs := by
        by_cases h : p x ∈ x :: xs
        · exact h
        · have he : p x = x := p.injective (hp (p x) h)
          exact False.elim (h (by rw [he]; simp))
      let t := transposition x (p x)
      have ht : ∀ z, z ∉ xs → (t.comp p) z = z := by
        intro z hz
        by_cases he : z = x
        · subst z; simp [t, Perm.comp, transposition, swap]
        · have hnot : z ∉ x :: xs := by simp [he, hz]
          have hne : z ≠ p x := fun h => hnot (h ▸ hy)
          simp [t, Perm.comp, transposition, hp z hnot, swap, he, hne]
      have he : t.comp (t.comp p) = p := by
        apply Perm.ext
        intro z
        exact swap_swap x (p x) (p z)
      exact he ▸ hcomp t (t.comp p) (hswap _ _) (ih _ ht)
  exact go U sigma (fun x hx => False.elim (hx (full x)))

theorem transposition_comm [DecidableEq α] (x y : α) :
    transposition x y = transposition y x := by
  apply Perm.ext
  intro z
  by_cases hx : z = x <;> by_cases hy : z = y <;> simp_all [transposition, swap]

theorem transposition_star [DecidableEq α] (pivot x y : α) (hx : x ≠ pivot) (hy : y ≠ pivot)
    (hne : x ≠ y) : transposition x y =
      (transposition pivot x).comp ((transposition pivot y).comp (transposition pivot x)) := by
  apply Perm.ext
  intro z
  by_cases hp : z = pivot <;> by_cases hzx : z = x <;> by_cases hzy : z = y <;>
    simp_all [transposition, swap, Perm.comp, Ne.symm hne]

/-- Every slot permutation induces a composition of the ORIGINAL stored star
    swaps, if the stored swaps agree with the layout. This proves the claim at
    symmetry_blocks.hh:274–302 and symmetry.hh:14–17 needed by #331. No orbit
    transport has to occur literally in the closure generator list. -/
theorem phiFromSigma_generated (S : BlockLayout n c bs) (I : BlockLayout a c bi)
    (G : List (Verified A)) (pivot : Fin c)
    (stars : ∀ j, j ≠ pivot → ∃ s ∈ G,
      s.states = S.phiFromSigma (transposition pivot j) ∧
      s.aps = I.phiFromSigma (transposition pivot j)) (sigma : Perm (Fin c)) :
    GeneratedBy G (S.phiFromSigma sigma) (I.phiFromSigma sigma) := by
  have hid : GeneratedBy G (S.phiFromSigma Perm.id) (I.phiFromSigma Perm.id) := by
    simpa using (GeneratedBy.identity (G := G))
  have hc : ∀ p q, GeneratedBy G (S.phiFromSigma p) (I.phiFromSigma p) →
      GeneratedBy G (S.phiFromSigma q) (I.phiFromSigma q) →
      GeneratedBy G (S.phiFromSigma (p.comp q)) (I.phiFromSigma (p.comp q)) := by
    intro p q hp hq
    simpa only [phiFromSigma_comp] using GeneratedBy.comp hp hq
  have hs : ∀ j, j ≠ pivot →
      GeneratedBy G (S.phiFromSigma (transposition pivot j))
        (I.phiFromSigma (transposition pivot j)) := by
    intro j hj
    obtain ⟨s, hm, hS, hI⟩ := stars j hj
    rw [← hS, ← hI]
    exact .generator s hm
  apply transpositions_generate (List.finRange c) List.mem_finRange _ hid _ hc sigma
  intro x y
  by_cases he : x = y
  · subst y
    have he : transposition x x = Perm.id := by
      apply Perm.ext
      intro z
      by_cases h : z = x <;> simp [transposition, swap, Perm.id, h]
    simpa only [he] using hid
  by_cases hx : x = pivot
  · subst x; exact hs y (Ne.symm he)
  by_cases hy : y = pivot
  · subst y; rw [transposition_comm]; exact hs x hx
  rw [transposition_star pivot x y hx hy he]
  exact hc _ _ (hs x hx) (hc _ _ (hs y hy) (hs x hx))

end Symmetry

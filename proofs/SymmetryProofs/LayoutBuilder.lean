import SymmetryProofs.Enumeration

namespace Symmetry.Builders
def findRoot (parent : Fin n → Fin n) : Nat → Fin n → Fin n
  | 0, q => q
  | fuel + 1, q => if parent q = q then q else findRoot parent fuel (parent q)
-- Unite generator edges and look up state roots; symmetry_blocks.hh:52–70.
def orbitRoots (generators : List (Perm (Fin n))) : Fin n → Fin n :=
  let parent := generators.foldl (fun (parent : Vector (Fin n) n) g =>
    (List.finRange n).foldl (fun parent q =>
      let a := findRoot parent.get n q
      let b := findRoot parent.get n (g q)
      parent.set a.val b a.isLt) parent) (Vector.ofFn fun q => q)
  findRoot parent.get n
-- Any root map partitions states without duplicates; symmetry_blocks.hh:90–114.
def buckets (root : Fin n → Fin n) : List (List (Fin n)) :=
  (List.finRange n).map fun r => (List.finRange n).filter fun q => root q == r
private theorem bucket_unique (states roots : List α) [DecidableEq α]
    (root : α → α) (hs : states.Nodup) (hr : roots.Nodup) :
    (roots.flatMap (fun r => states.filter fun q => root q == r)).Nodup := by
  induction roots with
  | nil => simp
  | cons r rs ih =>
    obtain ⟨hn, hu⟩ := List.nodup_cons.mp hr
    apply List.nodup_append.mpr; refine ⟨hs.filter _, ih hu, ?_⟩
    intro x hx y hy he; subst y
    obtain ⟨_, hx⟩ := List.mem_filter.mp hx
    obtain ⟨r', hr', hxr⟩ := List.mem_flatMap.mp hy
    have he := (beq_iff_eq.mp hx).symm.trans (beq_iff_eq.mp (List.mem_filter.mp hxr).2); exact hn (he ▸ hr')
theorem buckets_unique (root : Fin n → Fin n) : (buckets root).flatten.Nodup := by exact bucket_unique _ _ root (List.nodup_finRange _) (List.nodup_finRange _)
private theorem filter_rows_unique (rows : List (List α)) (p : List α → Bool)
    (h : rows.flatten.Nodup) : (rows.filter p).flatten.Nodup := by
  apply List.Pairwise.sublist _ h
  clear h
  induction rows with
  | nil => simp
  | cons row rows ih =>
    by_cases hp : p row = true <;> simp [hp, List.flatten_cons, ih]
def signature (generators : List (Perm (Fin n))) (q : Fin n) : Nat :=
  (BitVec.ofBoolListLE (generators.map fun g => decide (g q = q))).toNat
def recognizeBlock (reference : Fin clients → Nat) (sig : Fin n → Nat)
    (members : List (Fin n)) : Option (Fin clients → Fin n) :=
  match h : matchEntries (List.ofFn reference) (members.map fun q => (q, sig q)) with
  | none => none
  | some ordered => some (fun j => (ordered[j.val]'(by
      have := congrArg List.length (matchEntries_exact _ _ h).2
      simp only [List.length_map, List.length_ofFn] at this; omega)).1)
theorem recognizeBlock_exact (reference : Fin clients → Nat) (sig : Fin n → Nat)
    (members : List (Fin n)) (h : recognizeBlock reference sig members = some row) :
    (List.ofFn row).Perm members := by
  unfold recognizeBlock at h; split at h
  · contradiction
  next ordered he =>
    simp only [Option.some.injEq] at h; subst row
    have hp := ((matchEntries_exact _ _ he).1).map Prod.fst; simp only [List.map_map] at hp
    have hl : ordered.length = clients := by
      have := congrArg List.length (matchEntries_exact _ _ he).2; simpa using this
    have heq : List.ofFn (fun j : Fin clients => (ordered[j.val]'(by omega)).1) = ordered.map Prod.fst := by
      apply List.ext_getElem
      · simp [hl]
      · intro i h₁ h₂; simp
    simpa [heq, Function.comp_def] using hp
-- Recover each block by stable signature matching; symmetry_blocks.hh:170–185.
def recognizeBlocks (reference : Fin clients → Nat) (sig : Fin n → Nat) :
    List (List (Fin n)) → Option (List (Fin clients → Fin n))
  | [] => some []
  | members :: rest => do
      let row ← recognizeBlock reference sig members
      let rows ← recognizeBlocks reference sig rest
      pure (row :: rows)
theorem recognizeBlocks_exact (reference : Fin clients → Nat) (sig : Fin n → Nat)
    (members : List (List (Fin n)))
    (h : recognizeBlocks reference sig members = some rows) :
    (rows.flatMap List.ofFn).Perm members.flatten := by
  induction members generalizing rows with -- Recovered rows contain precisely the original block cells; symmetry_blocks.hh:170–185.
  | nil => simp [recognizeBlocks] at h; subst rows; exact .refl _
  | cons qs rest ih =>
    cases hb : recognizeBlock reference sig qs with
    | none => simp [recognizeBlocks, hb] at h
    | some row =>
      cases hr : recognizeBlocks reference sig rest with
      | none => simp [recognizeBlocks, hb, hr] at h
      | some tail =>
        simp [recognizeBlocks, hb, hr] at h
        subst rows; exact (recognizeBlock_exact reference sig qs hb).append (ih hr)
private theorem nodup_map_injective [DecidableEq β] (xs : List α) (f : α → β)
    (h : (xs.map f).Nodup) (x y : α) (hx : x ∈ xs) (hy : y ∈ xs) (he : f x = f y) : x = y := by
  obtain ⟨i, hi, rfl⟩ := List.getElem_of_mem hx
  obtain ⟨j, hj, rfl⟩ := List.getElem_of_mem hy
  have hij : i = j := h.eq_of_getElem_eq (by simpa using hi) (by simpa using hj)
    (by simpa only [List.getElem_map] using he)
  subst j; rfl
def cells (blocks clients : Nat) : List (Fin blocks × Fin clients) :=
  (List.finRange blocks).flatMap fun b => (List.finRange clients).map fun j => (b, j)
theorem mem_cells (bj : Fin blocks × Fin clients) : bj ∈ cells blocks clients := by
  exact List.mem_flatMap.mpr ⟨bj.1, List.mem_finRange _, List.mem_map.mpr ⟨bj.2, List.mem_finRange _, rfl⟩⟩
-- Distinct forward cells construct both inverse lookup laws; symmetry_blocks.hh:180–185.
def layoutOfRows (rows : List (Fin clients → Fin n))
    (unique : (rows.flatMap List.ofFn).Nodup) : BlockLayout n clients rows.length := by
  let state := fun bj : Fin rows.length × Fin clients => rows[bj.1.val] bj.2
  have he : (cells rows.length clients).map state = rows.flatMap List.ofFn := by
    have hm : (List.finRange rows.length).map (fun b => rows[b.val]) = rows := by
      simp [List.finRange, Function.comp_def, List.ofFn_getElem]
    calc
      _ = (List.finRange rows.length).flatMap (fun b => List.ofFn (rows[b.val])) := by
        simp [cells, List.map_flatMap, List.finRange, Function.comp_def, state]
      _ = ((List.finRange rows.length).map (fun b => rows[b.val])).flatMap List.ofFn :=
        (List.flatMap_map _ _ _).symm
      _ = _ := by rw [hm]
  have injective : ∀ x y, state x = state y → x = y := by
    intro x y h; exact nodup_map_injective (cells _ _) state (he ▸ unique) x y (mem_cells _) (mem_cells _) h
  let locate := fun q => (cells rows.length clients).find? fun bj => state bj == q
  have found : ∀ q bj, locate q = some bj → state bj = q := by
    intro q bj h; exact beq_iff_eq.mp (List.find?_some (p := fun bj => state bj == q) h)
  refine ⟨locate, fun b j => state (b, j), ?_, ?_⟩
  · intro b j
    cases h : locate (state (b, j)) with
    | none =>
      have hn := List.find?_eq_none.mp h (b, j) (mem_cells _); simp at hn
    | some bj =>
      have heq := injective bj (b, j) (found _ _ h); simp [heq]
  · intro q b j h
    exact found q (b, j) h
theorem layout_ext (L M : BlockLayout n clients blocks)
    (hs : ∀ b j, L.state b j = M.state b j) : L = M := by
  have stateEq : L.state = M.state := funext fun b => funext (hs b)
  have locateEq : L.locate = M.locate := by
    funext q; apply Option.ext
    intro (b, j)
    constructor <;> intro h
    · rw [← L.state_locate q b j h, hs b j, M.locate_state]
    · rw [← M.state_locate q b j h, ← hs b j, L.locate_state]
  cases L; cases M; cases locateEq; cases stateEq; rfl
def recognizeLayout (generators : List (Perm (Fin n))) (reference : Fin clients → Nat) :
    Option (Sigma fun blocks => BlockLayout n clients blocks) :=
  let all := buckets (orbitRoots generators)
  let blocks := all.filter fun members => members.length == clients
  if clients < 3 || blocks.length < 2 ||
      !(all.all fun members => members.isEmpty || members.length == 1 || members.length == clients) then none
  else match h : recognizeBlocks reference (signature generators) blocks with
    | none => none
    | some rows => some ⟨rows.length, layoutOfRows rows (by
        have hu := filter_rows_unique all (fun members => members.length == clients)
          (buckets_unique _)
        exact (recognizeBlocks_exact reference (signature generators) blocks h).nodup_iff.mpr hu)⟩
structure AlignedLayout (n clients : Nat) where
  blocks : Nat
  states : BlockLayout n clients blocks
  slotOfIndex : Perm (Fin clients)
-- Recover state slots and reconcile AP-index signatures; symmetry_blocks.hh:128–187.
def computeBlockLayout (generators : List (Perm (Fin n))) (indexSig : Fin clients → Nat) :
    Option (AlignedLayout n clients) :=
  let blocks := (buckets (orbitRoots generators)).filter fun row => row.length == clients
  match hh : blocks.head? with
  | none => none
  | some reference =>
      have hl : reference.length = clients := by
        have hm : reference ∈ blocks := List.mem_of_head? hh; exact beq_iff_eq.mp (List.mem_filter.mp hm).2
      let refSig := fun j : Fin clients => signature generators (reference[j.val]'(by omega))
      if ¬(List.ofFn refSig).Nodup ∨ ¬(List.ofFn indexSig).Nodup then none
      else do
        let sigma ← matchSlots indexSig refSig
        let table ← recognizeLayout generators refSig
        pure ⟨table.1, table.2, sigma⟩
-- Express AP families in recovered state-slot order; symmetry_blocks.hh:148–181.
def reindexLayout (L : BlockLayout n clients blocks) (sigma : Perm (Fin clients)) :
    BlockLayout n clients blocks where
  locate q := (L.locate q).map fun (b, j) => (b, sigma j)
  state b j := L.state b (sigma.invFun j)
  locate_state b j := by simp [L.locate_state]
  state_locate q b j h := by
    cases he : L.locate q with
    | none => simp [he] at h
    | some pair =>
      rcases pair with ⟨blk, slot⟩
      simp [he] at h
      obtain ⟨rfl, rfl⟩ := h; simpa using L.state_locate q blk slot he
theorem reindexLayout_id (L : BlockLayout n clients blocks) : reindexLayout L Perm.id = L := by
  apply layout_ext; intro b j
  rfl
theorem perm_swap (sigma : Perm α) [DecidableEq α] (x y z : α) :
    sigma (swap x y z) = swap (sigma x) (sigma y) (sigma z) := by
  by_cases hx : z = x
  · simp [swap, hx]
  by_cases hy : z = y
  · have hxy : y ≠ x := by simpa [hy] using hx
    have hs : sigma y ≠ sigma x := fun h => hxy (sigma.injective h); simp only [swap, hy, hxy, hs, ↓reduceIte]
  · have hsx : sigma z ≠ sigma x := fun h => hx (sigma.injective h)
    have hsy : sigma z ≠ sigma y := fun h => hy (sigma.injective h)
    simp only [swap, hx, hy, hsx, hsy, ↓reduceIte]
theorem reindexLayout_swap (L : BlockLayout n clients blocks) (sigma : Perm (Fin clients))
    (x y : Fin clients) : (reindexLayout L sigma).phiFromSigma (transposition (sigma x) (sigma y)) =
      L.phiFromSigma (transposition x y) := by
  apply Perm.ext; intro q -- Slot renaming preserves the original transpositions; symmetry_blocks.hh:148–181.
  cases hl : L.locate q with
  | none => simp [BlockLayout.phiFromSigma, reindexLayout, hl]
  | some pair =>
    rcases pair with ⟨b, j⟩
    simp only [BlockLayout.phiFromSigma, reindexLayout, hl, Option.map_some, transposition]
    have he := congrArg sigma.invFun (perm_swap sigma x y j)
    simpa only [sigma.left_inv] using congrArg (L.state b) he.symm

end Symmetry.Builders

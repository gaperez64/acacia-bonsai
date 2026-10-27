import SymmetryProofs.Transports

namespace Symmetry.Builders
-- Ascending count distributions; the last count is forced; equivariant_k_bounded_safety_aut.hh:123–148.
def typeCounts : Nat → Nat → List (List Nat)
  | 0, remaining => if remaining = 0 then [[]] else []
  | 1, remaining => [[remaining]]
  | types + 2, remaining => (List.range (remaining + 1)).flatMap fun count => (typeCounts (types + 1) (remaining - count)).map (count :: ·)
-- Count recursion enumerates precisely the requested sum; equivariant_k_bounded_safety_aut.hh:123–148.
theorem mem_typeCounts (types remaining : Nat) (counts : List Nat) :
    counts ∈ typeCounts types remaining ↔ counts.length = types ∧ counts.sum = remaining := by
  induction types generalizing remaining counts with
  | zero => cases counts <;> simp [typeCounts] <;> omega
  | succ types ih =>
    cases types with
    | zero =>
      cases counts with
      | nil => simp [typeCounts]
      | cons c cs => cases cs <;> simp [typeCounts, eq_comm]
    | succ types =>
      cases counts with
      | nil => simp [typeCounts]
      | cons c cs =>
        simp only [typeCounts, List.mem_flatMap, List.mem_range, List.mem_map,
          List.cons.injEq, ih, List.length_cons, List.sum_cons]
        constructor
        · rintro ⟨_, hc, _, ⟨hl, hs⟩, rfl, rfl⟩; omega
        · rintro ⟨hl, hs⟩
          exact ⟨c, by omega, cs, ⟨by omega, by omega⟩, rfl, rfl⟩
def expandCounts : List Nat → List Nat → List Nat
  | t :: ts, c :: cs => List.replicate c t ++ expandCounts ts cs
  | _, _ => []
theorem expandCounts_length (types counts : List Nat) (h : types.length = counts.length) :
    (expandCounts types counts).length = counts.sum := by
  induction types generalizing counts with
  | nil => cases counts <;> simp_all [expandCounts]
  | cons t ts ih => cases counts <;> simp_all [expandCounts]
theorem mem_expandCounts (types counts : List Nat) (x : Nat)
    (h : x ∈ expandCounts types counts) : x ∈ types := by
  induction types generalizing counts with
  | nil => simp [expandCounts] at h
  | cons t ts ih =>
    cases counts with
    | nil => simp [expandCounts] at h
    | cons c cs =>
      rcases List.mem_append.mp h with h | h
      · exact List.mem_cons.mpr (.inl (List.eq_of_mem_replicate h))
      · exact List.mem_cons.mpr (.inr (ih cs h))
theorem expandCounts_sorted (types counts : List Nat) (h : types.Pairwise (· ≤ ·)) :
    (expandCounts types counts).Pairwise (· ≤ ·) := by
  induction types generalizing counts with
  | nil => simp [expandCounts]
  | cons t ts ih =>
    cases counts with
    | nil => simp [expandCounts]
    | cons c cs =>
      obtain ⟨ht, hs⟩ := List.pairwise_cons.mp h
      apply List.pairwise_append.mpr; refine ⟨by simp, ih cs hs, ?_⟩
      intro x hx y hy; have he := List.eq_of_mem_replicate hx
      subst x; exact ht y (mem_expandCounts ts cs y hy)
private theorem histogram_count (types word : List Nat) (unique : types.Nodup) (x : Nat) :
    (types.flatMap (fun t => List.replicate (word.count t) t)).count x =
      if x ∈ types then word.count x else 0 := by
  induction types with
  | nil => simp
  | cons t ts ih =>
    have hu := List.nodup_cons.mp unique
    rw [List.flatMap_cons, List.count_append, ih hu.2]; by_cases he : t = x
    · subst t; simp [hu.1]
    · simp [List.count_replicate, he, Ne.symm he]
theorem histogram_perm (types word : List Nat) (unique : types.Nodup)
    (covered : ∀ x ∈ word, x ∈ types) :
    (types.flatMap (fun t => List.replicate (word.count t) t)).Perm word := by
  apply List.perm_iff_count.mpr; intro x
  rw [histogram_count types word unique]; by_cases h : x ∈ types
  · simp [h]
  · have hx : x ∉ word := fun hx => h (covered x hx)
    simp [h, List.count_eq_zero.mpr hx]
theorem expandCounts_histogram (types word : List Nat) : expandCounts types (types.map word.count) =
      types.flatMap (fun t => List.replicate (word.count t) t) := by induction types <;> simp_all [expandCounts]
-- Every type word has an enumerated canonical multiset; equivariant_k_bounded_safety_aut.hh:253–256.
theorem counts_cover (types clients : Nat) (word : List Nat)
    (length : word.length = clients) (bounded : ∀ x ∈ word, x < types) : ∃ counts ∈ typeCounts types clients,
      (expandCounts (List.range types) counts).Perm word := by
  let counts := (List.range types).map word.count
  have hp : (expandCounts (List.range types) counts).Perm word := by
    rw [expandCounts_histogram]; exact histogram_perm _ _ List.nodup_range (by simpa using bounded)
  refine ⟨counts, (mem_typeCounts _ _ _).mpr ⟨by simp [counts], ?_⟩, hp⟩
  rw [← expandCounts_length (List.range types) counts (by simp [counts]), hp.length_eq, length]
-- Pop the first remaining slot with this type; equivariant_k_bounded_safety_aut.hh:362–369.
def takeType (type : Nat) (entries : List (α × Nat)) : Option ((α × Nat) × List (α × Nat)) :=
  takeResponse (fun e => decide (e.2 = type)) entries
theorem takeType_none (type : Nat) (entries : List (α × Nat)) :
    takeType type entries = none ↔ type ∉ entries.map Prod.snd := by
  simp only [takeType, takeResponse_none, decide_eq_false_iff_not, List.mem_map, not_exists, not_and]
theorem takeType_exact (type : Nat) (entries : List (α × Nat))
    (h : takeType type entries = some (found, rest)) : found.2 = type ∧ (found :: rest).Perm entries := by
  obtain ⟨hf, hp⟩ := takeResponse_exact _ _ h; exact ⟨of_decide_eq_true hf, hp⟩
-- Stable type matching consumes each slot once; equivariant_k_bounded_safety_aut.hh:370–371.
def matchEntries : List Nat → List (α × Nat) → Option (List (α × Nat))
  | [], [] => some []
  | [], _ :: _ => none
  | type :: types, entries => do
      let (found, rest) ← takeType type entries
      let tail ← matchEntries types rest
      pure (found :: tail)
theorem matchEntries_complete (types : List Nat) (entries : List (α × Nat))
    (h : types.Perm (entries.map Prod.snd)) : ∃ ordered, matchEntries types entries = some ordered ∧
      ordered.Perm entries ∧ ordered.map Prod.snd = types := by
  induction types generalizing entries with -- Equal multisets always match completely; equivariant_k_bounded_safety_aut.hh:362–372.
  | nil =>
    have he : entries = [] := by simpa using h.length_eq.symm
    subst entries
    exact ⟨[], rfl, .refl _, rfl⟩
  | cons type types ih =>
    have hm : type ∈ entries.map Prod.snd := h.mem_iff.mp (by simp)
    have hn : takeType type entries ≠ none := by simpa [takeType_none] using hm
    cases ht : takeType type entries with
    | none => exact False.elim (hn ht)
    | some pair =>
      rcases pair with ⟨found, rest⟩
      obtain ⟨hf, hp⟩ := takeType_exact type entries ht
      have hr : types.Perm (rest.map Prod.snd) := by
        have he := h.trans (hp.map Prod.snd).symm; simpa [hf] using he
      obtain ⟨tail, htail, hperm, htypes⟩ := ih rest hr
      exact ⟨found :: tail, by simp [matchEntries, ht, htail],
        (hperm.cons found).trans hp, by simp [hf, htypes]⟩
theorem matchEntries_exact (types : List Nat) (entries : List (α × Nat))
    (h : matchEntries types entries = some ordered) :
    ordered.Perm entries ∧ ordered.map Prod.snd = types := by
  induction types generalizing entries ordered with
  | nil =>
    cases entries <;> simp [matchEntries] at h
    subst ordered; exact ⟨.refl _, rfl⟩
  | cons type types ih =>
    cases ht : takeType type entries with
    | none => simp [matchEntries, ht] at h
    | some pair =>
      rcases pair with ⟨found, rest⟩
      cases hm : matchEntries types rest with
      | none => simp [matchEntries, ht, hm] at h
      | some tail =>
        simp [matchEntries, ht, hm] at h
        subst ordered
        obtain ⟨hf, hp⟩ := takeType_exact type entries ht
        obtain ⟨htail, htypes⟩ := ih rest hm; exact ⟨(htail.cons found).trans hp, by simp [hf, htypes]⟩
-- Recover sigma and its inverse from the consumed slots; equivariant_k_bounded_safety_aut.hh:362–372.
def slotsPerm (slots : List (Fin clients)) (h : slots.Perm (List.finRange clients)) : Perm (Fin clients) where
  toFun j := slots[j.val]'(by simpa only [h.length_eq, List.length_finRange] using j.isLt)
  invFun j := ⟨slots.idxOf j, by
    have hm : j ∈ slots := h.mem_iff.mpr (List.mem_finRange j)
    have := List.idxOf_lt_length_of_mem hm
    have := h.length_eq; simp only [List.length_finRange] at this
    omega⟩
  left_inv j := by
    apply Fin.ext; have hu : slots.Nodup := h.nodup_iff.mpr (List.nodup_finRange _)
    have hj : j.val < slots.length := by simpa only [h.length_eq, List.length_finRange] using j.isLt
    have hi : slots.idxOf slots[j.val] < slots.length := List.idxOf_lt_length_of_mem (List.getElem_mem ..)
    have he := List.getElem_idxOf hi; exact hu.getElem_inj.mp he
  right_inv j := by exact List.getElem_idxOf _
-- Decline unequal multisets; otherwise construct a permutation; equivariant_k_bounded_safety_aut.hh:362–372.
def matchSlots (canonical member : Fin clients → Nat) : Option (Perm (Fin clients)) :=
  match h : matchEntries (List.ofFn canonical)
      ((List.finRange clients).map fun j => (j, member j)) with
  | none => none
  | some ordered => some (slotsPerm (ordered.map Prod.fst) (by
      have hp := ((matchEntries_exact _ _ h).1).map Prod.fst
      simpa [List.map_map, Function.comp_def] using hp))
-- Every returned sigma preserves the matched client types; equivariant_k_bounded_safety_aut.hh:362–372.
theorem matchSlots_types (canonical member : Fin clients → Nat)
    (h : matchSlots canonical member = some sigma) : ∀ j, member (sigma j) = canonical j := by
  unfold matchSlots at h; split at h
  · contradiction
  next ordered he =>
    simp only [Option.some.injEq] at h; subst sigma
    obtain ⟨hp, ht⟩ := matchEntries_exact _ _ he
    intro j; have hl : j.val < ordered.length := by
      have := hp.length_eq; simp only [List.length_map, List.length_finRange] at this
      omega
    have hm := hp.subset (List.getElem_mem (l := ordered) hl)
    obtain ⟨idx, _, hi⟩ := List.mem_map.mp hm
    have hj := congrArg (fun xs : List Nat => xs[j.val]?) ht
    simp only [List.getElem?_map, List.getElem?_eq_getElem hl, Option.map_some,
      List.getElem?_ofFn, j.isLt, ↓reduceDIte, Option.some.injEq] at hj
    have hmapped : j.val < (ordered.map Prod.fst).length := by simpa using hl
    change member ((ordered.map Prod.fst)[j.val]'hmapped) = canonical j; simp only [List.getElem_map]
    rw [← hi]; simpa [← hi] using hj
theorem matchSlots_complete (canonical member : Fin clients → Nat)
    (h : (List.ofFn canonical).Perm (List.ofFn member)) :
    ∃ sigma, matchSlots canonical member = some sigma := by
  have he : ((List.finRange clients).map fun j => (j, member j)).map Prod.snd =
      List.ofFn member := by simp [Function.comp_def, List.finRange]
  obtain ⟨ordered, ho, _, _⟩ := matchEntries_complete (List.ofFn canonical)
    ((List.finRange clients).map fun j => (j, member j)) (by rw [he]; exact h)
  unfold matchSlots; split
  · rename_i hn
    rw [ho] at hn; contradiction
  · exact ⟨_, rfl⟩

end Symmetry.Builders

import SymmetryProofs.Enumeration

namespace Symmetry.Builders
-- Find the first larger element in the reversed suffix; equivariant_k_bounded_safety_aut.hh:744.
def bumpHead (pivot : Nat) : List Nat → Option (Nat × List Nat)
  | [] => none
  | x :: xs => if pivot < x then some (x, pivot :: xs) else
      (bumpHead pivot xs).map fun (head, rest) => (head, x :: rest)
theorem bumpHead_none (pivot : Nat) (xs : List Nat) : bumpHead pivot xs = none ↔ ∀ x ∈ xs, x ≤ pivot := by
  induction xs with
  | nil => simp [bumpHead]
  | cons x xs ih => by_cases h : pivot < x <;> simp [bumpHead, h, ih] <;> omega
theorem bumpHead_exact (pivot : Nat) (xs : List Nat)
    (sorted : xs.Pairwise (· ≤ ·)) (h : bumpHead pivot xs = some (head, rest)) :
    pivot < head ∧ (head :: rest).Perm (pivot :: xs) ∧
      rest.Pairwise (· ≤ ·) ∧ (∀ x ∈ xs, pivot < x → head ≤ x) := by
  induction xs generalizing rest with
  | nil => simp [bumpHead] at h
  | cons x xs ih =>
    obtain ⟨hx, hs⟩ := List.pairwise_cons.mp sorted
    by_cases hp : pivot < x
    · simp [bumpHead, hp] at h
      obtain ⟨rfl, rfl⟩ := h
      refine ⟨hp, List.Perm.swap _ _ _, List.pairwise_cons.mpr ⟨?_, hs⟩, ?_⟩
      · intro z hz; exact Nat.le_trans (by omega) (hx z hz)
      · intro z hz _; rcases List.mem_cons.mp hz with rfl | hz
        · exact Nat.le_refl _
        · exact hx z hz
    · cases hb : bumpHead pivot xs with
      | none => simp [bumpHead, hp, hb] at h
      | some pair =>
        rcases pair with ⟨found, tail⟩
        simp [bumpHead, hp, hb] at h
        obtain ⟨rfl, rfl⟩ := h
        obtain ⟨hlt, hperm, hsort, hmin⟩ := ih hs hb
        refine ⟨hlt, (List.Perm.swap _ _ _).trans
          ((hperm.cons x).trans (List.Perm.swap _ _ _)), List.pairwise_cons.mpr ⟨?_, hsort⟩, ?_⟩
        · intro z hz
          have hm : z ∈ pivot :: xs := hperm.mem_iff.mp (by simp [hz])
          rcases List.mem_cons.mp hm with rfl | hm
          · omega
          · exact hx z hm
        · intro z hz hgt
          rcases List.mem_cons.mp hz with rfl | hz
          · omega
          · exact hmin z hz hgt
-- Rightmost ascent, pivot swap, suffix reversal; equivariant_k_bounded_safety_aut.hh:731–744.
def nextPermutation : List Nat → Option (List Nat)
  | [] => none
  | x :: xs => match nextPermutation xs with
      | some tail => some (x :: tail)
      | none => (bumpHead x xs.reverse).map fun (head, rest) => head :: rest
theorem nextPermutation_none (xs : List Nat) : nextPermutation xs = none ↔ xs.Pairwise (· ≥ ·) := by
  induction xs with
  | nil => simp [nextPermutation]
  | cons x xs ih =>
    cases hn : nextPermutation xs with
    | none =>
      have hs := ih.mp hn; simp only [nextPermutation, hn, Option.map_eq_none_iff, bumpHead_none,
        List.mem_reverse, List.pairwise_cons]
      exact ⟨fun h => ⟨h, hs⟩, fun h => h.1⟩
    | some tail =>
      have hs : ¬xs.Pairwise (· ≥ ·) := fun hs => by rw [ih.mpr hs] at hn; contradiction
      simp [nextPermutation, hn, List.pairwise_cons, hs]
theorem sorted_min (xs ys : List Nat) (sorted : xs.Pairwise (· ≤ ·))
    (perm : xs.Perm ys) : xs ≤ ys := by
  induction xs generalizing ys with
  | nil => simp
  | cons x xs ih =>
    cases ys with
    | nil => have := perm.length_eq; simp at this
    | cons y ys =>
      obtain ⟨hmin, hs⟩ := List.pairwise_cons.mp sorted
      have hm : y ∈ x :: xs := perm.mem_iff.mpr (by simp)
      have hxy : x ≤ y := by
        rcases List.mem_cons.mp hm with rfl | hm
        · exact Nat.le_refl _
        · exact hmin y hm
      apply List.cons_le_cons_iff.mpr; by_cases he : x = y
      · subst y; exact .inr ⟨rfl, ih ys hs perm.cons_inv⟩
      · exact .inl (by omega)
theorem sorted_max (xs ys : List Nat) (sorted : xs.Pairwise (· ≥ ·))
    (perm : xs.Perm ys) : ys ≤ xs := by
  induction xs generalizing ys with
  | nil => simp only [List.le_nil]; exact List.length_eq_zero_iff.mp perm.length_eq.symm
  | cons x xs ih =>
    cases ys with
    | nil => simp
    | cons y ys =>
      obtain ⟨hmax, hs⟩ := List.pairwise_cons.mp sorted
      have hm : y ∈ x :: xs := perm.mem_iff.mpr (by simp)
      have hyx : y ≤ x := by
        rcases List.mem_cons.mp hm with rfl | hm
        · exact Nat.le_refl _
        · exact hmax y hm
      apply List.cons_le_cons_iff.mpr; by_cases he : x = y
      · subst y; exact .inr ⟨rfl, ih ys hs perm.cons_inv⟩
      · exact .inl (by omega)
theorem nextPermutation_exact (xs : List Nat) (h : nextPermutation xs = some ys) :
    xs.Perm ys ∧ xs < ys ∧ ∀ zs, xs.Perm zs → xs < zs → ys ≤ zs := by
  induction xs generalizing ys with -- The next step is the immediate lexicographic successor; equivariant_k_bounded_safety_aut.hh:744.
  | nil => simp [nextPermutation] at h
  | cons x xs ih =>
    cases hn : nextPermutation xs with
    | some tail =>
      simp [nextPermutation, hn] at h
      subst ys
      obtain ⟨hp, hlt, hnext⟩ := ih hn
      refine ⟨hp.cons x, List.Lex.cons hlt, ?_⟩
      intro zs hz hgt; cases zs with
      | nil => exact False.elim (List.not_lt_nil _ hgt)
      | cons z zs =>
        rcases List.cons_lt_cons_iff.mp hgt with hlt | ⟨rfl, hlt⟩
        · exact List.cons_le_cons_iff.mpr (.inl hlt)
        · exact List.cons_le_cons_iff.mpr (.inr ⟨rfl, hnext zs hz.cons_inv hlt⟩)
    | none =>
      have hs := (nextPermutation_none xs).mp hn
      have hr : xs.reverse.Pairwise (· ≤ ·) := by simpa only [List.pairwise_reverse] using hs
      cases hb : bumpHead x xs.reverse with
      | none => simp [nextPermutation, hn, hb] at h
      | some pair =>
        rcases pair with ⟨head, rest⟩
        simp [nextPermutation, hn, hb] at h
        subst ys
        obtain ⟨hlt, hp, hsort, hmin⟩ := bumpHead_exact x xs.reverse hr hb
        have hp' : (x :: xs).Perm (head :: rest) :=
          ((List.Perm.refl [x]).append (List.reverse_perm xs).symm).trans hp.symm
        refine ⟨hp', List.Lex.rel hlt, ?_⟩
        intro zs hz hgt; cases zs with
        | nil => exact False.elim (List.not_lt_nil _ hgt)
        | cons z zs =>
          have hxz : x < z := by
            rcases List.cons_lt_cons_iff.mp hgt with hlt | ⟨rfl, hlt⟩
            · exact hlt
            · exact False.elim (sorted_max xs zs hs hz.cons_inv hlt)
          have hzmem : z ∈ xs.reverse := by
            have hm := hz.mem_iff.mpr List.mem_cons_self
            simpa only [List.mem_cons, show z ≠ x by omega, false_or, List.mem_reverse] using hm
          have hhead := hmin z hzmem hxz
          apply List.cons_le_cons_iff.mpr; by_cases he : head = z
          · subst z
            exact .inr ⟨rfl, sorted_min rest zs hsort (hp'.symm.trans hz).cons_inv⟩
          · exact .inl (by omega)
-- Total do/while traversal; finite fuel does not truncate; equivariant_k_bounded_safety_aut.hh:731–744.
def permutationWalk : Nat → List Nat → List (List Nat)
  | 0, xs => [xs]
  | fuel + 1, xs => xs :: match nextPermutation xs with
      | none => []
      | some ys => permutationWalk fuel ys
theorem permutationWalk_member (fuel : Nat) (xs ys : List Nat)
    (h : ys ∈ permutationWalk fuel xs) : xs.Perm ys ∧ xs ≤ ys := by
  induction fuel generalizing xs with
  | zero => simp only [permutationWalk, List.mem_singleton] at h; subst ys; exact ⟨.refl _, List.le_refl _⟩
  | succ fuel ih =>
    simp only [permutationWalk, List.mem_cons] at h
    rcases h with rfl | h
    · exact ⟨.refl _, List.le_refl _⟩
    · cases hn : nextPermutation xs with
      | none => simp [hn] at h
      | some zs =>
        rw [hn] at h; obtain ⟨hp, hle⟩ := ih zs h
        obtain ⟨hp', hlt, _⟩ := nextPermutation_exact xs hn
        exact ⟨hp'.trans hp, List.le_trans (List.le_of_lt hlt) hle⟩
theorem permutationWalk_nodup (fuel : Nat) (xs : List Nat) : (permutationWalk fuel xs).Nodup := by
  induction fuel generalizing xs with
  | zero => simp [permutationWalk]
  | succ fuel ih =>
    cases hn : nextPermutation xs with
    | none => simp [permutationWalk, hn]
    | some ys =>
      simp only [permutationWalk, hn, List.nodup_cons]
      refine ⟨?_, ih ys⟩
      intro h; have hle := (permutationWalk_member fuel ys xs h).2
      exact hle (nextPermutation_exact xs hn).2.1
def words (alphabet : List Nat) : Nat → List (List Nat)
  | 0 => [[]]
  | n + 1 => alphabet.flatMap fun x => (words alphabet n).map (x :: ·)
theorem words_complete (alphabet xs : List Nat) (h : ∀ x ∈ xs, x ∈ alphabet) :
    xs ∈ words alphabet xs.length := by
  induction xs with
  | nil => simp [words]
  | cons x xs ih =>
    simp only [List.length_cons, words, List.mem_flatMap, List.mem_map]
    exact ⟨x, h x (by simp), xs, ih (by intro y hy; exact h y (by simp [hy])), rfl⟩
private theorem walk_covers (U : List (List Nat)) (fuel : Nat)
    (xs target : List Nat)
    (full : ∀ ys, xs.Perm ys → ys ∈ U)
    (perm : xs.Perm target) (hle : xs ≤ target)
    (bound : (U.filter (fun ys => decide (xs < ys))).length ≤ fuel) : target ∈ permutationWalk fuel xs := by
  induction fuel generalizing xs with
  | zero =>
    by_cases he : xs = target
    · simp [permutationWalk, he]
    · have hlt : xs < target := Std.lt_of_le_of_ne hle he
      have hm : target ∈ U.filter (fun ys => decide (xs < ys)) := by
        simp [full target perm, hlt]
      have := List.length_pos_of_mem hm; omega
  | succ fuel ih =>
    by_cases he : xs = target
    · simp [permutationWalk, he]
    · have hlt : xs < target := Std.lt_of_le_of_ne hle he
      cases hn : nextPermutation xs with
      | none => exact False.elim (sorted_max xs target ((nextPermutation_none xs).mp hn) perm hlt)
      | some ys =>
        obtain ⟨hp, hstep, hmin⟩ := nextPermutation_exact xs hn
        have hb := filter_strict U (fun zs => decide (xs < zs)) (fun zs => decide (ys < zs))
          (fun zs hz => decide_eq_true (List.lt_trans hstep (of_decide_eq_true hz)))
          ys (full ys hp) (by simp [hstep]) (by simp [List.lt_irrefl])
        have hm := ih ys (fun zs hz => full zs (hp.trans hz))
          (hp.symm.trans perm) (hmin target perm hlt) (by omega)
        simp [permutationWalk, hn, hm]
-- Start traversal at the canonical word; equivariant_k_bounded_safety_aut.hh:731–744.
def permutations (canonical : List Nat) : List (List Nat) :=
  permutationWalk (words canonical canonical.length).length canonical
theorem traversal_complete (canonical member : List Nat)
    (sorted : canonical.Pairwise (· ≤ ·)) : member ∈ permutations canonical ↔ canonical.Perm member := by
  constructor -- Ascending traversal visits every multiset permutation; equivariant_k_bounded_safety_aut.hh:731–744.
  · intro h; exact (permutationWalk_member _ _ _ h).1
  · intro h
    apply walk_covers (words canonical canonical.length) _ canonical member
      (bound := List.length_filter_le ..) (perm := h) (hle := sorted_min _ _ sorted h)
    intro ys hy; rw [hy.length_eq]
    exact words_complete canonical ys (fun _ hm => hy.mem_iff.mpr hm)
theorem traversal_unique (canonical : List Nat) : (permutations canonical).Nodup :=
  permutationWalk_nodup _ _ -- Traversal never repeats a word; equivariant_k_bounded_safety_aut.hh:731–744.

end Symmetry.Builders

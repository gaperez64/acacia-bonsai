import SymmetryProofs.Automaton

namespace Symmetry

/-- C++: symmetry.hh:235–248. Ignore duplicate edges for transition semantics. -/
def unique [DecidableEq α] : List α → List α
  | [] => []
  | x :: xs => if x ∈ unique xs then unique xs else x :: unique xs

@[simp] theorem mem_unique [DecidableEq α] (x : α) (xs : List α) :
    x ∈ unique xs ↔ x ∈ xs := by
  induction xs with
  | nil => simp [unique]
  | cons y ys ih => by_cases h : y ∈ unique ys <;> simp_all [unique]; grind

theorem unique_nodup [DecidableEq α] (xs : List α) : (unique xs).Nodup := by
  induction xs with
  | nil => simp [unique]
  | cons x xs ih => by_cases h : x ∈ unique xs <;> simp_all [unique, List.nodup_cons]

/-- A one-way check suffices on a finite edge set: an injective image cannot
    omit an edge. This discharges #331's converse obligation, symmetry.hh:9–17. -/
theorem finite_closed_iff (xs : List α) (p : Perm α)
    (closed : ∀ x ∈ xs, p x ∈ xs) (x : α) : p x ∈ xs ↔ x ∈ xs := by
  classical
  refine ⟨fun hx => ?_, closed x⟩
  by_cases hn : x ∈ xs
  · exact hn
  · apply False.elim
    let ys := unique xs
    have nod : (ys.map p).Nodup :=
      (unique_nodup xs).map p (fun _ _ hne he => hne (p.injective he))
    have missing : p x ∉ ys.map p := by
      intro hm
      obtain ⟨z, hz, he⟩ := List.mem_map.mp hm
      have : z = x := p.injective he
      subst z
      exact hn ((mem_unique _ _).mp hz)
    have sub : p x :: ys.map p ⊆ ys := by
      intro z hz
      simp only [List.mem_cons, List.mem_map] at hz
      rcases hz with he | ⟨w, hw, he⟩
      · subst z; exact (mem_unique _ _).mpr hx
      · subst z; exact (mem_unique _ _).mpr (closed _ ((mem_unique _ _).mp hw))
    have hlen := (List.nodup_cons.mpr ⟨missing, nod⟩).length_le_of_subset sub
    simp only [List.length_cons, List.length_map] at hlen
    omega

end Symmetry

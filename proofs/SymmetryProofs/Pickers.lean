import SymmetryProofs.Solver

namespace Symmetry
variable {A : Automaton n a}

/-- C++: input_pickers/critical.hh:33–80; critical_pq.hh:31–78.
    The scan order is supplied explicitly, including the priority queue's order.
    Moving a successful action to the front changes only the next scan's order. -/
def criticalPicker (apply : State n K → α → State n K)
    (maxima : List (State n K)) (inputs : List (β × List α)) : Option (β × List α) :=
  maxima.findSome? fun f => inputs.find? fun row =>
    !(row.2.any fun action => downset maxima (apply f action))

/-- Exhaustion means that every maximal vector has a surviving response for
    every retained input. This follows from the two complete nested scans,
    regardless of priority or move-to-front ordering. -/
theorem criticalPicker_none (apply : State n K → α → State n K)
    (maxima : List (State n K)) (inputs : List (β × List α)) :
    criticalPicker apply maxima inputs = none ↔
      ∀ f ∈ maxima, ∀ row ∈ inputs,
        ∃ action ∈ row.2, downset maxima (apply f action) = true := by
  simp [criticalPicker, List.findSome?_eq_none_iff, List.find?_eq_none]

/-- C++: input_pickers/critical_pq.hh:19–21,84–89. The queue's signed
    priority decreases on a hit; ties retain all entries. -/
def criticalPQ (apply : State n K → α → State n K) (maxima : List (State n K))
    (queue : List (Int × (β × List α))) : Option (β × List α) :=
  criticalPicker apply maxima ((queue.mergeSort (fun x y => x.1 ≤ y.1)).map Prod.snd)

/-- Priority changes cannot hide an input: sorting the multimap retains every
    row, so the priority picker has exactly the same exhaustion contract. -/
theorem criticalPQ_none (apply : State n K → α → State n K)
    (maxima : List (State n K)) (queue : List (Int × (β × List α))) :
    criticalPQ apply maxima queue = none ↔
      ∀ f ∈ maxima, ∀ row ∈ queue.map Prod.snd,
        ∃ action ∈ row.2, downset maxima (apply f action) = true := by
  rw [criticalPQ, criticalPicker_none]
  have h := List.mergeSort_perm queue (fun x y => x.1 ≤ y.1)
  simp only [List.mem_map]
  constructor <;> intro hall f hf row hr
  · obtain ⟨entry, he, rfl⟩ := hr
    exact hall f hf entry.2 ⟨entry, h.mem_iff.mpr he, rfl⟩
  · obtain ⟨entry, he, rfl⟩ := hr
    exact hall f hf entry.2 ⟨entry, h.mem_iff.mp he, rfl⟩

theorem below_trans {f g h : State n K} (hfg : below f g = true)
    (hgh : below g h = true) : below f h = true := by
  exact decide_eq_true (fun q => Nat.le_trans ((of_decide_eq_true hfg) q)
    ((of_decide_eq_true hgh) q))

theorem succ_mono (K : Nat) (f g : State n K) (h : below f g = true)
    (v : Fin a → Bool) : below (succ A K f v) (succ A K g v) = true := by
  apply decide_eq_true
  intro r
  change maxStates (fun q => contribution A K f v q r) ≤
    maxStates (fun q => contribution A K g v q r)
  apply (maxStates_le _ _).mpr
  intro q
  apply Nat.le_trans (m := contribution A K g v q r) _
    (le_maxStates (fun q => contribution A K g v q r) q)
  have hq := (of_decide_eq_true h) q
  unfold contribution
  by_cases he : A.enabled q r v = true
  · by_cases hf : (f q).val > 0
    · have hg : (g q).val > 0 := by omega
      simp only [he, Bool.true_and, decide_eq_true hf, decide_eq_true hg, ite_true]
      omega
    · simp [hf]
  · simp [Bool.eq_false_iff.mpr he]

/-- Scanning maxima suffices for the entire represented region: the same
    response works below each maximum because max/increment is monotone. -/
theorem picker_none_constraints (K : Nat) (side : Bool)
    (maxima : List (State n K)) (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    ∀ row ∈ inputs, ∀ f, downset maxima f = true →
      computeT A K side (downset maxima) row.1 f = true := by
  intro row hr f hf
  obtain ⟨m, hm, hfm⟩ := List.any_eq_true.mp hf
  obtain ⟨v, hv, hs⟩ := (criticalPicker_none _ _ _).mp exhausted m hm row hr
  obtain ⟨o, rfl⟩ := compatible row hr v hv
  obtain ⟨g, hg, hmg⟩ := List.any_eq_true.mp hs
  apply (computeT_iff _ _ _ _ _ _).mpr
  exact ⟨o, List.any_eq_true.mpr ⟨g, hg,
    below_trans (succ_mono K f m hfm _) hmg⟩⟩

/-- The no-input success branch satisfies every literal backward constraint.
    Positive K and safe maxima are exactly the existing preimage bridge's
    requirements; no backward validation is added to the solver. -/
theorem picker_none_backward (K : Nat) (hk : 1 ≤ K) (side : Bool)
    (maxima : List (State n K)) (safeMax : ∀ m ∈ maxima, safe A K m = true)
    (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    ∀ row ∈ inputs, (fun f => downset maxima f && backwardT A K side maxima row.1 f) =
      downset maxima := by
  intro row hr
  funext f
  rw [backward_update_exact K hk side maxima safeMax]
  cases hf : downset maxima f with
  | false => rfl
  | true => simp only [Bool.true_and, picker_none_constraints K side maxima inputs
      exhausted compatible row hr f hf]

/-- C++: equivariant_k_bounded_safety_aut.hh:936–942. Once the picker is
    exhausted, representative update and generator closure leave the region
    fixed. Closure symmetry is maintained by the shared loop. -/
theorem picker_none_fixed (K : Nat) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (symmetric : Symmetric P.generators (downset maxima))
    (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ inputs, row.1 = O.representative)
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    representativeRound K side P (downset maxima) = downset maxima := by
  have each : ∀ O ∈ P.orbits,
      representativeUpdate K side P.generators O.representative (downset maxima) = downset maxima := by
    intro O ho
    obtain ⟨row, hr, he⟩ := retained O ho
    have hx : (fun f => downset maxima f && computeT A K side (downset maxima) O.representative f) =
        downset maxima := by
      funext f
      cases hf : downset maxima f with
      | false => rfl
      | true =>
        simp only [Bool.true_and]
        rw [← he]
        exact picker_none_constraints K side maxima inputs exhausted compatible row hr f hf
    rw [representativeUpdate, hx]
    apply subset_antisymm (close_shrinks _ _)
    exact close_preserves _ _ _ symmetric (fun _ h => h)
  unfold representativeRound
  have go : ∀ os : List (InputOrbit A), (∀ O ∈ os, O ∈ P.orbits) →
      os.foldl (fun Y O => representativeUpdate K side P.generators O.representative Y)
        (downset maxima) = downset maxima := by
    intro os
    induction os with
    | nil => intro _; rfl
    | cons O os ih =>
      intro h
      rw [List.foldl_cons, each O (h O (by simp))]
      exact ih (fun O ho => h O (by simp [ho]))
  exact go P.orbits (fun _ h => h)

/-- A region reached by shrinking sound updates contains the reference winner.
    Picker exhaustion supplies the converse inclusion, so returning it at the
    C++ success branch gives the exact final region and verdict. -/
theorem picker_success_eq (K : Nat) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (safeMax : ∀ m ∈ maxima, safe A K m = true)
    (contains : Subset (final A K side) (downset maxima))
    (symmetric : Symmetric P.generators (downset maxima))
    (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ inputs, row.1 = O.representative)
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    downset maxima = final A K side := by
  have fixed := representativeRound_fixed_cpre K side P (downset maxima)
    (picker_none_fixed K side P maxima symmetric inputs retained exhausted compatible)
  apply subset_antisymm _ contains
  apply final_greatest K side _ (downset_safe K maxima safeMax)
  intro f hf
  rw [fixed]
  exact hf

end Symmetry

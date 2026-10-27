import SymmetryProofs.Closure

namespace Symmetry
variable {A : Automaton n a}
def criticalPicker (apply : State n K → α → State n K) -- Complete nested maxima/input/response scan; critical.hh:33–80.
    (maxima : List (State n K)) (inputs : List (β × List α)) : Option (β × List α) :=
  maxima.findSome? fun f => inputs.find? fun row => !(row.2.any fun action => downset maxima (apply f action))
-- Exhaustion means a surviving response for every maximum and row; critical.hh:33–80.
theorem criticalPicker_none (apply : State n K → α → State n K)
    (maxima : List (State n K)) (inputs : List (β × List α)) : criticalPicker apply maxima inputs = none ↔
      ∀ f ∈ maxima, ∀ row ∈ inputs,
        ∃ action ∈ row.2, downset maxima (apply f action) = true := by simp [criticalPicker, List.findSome?_eq_none_iff, List.find?_eq_none]
-- Sort signed hit counts before scanning all rows; critical_pq.hh:19–89.
def criticalPQ (apply : State n K → α → State n K) (maxima : List (State n K))
    (queue : List (Int × (β × List α))) : Option (β × List α) :=
  criticalPicker apply maxima ((queue.mergeSort (fun x y => x.1 ≤ y.1)).map Prod.snd)
theorem criticalPQ_none (apply : State n K → α → State n K) -- Priority order does not change exhaustion; critical_pq.hh:19–89.
    (maxima : List (State n K)) (queue : List (Int × (β × List α))) : criticalPQ apply maxima queue = none ↔
      ∀ f ∈ maxima, ∀ row ∈ queue.map Prod.snd, ∃ action ∈ row.2, downset maxima (apply f action) = true := by
  rw [criticalPQ, criticalPicker_none]; have h := List.mergeSort_perm queue (fun x y => x.1 ≤ y.1)
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
  apply decide_eq_true; intro r
  change maxStates (fun q => contribution A K f v q r) ≤
    maxStates (fun q => contribution A K g v q r)
  apply (maxStates_le _ _).mpr; intro q
  apply Nat.le_trans (m := contribution A K g v q r) _
    (le_maxStates (fun q => contribution A K g v q r) q)
  have hq := (of_decide_eq_true h) q
  unfold contribution; by_cases he : A.enabled q r v = true
  · by_cases hf : (f q).val > 0
    · have hg : (g q).val > 0 := by omega
      simp only [he, Bool.true_and, decide_eq_true hf, decide_eq_true hg, ite_true]; omega
    · simp [hf]
  · simp [Bool.eq_false_iff.mpr he]
theorem picker_none_constraints (K : Nat) (side : Bool)
    (maxima : List (State n K)) (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    ∀ row ∈ inputs, ∀ f, downset maxima f = true → computeT A K side (downset maxima) row.1 f = true := by
  intro row hr f hf; obtain ⟨m, hm, hfm⟩ := List.any_eq_true.mp hf
  obtain ⟨v, hv, hs⟩ := (criticalPicker_none _ _ _).mp exhausted m hm row hr
  obtain ⟨o, rfl⟩ := compatible row hr v hv
  obtain ⟨g, hg, hmg⟩ := List.any_eq_true.mp hs
  apply (computeT_iff _ _ _ _ _ _).mpr; exact ⟨o, List.any_eq_true.mpr ⟨g, hg,
    below_trans (succ_mono K f m hfm _) hmg⟩⟩
theorem picker_none_backward (K : Nat) (hk : 1 ≤ K) (side : Bool)
    (maxima : List (State n K)) (safeMax : ∀ m ∈ maxima, safe A K m = true)
    (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    ∀ row ∈ inputs, (fun f => downset maxima f && backwardT A K side maxima row.1 f) = downset maxima := by
  intro row hr; funext f -- Scalar exhaustion satisfies each literal backward constraint; equivariant_k_bounded_safety_aut.hh:936–942.
  rw [backward_update_exact K hk side maxima safeMax]; cases hf : downset maxima f with
  | false => rfl
  | true => simp only [Bool.true_and, picker_none_constraints K side maxima inputs
      exhausted compatible row hr f hf]
theorem picker_none_fixed (K : Nat) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (symmetric : Symmetric P.generators (downset maxima))
    (inputs : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ inputs, row.1 = O.representative)
    (exhausted : criticalPicker (fun f v => succ A K f v) maxima inputs = none)
    (compatible : ∀ row ∈ inputs, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o) :
    representativeRound K side P (downset maxima) = downset maxima := by
  have each : ∀ O ∈ P.orbits, -- Exhaustion fixes every representative update and closure; equivariant_k_bounded_safety_aut.hh:936–942.
      representativeUpdate K side P.generators O.representative (downset maxima) = downset maxima := by
    intro O ho; obtain ⟨row, hr, he⟩ := retained O ho
    have hx : (fun f => downset maxima f && computeT A K side (downset maxima) O.representative f) =
        downset maxima := by
      funext f; cases hf : downset maxima f with
      | false => rfl
      | true =>
        simp only [Bool.true_and]
        rw [← he]; exact picker_none_constraints K side maxima inputs exhausted compatible row hr f hf
    rw [representativeUpdate, hx]; apply subset_antisymm (close_shrinks _ _)
    exact close_preserves _ _ _ symmetric (fun _ h => h)
  unfold representativeRound; have go : ∀ os : List (InputOrbit A), (∀ O ∈ os, O ∈ P.orbits) →
      os.foldl (fun Y O => representativeUpdate K side P.generators O.representative Y)
        (downset maxima) = downset maxima := by
    intro os; induction os with
    | nil => intro _; rfl
    | cons O os ih =>
      intro h; rw [List.foldl_cons, each O (h O (by simp))]
      exact ih (fun O ho => h O (by simp [ho]))
  exact go P.orbits (fun _ h => h)
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
  apply subset_antisymm _ contains; apply final_greatest K side _ (downset_safe K maxima safeMax)
  intro f hf; rw [fixed]
  exact hf

variable {A : Automaton n a}
inductive SameRows : List (β × List α) → List (β × List α) → Prop
  | nil : SameRows [] []
  | cons {x y : β × List α} {xs ys} : (x.1 = y.1 ∧ x.2.Perm y.2) →
      SameRows xs ys → SameRows (x :: xs) (y :: ys)
theorem SameRows.refl (xs : List (β × List α)) : SameRows xs xs := by
  induction xs with
  | nil => exact .nil
  | cons x xs ih => exact .cons ⟨rfl, .refl _⟩ ih
theorem SameRows.trans {xs ys zs : List (β × List α)} (h : SameRows xs ys) (h' : SameRows ys zs) :
    SameRows xs zs := by
  induction h generalizing zs with
  | nil => cases h'; exact .nil
  | cons hp _ ih =>
    cases h' with
    | cons hq ht => exact .cons ⟨hp.1.trans hq.1, hp.2.trans hq.2⟩ (ih ht)
theorem SameRows.responses {xs ys : List (β × List α)} (h : SameRows xs ys) (good : α → Bool) :
    (∀ row ∈ xs, ∃ action ∈ row.2, good action = true) ↔
      ∀ row ∈ ys, ∃ action ∈ row.2, good action = true := by
  induction h with
  | nil => simp
  | @cons x y xs ys hp _ ih =>
    simp only [List.mem_cons, forall_eq_or_imp]
    apply and_congr _ ih; exact ⟨fun ⟨a, ha, he⟩ => ⟨a, hp.2.mem_iff.mp ha, he⟩,
      fun ⟨a, ha, he⟩ => ⟨a, hp.2.mem_iff.mpr ha, he⟩⟩
-- Splice each surviving response to the front; critical.hh:43–77.
def scanCriticalRows (good : α → Bool) : List (β × List α) → Option (β × List α) × List (β × List α)
  | [] => (none, [])
  | row :: rows => match takeResponse good row.2 with
      | none => (some row, row :: rows)
      | some (action, rest) =>
          let (picked, tail) := scanCriticalRows good rows
          (picked, (row.1, action :: rest) :: tail)
theorem scanCriticalRows_exact (good : α → Bool) (rows : List (β × List α)) :
    SameRows (scanCriticalRows good rows).2 rows ∧ ((scanCriticalRows good rows).1 = none ↔
        ∀ row ∈ rows, ∃ action ∈ row.2, good action = true) := by
  induction rows with
  | nil => exact ⟨.nil, by simp [scanCriticalRows]⟩
  | cons row rows ih =>
    cases ht : takeResponse good row.2 with
    | none =>
      have hn := (takeResponse_none _ _).mp ht
      refine ⟨by simpa [scanCriticalRows, ht] using SameRows.refl (row :: rows), ?_⟩
      simp only [scanCriticalRows, ht, Option.some_ne_none, false_iff]
      intro h; obtain ⟨a, ha, he⟩ := h row (by simp)
      rw [hn a ha] at he; contradiction
    | some pair =>
      rcases pair with ⟨action, rest⟩
      obtain ⟨ha, hp⟩ := takeResponse_exact good row.2 ht
      have hm : action ∈ row.2 := hp.mem_iff.mp (by simp); simp only [scanCriticalRows, ht]
      refine ⟨.cons ⟨rfl, hp⟩ ih.1, ?_⟩
      rw [ih.2]; simp only [List.mem_cons, forall_eq_or_imp]
      exact ⟨fun h => ⟨⟨action, hm, ha⟩, h⟩, And.right⟩
-- Keep cache mutations between maxima; stop at the first hit; critical.hh:39–80.
def scanCriticalMaxima (apply : State n K → α → State n K) (region : Region n K) :
    List (State n K) → List (β × List α) → Option (β × List α) × List (β × List α)
  | [], rows => (none, rows)
  | f :: fs, rows =>
      let (picked, updated) := scanCriticalRows (fun action => region (apply f action)) rows
      match picked with
      | some row => (some row, updated)
      | none => scanCriticalMaxima apply region fs updated
theorem scanCriticalMaxima_exact (apply : State n K → α → State n K) (region : Region n K)
    (maxima : List (State n K)) (rows : List (β × List α)) :
    SameRows (scanCriticalMaxima apply region maxima rows).2 rows ∧
      ((scanCriticalMaxima apply region maxima rows).1 = none ↔
        ∀ f ∈ maxima, ∀ row ∈ rows, ∃ action ∈ row.2, region (apply f action) = true) := by
  induction maxima generalizing rows with
  | nil => exact ⟨SameRows.refl _, by simp [scanCriticalMaxima]⟩
  | cons f fs ih =>
    have hr := scanCriticalRows_exact (fun action => region (apply f action)) rows
    cases he : scanCriticalRows (fun action => region (apply f action)) rows with
    | mk picked updated =>
      rw [he] at hr; cases picked with
      | none =>
        have hu := ih updated; simp only [scanCriticalMaxima, he]
        refine ⟨hu.1.trans hr.1, ?_⟩
        rw [hu.2]; have hh := hr.2.mp rfl
        simp only [List.mem_cons, forall_eq_or_imp]
        constructor
        · intro h
          exact ⟨hh, fun g hg => (hr.1.responses _).mp (h g hg)⟩
        · intro h g hg
          exact (hr.1.responses _).mpr (h.2 g hg)
      | some row =>
        simp only [scanCriticalMaxima, he]
        refine ⟨hr.1, ?_⟩
        have hn : ¬∀ row ∈ rows, ∃ action ∈ row.2, region (apply f action) = true := by
          intro h; have := hr.2.mpr h
          contradiction
        simp only [Option.some_ne_none, List.mem_cons, forall_eq_or_imp, false_iff]; exact fun h => hn h.1
def criticalCached (apply : State n K → α → State n K) (maxima : List (State n K))
    (rows : List (β × List α)) : Option (β × List α) × List (β × List α) :=
  scanCriticalMaxima apply (downset maxima) maxima rows
-- Literal response splices preserve no-input results; critical.hh:24–89.
theorem criticalCached_none (apply : State n K → α → State n K) (maxima : List (State n K))
    (rows : List (β × List α)) :
    (criticalCached apply maxima rows).1 = none ↔ criticalPicker apply maxima rows = none := by
  rw [criticalPicker_none]; exact (scanCriticalMaxima_exact apply (downset maxima) maxima rows).2
def criticalPQCached [DecidableEq β] (apply : State n K → α → State n K)
    (maxima : List (State n K)) (queue : List (Int × (β × List α))) :
    Option (β × List α) × List (Int × (β × List α)) :=
  let ordered := queue.mergeSort fun x y => x.1 ≤ y.1 -- Scan by priority, then decrease the selected hit count; critical_pq.hh:19–89.
  let (picked, rows) := criticalCached apply maxima (ordered.map Prod.snd)
  (picked, ordered.zipWith (fun old row => (if picked.map Prod.fst = some row.1 then old.1 - 1 else old.1, row)) rows)
-- Signed hits and caching preserve exhaustion; critical_pq.hh:19–89.
theorem criticalPQCached_none [DecidableEq β] (apply : State n K → α → State n K)
    (maxima : List (State n K)) (queue : List (Int × (β × List α))) :
    (criticalPQCached apply maxima queue).1 = none ↔ criticalPQ apply maxima queue = none := by
  change (criticalCached apply maxima _).1 = none ↔ criticalPicker apply maxima _ = none
  exact criticalCached_none _ _ _
-- Boolean destinations neither reject nor receive counting edges; forward_saturation.hh:46–86.
def BooleanClosed (A : Automaton n a) : Prop :=
  ∀ q r v, A.enabled q r v = true → A.counting r = false → A.counting q = false ∧ A.rejecting r = false
def storedSucc (A : Automaton n a) (K : Nat) (f : State n K) (v : Fin a → Bool) : State n K :=
  fun q => if A.counting q then succ A K f v q else -- Boolean storage keeps presence only; X_and_boolvec.hh:21–30,69–71.
    if (succ A K f v q).val = 0 then ⟨0, by omega⟩ else ⟨1, by omega⟩
theorem storedSucc_exact (closed : BooleanClosed A) (K : Nat) (f : State n K)
    (hf : safe A K f = true) (v : Fin a → Bool) : storedSucc A K f v = succ A K f v := by
  funext r; by_cases hc : A.counting r = true -- Safe Boolean-closed scalar successors already equal stored values; standard.hh:195.
  · simp [storedSucc, hc]
  · have hr : A.counting r = false := Bool.eq_false_iff.mpr hc
    have hb : (succ A K f v r).val ≤ 1 := by
      apply (maxStates_le _ _).mpr; intro q
      by_cases he : A.enabled q r v = true
      · obtain ⟨hq, hrej⟩ := closed q r v he hr
        have hcap := (of_decide_eq_true hf) q; simp only [hq, Bool.false_eq_true, ↓reduceIte] at hcap
        unfold contribution; simp only [he, Bool.true_and, hrej, Bool.false_eq_true, ↓reduceIte]
        split <;> omega
      · simp [contribution, Bool.eq_false_iff.mpr he]
    apply Fin.ext; simp only [storedSucc, hr, Bool.false_eq_true, ↓reduceIte]
    by_cases hz : (succ A K f v r).val = 0
    · simp only [hz, ↓reduceIte]
    · simp only [hz, ↓reduceIte]
      omega
theorem stored_picker_none (closed : BooleanClosed A) (K : Nat)
    (maxima : List (State n K)) (hm : ∀ m ∈ maxima, safe A K m = true)
    (rows : List (β × List (Fin a → Bool)))
    (exhausted : (criticalCached (fun f v => storedSucc A K f v) maxima rows).1 = none) :
    criticalPicker (fun f v => succ A K f v) maxima rows = none := by
  apply (criticalPicker_none _ _ _).mpr -- Boolean closure connects stored exhaustion to scalar exhaustion; critical.hh:49–78.
  have h := (criticalPicker_none _ _ _).mp ((criticalCached_none _ _ _).mp exhausted)
  intro m hm' row hr; obtain ⟨v, hv, hsurv⟩ := h m hm' row hr
  rw [storedSucc_exact closed K m (hm m hm') v] at hsurv; exact ⟨v, hv, hsurv⟩

end Symmetry

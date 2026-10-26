import SymmetryProofs.Pickers

namespace Symmetry
variable {A : Automaton n a}

/-- C++: input_pickers/critical.hh:46–57,72–73; critical_pq.hh:44–55,70–72.
    Stop at the first surviving response and remove it for splice-to-front. -/
def takeResponse (good : α → Bool) : List α → Option (α × List α)
  | [] => none
  | x :: xs => if good x then some (x, xs) else
      (takeResponse good xs).map fun (found, rest) => (found, x :: rest)

theorem takeResponse_none (good : α → Bool) (xs : List α) :
    takeResponse good xs = none ↔ ∀ x ∈ xs, good x = false := by
  induction xs with
  | nil => simp [takeResponse]
  | cons x xs ih => cases hx : good x <;> simp [takeResponse, hx, ih]

theorem takeResponse_exact (good : α → Bool) (xs : List α)
    (h : takeResponse good xs = some (found, rest)) :
    good found = true ∧ (found :: rest).Perm xs := by
  induction xs generalizing rest with
  | nil => simp [takeResponse] at h
  | cons x xs ih =>
    cases hx : good x with
    | true =>
      simp [takeResponse, hx] at h
      obtain ⟨rfl, rfl⟩ := h
      exact ⟨hx, .refl _⟩
    | false =>
      cases ht : takeResponse good xs with
      | none => simp [takeResponse, hx, ht] at h
      | some pair =>
        rcases pair with ⟨f, rs⟩
        simp [takeResponse, hx, ht] at h
        obtain ⟨rfl, rfl⟩ := h
        obtain ⟨hf, hp⟩ := ih ht
        exact ⟨hf, (List.Perm.swap _ _ _).trans (hp.cons x)⟩

/-- Same input references, in the same order, with only response permutations. -/
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
    apply and_congr _ ih
    exact ⟨fun ⟨a, ha, he⟩ => ⟨a, hp.2.mem_iff.mp ha, he⟩,
      fun ⟨a, ha, he⟩ => ⟨a, hp.2.mem_iff.mpr ha, he⟩⟩

/-- C++: input_pickers/critical.hh:43–77; critical_pq.hh:41–76.
    Scan inputs for one maximum, splicing each successful response to front. -/
def scanCriticalRows (good : α → Bool) : List (β × List α) →
    Option (β × List α) × List (β × List α)
  | [] => (none, [])
  | row :: rows => match takeResponse good row.2 with
      | none => (some row, row :: rows)
      | some (action, rest) =>
          let (picked, tail) := scanCriticalRows good rows
          (picked, (row.1, action :: rest) :: tail)

theorem scanCriticalRows_exact (good : α → Bool) (rows : List (β × List α)) :
    SameRows (scanCriticalRows good rows).2 rows ∧
      ((scanCriticalRows good rows).1 = none ↔
        ∀ row ∈ rows, ∃ action ∈ row.2, good action = true) := by
  induction rows with
  | nil => exact ⟨.nil, by simp [scanCriticalRows]⟩
  | cons row rows ih =>
    cases ht : takeResponse good row.2 with
    | none =>
      have hn := (takeResponse_none _ _).mp ht
      refine ⟨by simpa [scanCriticalRows, ht] using SameRows.refl (row :: rows), ?_⟩
      simp only [scanCriticalRows, ht, Option.some_ne_none, false_iff]
      intro h
      obtain ⟨a, ha, he⟩ := h row (by simp)
      rw [hn a ha] at he
      contradiction
    | some pair =>
      rcases pair with ⟨action, rest⟩
      obtain ⟨ha, hp⟩ := takeResponse_exact good row.2 ht
      have hm : action ∈ row.2 := hp.mem_iff.mp (by simp)
      simp only [scanCriticalRows, ht]
      refine ⟨.cons ⟨rfl, hp⟩ ih.1, ?_⟩
      rw [ih.2]
      simp only [List.mem_cons, forall_eq_or_imp]
      exact ⟨fun h => ⟨⟨action, hm, ha⟩, h⟩, And.right⟩

/-- C++: input_pickers/critical.hh:39–80; critical_pq.hh:37–78.
    Preserve the mutated action order between maximal vectors and stop at the
    first critical input. The region remains the call's original downset. -/
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
      rw [he] at hr
      cases picked with
      | none =>
        have hu := ih updated
        simp only [scanCriticalMaxima, he]
        refine ⟨hu.1.trans hr.1, ?_⟩
        rw [hu.2]
        have hh := hr.2.mp rfl
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
          intro h
          have := hr.2.mpr h
          contradiction
        simp only [Option.some_ne_none, List.mem_cons, forall_eq_or_imp, false_iff]
        exact fun h => hn h.1

/-- C++: input_pickers/critical.hh:24–89. Executable picker with its cache. -/
def criticalCached (apply : State n K → α → State n K) (maxima : List (State n K))
    (rows : List (β × List α)) : Option (β × List α) × List (β × List α) :=
  scanCriticalMaxima apply (downset maxima) maxima rows

/-- Move-to-front caching does not alter the no-input result. Thus all scalar
    picker/backward/final-set theorems also apply to the literal cached scan. -/
theorem criticalCached_none (apply : State n K → α → State n K) (maxima : List (State n K))
    (rows : List (β × List α)) :
    (criticalCached apply maxima rows).1 = none ↔ criticalPicker apply maxima rows = none := by
  rw [criticalPicker_none]
  exact (scanCriticalMaxima_exact apply (downset maxima) maxima rows).2

/-- C++: input_pickers/critical_pq.hh:19–21,84–89. Sort signed priorities,
    scan with response caching, and lower the selected reference's priority.
    Input references are distinct in the source multimap. -/
def criticalPQCached [DecidableEq β] (apply : State n K → α → State n K)
    (maxima : List (State n K)) (queue : List (Int × (β × List α))) :
    Option (β × List α) × List (Int × (β × List α)) :=
  let ordered := queue.mergeSort fun x y => x.1 ≤ y.1
  let (picked, rows) := criticalCached apply maxima (ordered.map Prod.snd)
  (picked, ordered.zipWith (fun old row =>
    (if picked.map Prod.fst = some row.1 then old.1 - 1 else old.1, row)) rows)

/-- The priority picker's hit counter and action splices preserve exhaustion.
    This includes calls after any number of earlier priority updates. -/
theorem criticalPQCached_none [DecidableEq β] (apply : State n K → α → State n K)
    (maxima : List (State n K)) (queue : List (Int × (β × List α))) :
    (criticalPQCached apply maxima queue).1 = none ↔ criticalPQ apply maxima queue = none := by
  change (criticalCached apply maxima _).1 = none ↔ criticalPicker apply maxima _ = none
  exact criticalCached_none _ _ _

/-- C++: boolean_states/forward_saturation.hh:46–67,75–86. The preprocessed
    Boolean suffix has no rejecting state or incoming counting-state edge.
    This is the graph property needed to interpret forward Boolean storage. -/
def BooleanClosed (A : Automaton n a) : Prop :=
  ∀ q r v, A.enabled q r v = true → A.counting r = false →
    A.counting q = false ∧ A.rejecting r = false

/-- C++: actioners/standard.hh:195; subprojects/posets/include/posets/vectors/X_and_boolvec.hh:21–30,69–71.
    Store a Boolean destination as presence, just as State(apply_out) does. -/
def storedSucc (A : Automaton n a) (K : Nat) (f : State n K) (v : Fin a → Bool) : State n K :=
  fun q => if A.counting q then succ A K f v q else
    if (succ A K f v q).val = 0 then ⟨0, by omega⟩ else ⟨1, by omega⟩

/-- On safe vectors of a Boolean-closed graph, forward compression loses no
    value: every Boolean destination's scalar successor is already zero or one.
    This connects the actual picker apply result to backward_preimage. -/
theorem storedSucc_exact (closed : BooleanClosed A) (K : Nat) (f : State n K)
    (hf : safe A K f = true) (v : Fin a → Bool) : storedSucc A K f v = succ A K f v := by
  funext r
  by_cases hc : A.counting r = true
  · simp [storedSucc, hc]
  · have hr : A.counting r = false := Bool.eq_false_iff.mpr hc
    have hb : (succ A K f v r).val ≤ 1 := by
      apply (maxStates_le _ _).mpr
      intro q
      by_cases he : A.enabled q r v = true
      · obtain ⟨hq, hrej⟩ := closed q r v he hr
        have hcap := (of_decide_eq_true hf) q
        simp only [hq, Bool.false_eq_true, ↓reduceIte] at hcap
        unfold contribution
        simp only [he, Bool.true_and, hrej, Bool.false_eq_true, ↓reduceIte]
        split <;> omega
      · simp [contribution, Bool.eq_false_iff.mpr he]
    apply Fin.ext
    simp only [storedSucc, hr, Bool.false_eq_true, ↓reduceIte]
    by_cases hz : (succ A K f v r).val = 0
    · simp only [hz, ↓reduceIte]
    · simp only [hz, ↓reduceIte]
      omega

/-- Both actual forward membership tests and the scalar picker have the same
    no-input result. Safe maxima and Boolean closure justify the storage step. -/
theorem stored_picker_none (closed : BooleanClosed A) (K : Nat)
    (maxima : List (State n K)) (hm : ∀ m ∈ maxima, safe A K m = true)
    (rows : List (β × List (Fin a → Bool)))
    (exhausted : (criticalCached (fun f v => storedSucc A K f v) maxima rows).1 = none) :
    criticalPicker (fun f v => succ A K f v) maxima rows = none := by
  apply (criticalPicker_none _ _ _).mpr
  have h := (criticalPicker_none _ _ _).mp ((criticalCached_none _ _ _).mp exhausted)
  intro m hm' row hr
  obtain ⟨v, hv, hsurv⟩ := h m hm' row hr
  rw [storedSucc_exact closed K m (hm m hm') v] at hsurv
  exact ⟨v, hv, hsurv⟩

end Symmetry

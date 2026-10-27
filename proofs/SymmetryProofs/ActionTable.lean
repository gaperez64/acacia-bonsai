import SymmetryProofs.PlanBuilder
import SymmetryProofs.BooleanBoundary

namespace Symmetry.ActionTable
open Builders
abbrev Letter (a : Nat) := Fin a → Bool
abbrev Action (n : Nat) := Fin n → Fin n → Bool
abbrev Row (n a : Nat) := (Letter a → Bool) × List (Action n)
instance [DecidableEq α] : DecidableEq (Fin m → α) := fun f g => if h : ∀ p, f p = g p then isTrue (funext h) else isFalse (fun he => h (congrFun he))
instance : DecidableEq (Letter a → Bool) := fun f g =>
  if h : ((letters a).all fun i => f i == g i) = true then
    isTrue (funext fun i => by simpa using (List.all_eq_true.mp h) i (mem_letters i))
  else isFalse (fun he => h (by subst g; simp))
def action (A : Automaton n a) (v : Letter a) : Action n := fun q r => A.enabled q r v
def applyAction (A : Automaton n a) (K : Nat) (f : State n K) (t : Action n) : State n K :=
  fun r => ⟨maxStates (fun q => if t q r && decide ((f q).val > 0) then
    min (K + 1) ((f q).val + if A.rejecting r then 1 else 0) else 0), by
      apply Nat.lt_succ_of_le; apply (maxStates_le _ _).mpr
      intro q; split <;> omega⟩
def responses (A : Automaton n a) (side : Bool) (i : Letter a) : List (Action n) :=
  unique ((letters a).map fun o => action A (merge A side i o))
theorem mem_responses (A : Automaton n a) (side : Bool) (i : Letter a) (t : Action n) :
    t ∈ responses A side i ↔ ∃ o, action A (merge A side i o) = t := by
  simp only [responses, mem_unique, List.mem_map]
  exact ⟨fun ⟨o, _, h⟩ => ⟨o, h⟩, fun ⟨o, h⟩ => ⟨o, mem_letters o, h⟩⟩
inductive Diagram (a : Nat) (α : Type) where -- Input leaves carry residual output diagrams; mona.hh:142–190.
  | leaf (value : α)
  | branch (p : Fin a) (low high : Diagram a α)
  deriving DecidableEq
def Diagram.eval : Diagram a α → Letter a → α
  | .leaf x, _ => x
  | .branch p l h, i => if i p then h.eval i else l.eval i
def Diagram.rows : Diagram a α → List ((Letter a → Bool) × α)
  | .leaf x => [(fun _ => true, x)]
  | .branch p l h =>
      (l.rows.map fun row => (fun i => !i p && row.1 i, row.2)) ++
      (h.rows.map fun row => (fun i => i p && row.1 i, row.2))
def assign (i : Letter a) (p : Fin a) (b : Bool) : Letter a := fun q => if q = p then b else i q
def tabulate [DecidableEq α] (F : Letter a → α) : List (Fin a) → Letter a → Diagram a α
  | [], i => .leaf (F i)
  | p :: ps, i =>
      let l := tabulate F ps (assign i p false)
      let h := tabulate F ps (assign i p true)
      if l = h then l else .branch p l h
theorem tabulate_eval [DecidableEq α] (F : Letter a → α) (ps : List (Fin a)) (base i : Letter a)
    (agree : ∀ p, p ∉ ps → base p = i p) : (tabulate F ps base).eval i = F i := by
  induction ps generalizing base with
  | nil =>
    have he : base = i := funext fun p => agree p (by simp); simp [tabulate, Diagram.eval, he]
  | cons p ps ih =>
    have ha : ∀ b, b = i p → ∀ q, q ∉ ps → assign base p b q = i q := by
      intro b hb q hq; by_cases he : q = p
      · simp [assign, he, hb]
      · simp [assign, he, agree q (by simp [he, hq])]
    simp only [tabulate]
    split
    · next he =>
        cases hp : i p with
        | false => exact ih _ (ha false hp.symm)
        | true => rw [he]; exact ih _ (ha true hp.symm)
    · cases hp : i p <;> simp only [Diagram.eval, hp, ↓reduceIte]
      · exact ih _ (ha false hp.symm)
      · exact ih _ (ha true hp.symm)
theorem diagram_rows (D : Diagram a α) : (∀ i, ∃ row ∈ D.rows, row.1 i = true) ∧
    (∀ row ∈ D.rows, ∀ i, row.1 i = true → D.eval i = row.2) := by
  induction D with -- Traversal covers all valuations and gives each cube its residual; mona.hh:142–190.
  | leaf x => simp [Diagram.rows, Diagram.eval]
  | branch p l h il ih =>
    constructor
    · intro i
      cases hp : i p with
      | false =>
        obtain ⟨row, hr, hi⟩ := il.1 i
        exact ⟨_, List.mem_append_left _ (List.mem_map.mpr ⟨row, hr, rfl⟩), by simp [hp, hi]⟩
      | true =>
        obtain ⟨row, hr, hi⟩ := ih.1 i
        exact ⟨_, List.mem_append_right _ (List.mem_map.mpr ⟨row, hr, rfl⟩), by simp [hp, hi]⟩
    · intro row hr i hi
      rcases List.mem_append.mp hr with hr | hr
      · obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
        obtain ⟨hp, hi⟩ := Bool.and_eq_true_iff.mp hi
        have hp' : i p = false := by simpa using hp
        simpa [Diagram.eval, hp'] using il.2 r hm i hi
      · obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
        obtain ⟨hp, hi⟩ := Bool.and_eq_true_iff.mp hi; simpa [Diagram.eval, hp] using ih.2 r hm i hi
-- Keep an entire cube if it intersects the representatives; equivariant_k_bounded_safety_aut.hh:331–353.
def restrict (reps : List (Letter a)) (rows : List (Row n a)) : List (Row n a) :=
  rows.filter fun row => reps.any row.1
def dedup : List (Row n a) → List (Row n a) -- Compare whole action lists; retain the first original key; standard.hh:75–118.
  | [] => []
  | row :: rows => row :: (dedup rows).filter (fun r => r.2 != row.2)
theorem dedup_refines (rows : List (Row n a)) : (∀ row ∈ dedup rows, row ∈ rows) ∧
    (∀ row ∈ rows, ∃ kept ∈ dedup rows, kept.2 = row.2) := by
  induction rows with -- Discarded keys retain an equal action class; standard.hh:75–118.
  | nil => simp [dedup]
  | cons row rows ih =>
    rw [dedup]; constructor
    · intro r hr
      rcases List.mem_cons.mp hr with rfl | hr
      · simp
      · exact List.mem_cons_of_mem _ (ih.1 r (List.mem_filter.mp hr).1)
    · intro r hr
      by_cases he : r.2 = row.2
      · exact ⟨row, by simp, he.symm⟩
      · have hm : r ∈ rows := (List.mem_cons.mp hr).resolve_left (fun h => he (congrArg Prod.snd h))
        obtain ⟨kept, hk, heq⟩ := ih.2 r hm
        exact ⟨kept, List.mem_cons_of_mem _ (List.mem_filter.mpr ⟨hk, by simpa [heq] using he⟩), heq⟩
-- Action membership is the full response relation of a legal input; standard.hh:149–181.
def Class (A : Automaton n a) (side : Bool) (ts : List (Action n)) (i : Letter a) : Prop :=
  ∀ t, t ∈ ts ↔ ∃ o, action A (merge A side i o) = t
-- Representatives are covered by classes; surviving keys implement legal inputs; standard.hh:75–118.
structure Refines (A : Automaton n a) (side : Bool) (reps : List (Letter a)) (rows : List (Row n a)) : Prop where
  covers : ∀ i ∈ reps, ∃ row ∈ rows, Class A side row.2 i
  legal : ∀ row ∈ rows, ∃ i ∈ reps, row.1 i = true ∧ Class A side row.2 i
def diagramActions (D : Diagram a (Action n)) : List (Action n) :=
  (D.rows.filter (fun row => (letters a).any row.1)).map Prod.snd
-- Output traversal contains exactly the reachable endpoint actions; mona.hh:142–165.
theorem mem_diagramActions (D : Diagram a (Action n)) (t : Action n) :
    t ∈ diagramActions D ↔ ∃ o, D.eval o = t := by
  simp only [diagramActions, List.mem_map, List.mem_filter, List.any_eq_true]
  constructor
  · rintro ⟨row, ⟨hr, o, _, hc⟩, rfl⟩
    exact ⟨o, (diagram_rows D).2 row hr o hc⟩
  · rintro ⟨o, he⟩
    obtain ⟨row, hr, hc⟩ := (diagram_rows D).1 o
    exact ⟨row, ⟨hr, o, mem_letters o, hc⟩, ((diagram_rows D).2 row hr o hc).symm.trans he⟩
def precomputerRows (D : Diagram a (Diagram a (Action n))) : List (Row n a) :=
  D.rows.map fun row => (row.1, diagramActions row.2)
def precomputerTable (D : Diagram a (Diagram a (Action n))) (reps : List (Letter a)) : List (Row n a) :=
  dedup (restrict reps (precomputerRows D))
-- Traversal, cube restriction and row quotient derive table refinement; standard.hh:28–100,159.
theorem precomputer_refinement (A : Automaton n a) (side : Bool)
    (D : Diagram a (Diagram a (Action n)))
    (denotes : ∀ i o, (D.eval i).eval o = action A (merge A side i o)) (reps : List (Letter a)) :
    Refines A side reps (precomputerTable D reps) := by
  have complete : ∀ i, ∃ row ∈ precomputerRows D, row.1 i = true := by
    intro i; obtain ⟨row, hr, hc⟩ := (diagram_rows D).1 i
    exact ⟨_, List.mem_map.mpr ⟨row, hr, rfl⟩, hc⟩
  have exactClass : ∀ row ∈ precomputerRows D, ∀ i, row.1 i = true → Class A side row.2 i := by
    intro row hr i hi t; obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
    rw [mem_diagramActions, ← (diagram_rows D).2 r hm i hi]; simp only [denotes]
  constructor
  · intro i hi
    obtain ⟨row, hr, hc⟩ := complete i
    have hm : row ∈ restrict reps (precomputerRows D) :=
      List.mem_filter.mpr ⟨hr, List.any_eq_true.mpr ⟨i, hi, hc⟩⟩
    obtain ⟨kept, hk, he⟩ := (dedup_refines _).2 row hm; exact ⟨kept, hk, he ▸ exactClass row hr i hc⟩
  · intro row hr
    obtain ⟨hm, hc⟩ := List.mem_filter.mp ((dedup_refines _).1 row hr)
    obtain ⟨i, hi, hc⟩ := List.any_eq_true.mp hc; exact ⟨i, hi, hc, exactClass row hm i hc⟩
def precomputerDiagram (A : Automaton n a) (side : Bool) : Diagram a (Diagram a (Action n)) :=
  tabulate (fun i => tabulate (fun o => action A (merge A side i o))
    (List.finRange a) (fun _ => false)) (List.finRange a) (fun _ => false)
-- Truth-table construction supplies endpoint interpretation internally; standard.hh:28–100.
theorem precomputer_denotes (A : Automaton n a) (side : Bool) (i o : Letter a) :
    ((precomputerDiagram A side).eval i).eval o = action A (merge A side i o) := by
  unfold precomputerDiagram; rw [tabulate_eval _ _ _ i (by simp), tabulate_eval _ _ _ o (by simp)]
def grouped (A : Automaton n a) (side : Bool) : List (Row n a) :=
  precomputerRows (precomputerDiagram A side)
def physical (A : Automaton n a) (side : Bool) (reps : List (Letter a)) : List (Row n a) :=
  precomputerTable (precomputerDiagram A side) reps
theorem matching_rows {xs ys : List (Row n a)} (h : SameRows xs ys) :
    (∀ x ∈ xs, ∃ y ∈ ys, x.1 = y.1 ∧ x.2.Perm y.2) ∧ (∀ y ∈ ys, ∃ x ∈ xs, x.1 = y.1 ∧ x.2.Perm y.2) := by
  induction h with
  | nil => simp
  | cons hp _ ih => simp only [List.mem_cons] at *; grind
theorem Refines.cached {A : Automaton n a} {side reps xs ys}
    (h : Refines A side reps ys) (same : SameRows xs ys) : Refines A side reps xs := by
  have hm := matching_rows same
  constructor
  · intro i hi
    obtain ⟨y, hy, hc⟩ := h.covers i hi
    obtain ⟨x, hx, _, hp⟩ := hm.2 y hy; exact ⟨x, hx, fun t => hp.mem_iff.trans (hc t)⟩
  · intro x hx
    obtain ⟨y, hy, he, hp⟩ := hm.1 x hx
    obtain ⟨i, hi, hk, hc⟩ := h.legal y hy; exact ⟨i, hi, he ▸ hk, fun t => hp.mem_iff.trans (hc t)⟩
-- Priority ordering preserves action-class refinement; critical_pq.hh:19–89.
theorem Refines.permuted {A : Automaton n a} {side reps xs ys}
    (h : Refines A side reps ys) (perm : xs.Perm ys) : Refines A side reps xs where
  covers i hi := let ⟨row, hr, hc⟩ := h.covers i hi; ⟨row, perm.mem_iff.mpr hr, hc⟩
  legal row hr := h.legal row (perm.mem_iff.mp hr)
theorem Refines.scan {A : Automaton n a} {side reps rows} (h : Refines A side reps rows)
    (K : Nat) (maxima : List (State n K)) :
    Refines A side reps (criticalCached (applyAction A K) maxima rows).2 :=
  h.cached (scanCriticalMaxima_exact _ _ _ _).1 -- Response splices preserve action-class refinement; critical.hh:43–77.
theorem sameRows_length {xs ys : List (β × List α)} (same : SameRows xs ys) : xs.length = ys.length := by
  induction same with
  | nil => rfl
  | cons _ _ ih => simp only [List.length_cons, ih]
private theorem zip_rows (queue : List (Int × β)) (rows : List β) (priority : (Int × β) → β → Int)
    (lengths : rows.length = queue.length) :
    (queue.zipWith (fun old row => (priority old row, row)) rows).map Prod.snd = rows := by
  induction queue generalizing rows with
  | nil => cases rows <;> simp_all
  | cons entry queue ih =>
    cases rows with
    | nil => simp_all
    | cons row rows => simpa using ih rows (by simpa using lengths)
-- Sort, splice and queue reconstruction preserve refinement; critical_pq.hh:19–89.
theorem Refines.priority {A : Automaton n a} {side reps} (K : Nat) (maxima : List (State n K))
    (queue : List (Int × Row n a)) (h : Refines A side reps (queue.map Prod.snd)) :
    Refines A side reps ((criticalPQCached (applyAction A K) maxima queue).2.map Prod.snd) := by
  let ordered := queue.mergeSort (fun x y => x.1 ≤ y.1)
  have hs : Refines A side reps (ordered.map Prod.snd) :=
    h.permuted ((List.mergeSort_perm queue _).map Prod.snd)
  have hc := hs.scan K maxima
  have same := (scanCriticalMaxima_exact (applyAction A K) (downset maxima) maxima (ordered.map Prod.snd)).1
  have hl : (criticalCached (applyAction A K) maxima (ordered.map Prod.snd)).2.length = ordered.length := by
    simpa [criticalCached] using sameRows_length same
  unfold criticalPQCached; change Refines A side reps ((ordered.zipWith _ _).map Prod.snd)
  rw [zip_rows ordered _ _ hl]; exact hc
theorem physical_refinement (A : Automaton n a) (side : Bool) (reps : List (Letter a)) :
    Refines A side reps (physical A side reps) :=
  precomputer_refinement A side (precomputerDiagram A side) (precomputer_denotes A side) reps
theorem class_predecessor_exact (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (ts : List (Action n)) (i : Letter a) (he : Class A side ts i) (f : State n K) :
    ts.any (fun t => X (applyAction A K f t)) = computeT A K side X i f := by
  apply Bool.eq_iff_iff.mpr; rw [computeT_iff] -- Each class implements the legal input’s complete forward Pre; standard.hh:149–172.
  unfold Class at he; simp only [List.any_eq_true, he]
  constructor
  · rintro ⟨_, ⟨o, rfl⟩, hs⟩
    exact ⟨o, hs⟩
  · rintro ⟨o, hs⟩
    exact ⟨_, ⟨o, rfl⟩, hs⟩
def backwardAction (A : Automaton n a) (K : Nat) (m : State n K) (t : Action n) : State n K :=
  fun q => ⟨min (safeCap A K q) (minStates (K + 1) (fun r =>
    if t q r then (m r).val - (if A.rejecting r then 1 else 0) else K + 1)), by
      have hc : safeCap A K q ≤ K + 1 := by unfold safeCap; split <;> omega
      have := Nat.min_le_left (safeCap A K q) (minStates (K + 1) (fun r =>
        if t q r then (m r).val - (if A.rejecting r then 1 else 0) else K + 1))
      omega⟩
def classBackward (A : Automaton n a) (K : Nat) (maxima : List (State n K)) (ts : List (Action n)) : Region n K :=
  fun f => ts.any fun t => maxima.any fun m => below f (backwardAction A K m t)
theorem class_backward_exact (A : Automaton n a) (K : Nat) (side : Bool)
    (maxima : List (State n K)) (ts : List (Action n)) (i : Letter a) (he : Class A side ts i) :
    classBackward A K maxima ts = backwardT A K side maxima i := by
  funext f; apply Bool.eq_iff_iff.mpr -- Each class implements the complete backward union; standard.hh:175–181.
  unfold Class at he; simp only [classBackward, backwardT, List.any_eq_true, he]
  constructor
  · rintro ⟨_, ⟨o, rfl⟩, m, hm, hs⟩
    exact ⟨o, mem_letters o, m, hm, hs⟩
  · rintro ⟨o, _, m, hm, hs⟩
    exact ⟨_, ⟨o, rfl⟩, m, hm, hs⟩
def update (A : Automaton n a) (K : Nat) (G : List (Verified A))
    (maxima : List (State n K)) (row : Row n a) : Region n K :=
  close G (fun f => downset maxima f && classBackward A K maxima row.2 f)
-- A physical payload update equals an update at a legal input; equivariant_k_bounded_safety_aut.hh:945–947.
theorem selected_refinement (A : Automaton n a) (K : Nat) (positive : 1 ≤ K) (side : Bool)
    (G : List (Verified A)) (maxima : List (State n K))
    (safeMax : ∀ m ∈ maxima, safe A K m = true) (reps : List (Letter a)) (rows : List (Row n a))
    (refines : Refines A side reps rows) (row : Row n a) (hr : row ∈ rows) :
    ∃ i ∈ reps, row.1 i = true ∧ update A K G maxima row = representativeUpdate K side G i (downset maxima) := by
  obtain ⟨i, hi, hc, he⟩ := refines.legal row hr
  refine ⟨i, hi, hc, ?_⟩
  unfold update representativeUpdate; rw [class_backward_exact A K side maxima row.2 i he]
  congr 1
  funext f; exact backward_update_exact K positive side maxima safeMax i f
def virtual (A : Automaton n a) (side : Bool) (reps : List (Letter a)) : List (Letter a × List (Letter a)) :=
  reps.map fun i => (i, (letters a).map (merge A side i))
theorem virtual_retained (A : Automaton n a) (side : Bool) (P : SweepPlan A side) :
    ∀ O ∈ P.orbits, ∃ row ∈ virtual A side (P.orbits.map InputOrbit.representative),
      row.1 = O.representative := by intro O ho; exact ⟨_, List.mem_map.mpr ⟨_, List.mem_map.mpr ⟨O, ho, rfl⟩, rfl⟩, rfl⟩
theorem virtual_compatible (A : Automaton n a) (side : Bool) (reps : List (Letter a)) :
    ∀ row ∈ virtual A side reps, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o := by
  intro row hr v hv; obtain ⟨i, _, rfl⟩ := List.mem_map.mp hr
  obtain ⟨o, _, rfl⟩ := List.mem_map.mp hv; exact ⟨o, rfl⟩
-- Physical exhaustion supplies all full-response virtual rows; equivariant_k_bounded_safety_aut.hh:936–942.
theorem virtual_exhausted (A : Automaton n a) (closed : BooleanClosed A) (K : Nat) (side : Bool)
    (maxima : List (State n K)) (safeMax : ∀ m ∈ maxima, safe A K m = true)
    (reps : List (Letter a)) (rows : List (Row n a)) (refines : Refines A side reps rows)
    (exhausted : (criticalCached (applyAction A K) maxima rows).1 = none) :
    (criticalCached (fun f v => storedSucc A K f v) maxima (virtual A side reps)).1 = none := by
  rw [criticalCached_none, criticalPicker_none]
  have hx := (criticalPicker_none _ _ _).mp ((criticalCached_none _ _ _).mp exhausted)
  intro m hm vrow hv; obtain ⟨i, hi, rfl⟩ := List.mem_map.mp hv
  obtain ⟨row, hr, he⟩ := refines.covers i hi
  obtain ⟨t, ht, hs⟩ := hx m hm row hr
  obtain ⟨o, rfl⟩ := (he t).mp ht
  refine ⟨_, List.mem_map.mpr ⟨o, mem_letters o, rfl⟩, ?_⟩
  rw [storedSucc_exact closed K m (safeMax m hm)]; exact hs
theorem refined_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (invariants : Subset (final A K side) (downset maxima) ∧
      Subset (downset maxima) (safe A K) ∧ Symmetric P.generators (downset maxima) ∧
      downset maxima (initial A K) = true)
    (rows : List (Row n a)) (refines : Refines A side (P.orbits.map InputOrbit.representative) rows)
    (exhausted : (criticalCached (applyAction A K) maxima rows).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  have hm := maxima_safe invariants.2.1
  exact (picker_completed_of_invariants closed K positive side P maxima invariants _
    (virtual_retained A side P) (virtual_compatible A side _)
    (virtual_exhausted A closed K side maxima hm _ rows refines exhausted)).2
-- BDD endpoint interpretation suffices for physical-table completion; equivariant_k_bounded_safety_aut.hh:936–942.
theorem precomputer_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (invariants : Subset (final A K side) (downset maxima) ∧
      Subset (downset maxima) (safe A K) ∧ Symmetric P.generators (downset maxima) ∧
      downset maxima (initial A K) = true)
    (D : Diagram a (Diagram a (Action n)))
    (denotes : ∀ i o, (D.eval i).eval o = action A (merge A side i o))
    (exhausted : (criticalCached (applyAction A K) maxima
      (precomputerTable D (P.orbits.map InputOrbit.representative))).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true :=
  refined_completed A closed K positive side P maxima invariants _
    (precomputer_refinement A side D denotes _) exhausted
-- Constructed physical classes instantiate picker completion; equivariant_k_bounded_safety_aut.hh:936–942.
theorem physical_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (exhausted : (criticalCached (applyAction A K) maxima
      (physical A side (P.orbits.map InputOrbit.representative))).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true :=
  refined_completed A closed K positive side P maxima (picker_reach_invariants reached) _
    (physical_refinement A side _) exhausted
-- Physical updates preserve winners and continuing picker histories; equivariant_k_bounded_safety_aut.hh:945–955.
theorem physical_update_reach (A : Automaton n a) (K : Nat) (positive : 1 ≤ K) (side : Bool)
    (P : SweepPlan A side) (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (row : Row n a) (hr : row ∈ physical A side (P.orbits.map InputOrbit.representative))
    (hin : update A K P.generators maxima row (initial A K) = true) :
    Subset (final A K side) (update A K P.generators maxima row) ∧
    PickerReach A K side P.generators (update A K P.generators maxima row) := by
  have inv := picker_reach_invariants reached
  have hm := maxima_safe inv.2.1
  obtain ⟨i, _, _, he⟩ := selected_refinement A K positive side P.generators maxima hm _ _
    (physical_refinement A side _) row hr
  rw [he] at hin ⊢; exact ⟨selected_preserves K side P.generators i _ inv.1, .update reached i hin⟩

end Symmetry.ActionTable

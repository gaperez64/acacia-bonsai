import SymmetryProofs.PlanBuilder
import SymmetryProofs.PickerCompletion

namespace Symmetry.ActionTable
open Builders

abbrev Letter (a : Nat) := Fin a → Bool
abbrev Action (n : Nat) := Fin n → Fin n → Bool
abbrev Row (n a : Nat) := (Letter a → Bool) × List (Action n)

instance [DecidableEq α] : DecidableEq (Fin m → α) := fun f g =>
  if h : ∀ p, f p = g p then isTrue (funext h) else isFalse (fun he => h (congrFun he))

instance : DecidableEq (Letter a → Bool) := fun f g =>
  if h : ((letters a).all fun i => f i == g i) = true then
    isTrue (funext fun i => by simpa using (List.all_eq_true.mp h) i (mem_letters i))
  else isFalse (fun he => h (by subst g; simp))

/-- The extensional endpoint relation of a compact payload. Acceptance is
    fixed by the destination, in this solver's state-based configuration. -/
def action (A : Automaton n a) (v : Letter a) : Action n := fun q r => A.enabled q r v

/-- Payload equality and equality of endpoint relations give the same quotient. -/
theorem action_eq_payload (A : Automaton n a) (v w : Letter a) :
    action A v = action A w ↔ responseAction A v = responseAction A w := by
  constructor
  · intro h
    have he : ∀ q r, A.enabled q r v = A.enabled q r w := fun q r => congrFun (congrFun h q) r
    simp only [responseAction, he]
  · intro h
    funext q r
    change A.enabled q r v = A.enabled q r w
    apply Bool.eq_iff_iff.mpr
    rw [← responseAction_edge A v q r, ← responseAction_edge A w q r]
    simp only [h]

/-- Scalar forward application of an endpoint payload, including overflow. -/
def applyAction (A : Automaton n a) (K : Nat) (f : State n K) (t : Action n) : State n K :=
  fun r => ⟨maxStates (fun q => if t q r && decide ((f q).val > 0) then
    min (K + 1) ((f q).val + if A.rejecting r then 1 else 0) else 0), by
      apply Nat.lt_succ_of_le
      apply (maxStates_le _ _).mpr
      intro q
      split <;> omega⟩

theorem apply_action (A : Automaton n a) (K : Nat) (f : State n K) (v : Letter a) :
    applyAction A K f (action A v) = succ A K f v := rfl

/-- Complete output residual at an input; duplicate actions are harmless. -/
def responses (A : Automaton n a) (side : Bool) (i : Letter a) : List (Action n) :=
  unique ((letters a).map fun o => action A (merge A side i o))

theorem mem_responses (A : Automaton n a) (side : Bool) (i : Letter a) (t : Action n) :
    t ∈ responses A side i ↔ ∃ o, action A (merge A side i o) = t := by
  simp only [responses, mem_unique, List.mem_map]
  exact ⟨fun ⟨o, _, h⟩ => ⟨o, h⟩, fun ⟨o, h⟩ => ⟨o, mem_letters o, h⟩⟩

/-- Shannon input traversal. A leaf is an output residual; skipping an equal
    pair of children groups several input valuations into one partial cube. -/
inductive Diagram (a : Nat) (α : Type) where
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

/-- Reduced traversal still denotes the complete input-to-residual function. -/
theorem tabulate_eval [DecidableEq α] (F : Letter a → α) (ps : List (Fin a)) (base i : Letter a)
    (agree : ∀ p, p ∉ ps → base p = i p) : (tabulate F ps base).eval i = F i := by
  induction ps generalizing base with
  | nil =>
    have he : base = i := funext fun p => agree p (by simp)
    simp [tabulate, Diagram.eval, he]
  | cons p ps ih =>
    have ha : ∀ b, b = i p → ∀ q, q ∉ ps → assign base p b q = i q := by
      intro b hb q hq
      by_cases he : q = p
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

/-- Every valuation reaches a leaf, and every valuation in a leaf's cube has
    exactly that residual. These follow from traversal, not table contracts. -/
theorem diagram_rows (D : Diagram a α) :
    (∀ i, ∃ row ∈ D.rows, row.1 i = true) ∧
    (∀ row ∈ D.rows, ∀ i, row.1 i = true → D.eval i = row.2) := by
  induction D with
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
        obtain ⟨hp, hi⟩ := Bool.and_eq_true_iff.mp hi
        simpa [Diagram.eval, hp] using ih.2 r hm i hi

/-- C++ filter_to_representative_inputs keeps a whole cube on nonempty
    intersection. Its surviving key need not be a literal representative. -/
def restrict (reps : List (Letter a)) (rows : List (Row n a)) : List (Row n a) :=
  rows.filter fun row => reps.any row.1

/-- C++ compare_actions ignores the BDD key. Keep one entire row per equal
    action list, without merging discarded input keys. Order is immaterial. -/
def dedup : List (Row n a) → List (Row n a)
  | [] => []
  | row :: rows => row :: (dedup rows).filter (fun r => r.2 != row.2)

/-- Deduplication retains an original key and an equal action class for every
    original row, even when that row's own key is discarded. -/
theorem dedup_refines (rows : List (Row n a)) :
    (∀ row ∈ dedup rows, row ∈ rows) ∧
    (∀ row ∈ rows, ∃ kept ∈ dedup rows, kept.2 = row.2) := by
  induction rows with
  | nil => simp [dedup]
  | cons row rows ih =>
    rw [dedup]
    constructor
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

/-- Semantic class equality allows response splices and priority reordering. -/
def Class (A : Automaton n a) (side : Bool) (ts : List (Action n)) (i : Letter a) : Prop :=
  ∀ t, t ∈ ts ↔ ∃ o, action A (merge A side i o) = t

structure Refines (A : Automaton n a) (side : Bool) (reps : List (Letter a)) (rows : List (Row n a)) : Prop where
  covers : ∀ i ∈ reps, ∃ row ∈ rows, Class A side row.2 i
  legal : ∀ row ∈ rows, ∃ i ∈ reps, row.1 i = true ∧ Class A side row.2 i

/-- Enumerate an output diagram's reachable residuals. False path cubes
    contribute nothing, exactly as the BDD traversal's empty branches do. -/
def diagramActions (D : Diagram a (Action n)) : List (Action n) :=
  (D.rows.filter (fun row => (letters a).any row.1)).map Prod.snd

theorem mem_diagramActions (D : Diagram a (Action n)) (t : Action n) :
    t ∈ diagramActions D ↔ ∃ o, D.eval o = t := by
  simp only [diagramActions, List.mem_map, List.mem_filter, List.any_eq_true]
  constructor
  · rintro ⟨row, ⟨hr, o, _, hc⟩, rfl⟩
    exact ⟨o, (diagram_rows D).2 row hr o hc⟩
  · rintro ⟨o, he⟩
    obtain ⟨row, hr, hc⟩ := (diagram_rows D).1 o
    exact ⟨row, ⟨hr, o, mem_letters o, hc⟩, ((diagram_rows D).2 row hr o hc).symm.trans he⟩

/-- An input-frontier leaf carries an output diagram of endpoint relations.
    Input/output traversal orders and repeated action paths are retained. -/
def precomputerRows (D : Diagram a (Diagram a (Action n))) : List (Row n a) :=
  D.rows.map fun row => (row.1, diagramActions row.2)

def precomputerTable (D : Diagram a (Diagram a (Action n))) (reps : List (Letter a)) : List (Row n a) :=
  dedup (restrict reps (precomputerRows D))

/-- For any BDD input/output decomposition denoting the automaton relation,
    traversal, cube intersection and whole-row dedup derive the refinement.
    Only the BDD's extensional endpoint interpretation is a premise here;
    neither physical representative coverage nor action compatibility is. -/
theorem precomputer_refinement (A : Automaton n a) (side : Bool)
    (D : Diagram a (Diagram a (Action n)))
    (denotes : ∀ i o, (D.eval i).eval o = action A (merge A side i o)) (reps : List (Letter a)) :
    Refines A side reps (precomputerTable D reps) := by
  have complete : ∀ i, ∃ row ∈ precomputerRows D, row.1 i = true := by
    intro i
    obtain ⟨row, hr, hc⟩ := (diagram_rows D).1 i
    exact ⟨_, List.mem_map.mpr ⟨row, hr, rfl⟩, hc⟩
  have exactClass : ∀ row ∈ precomputerRows D, ∀ i, row.1 i = true → Class A side row.2 i := by
    intro row hr i hi t
    obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
    rw [mem_diagramActions, ← (diagram_rows D).2 r hm i hi]
    simp only [denotes]
  constructor
  · intro i hi
    obtain ⟨row, hr, hc⟩ := complete i
    have hm : row ∈ restrict reps (precomputerRows D) :=
      List.mem_filter.mpr ⟨hr, List.any_eq_true.mpr ⟨i, hi, hc⟩⟩
    obtain ⟨kept, hk, he⟩ := (dedup_refines _).2 row hm
    exact ⟨kept, hk, he ▸ exactClass row hr i hc⟩
  · intro row hr
    obtain ⟨hm, hc⟩ := List.mem_filter.mp ((dedup_refines _).1 row hr)
    obtain ⟨i, hi, hc⟩ := List.any_eq_true.mp hc
    exact ⟨i, hi, hc, exactClass row hm i hc⟩

/-- A fully executable truth-table construction supplies the endpoint
    interpretation without any premise. BDD sharing changes representation. -/
def precomputerDiagram (A : Automaton n a) (side : Bool) : Diagram a (Diagram a (Action n)) :=
  tabulate (fun i => tabulate (fun o => action A (merge A side i o))
    (List.finRange a) (fun _ => false)) (List.finRange a) (fun _ => false)

/-- Shannon construction denotes every merged input/output endpoint relation. -/
theorem precomputer_denotes (A : Automaton n a) (side : Bool) (i o : Letter a) :
    ((precomputerDiagram A side).eval i).eval o = action A (merge A side i o) := by
  unfold precomputerDiagram
  rw [tabulate_eval _ _ _ i (by simp), tabulate_eval _ _ _ o (by simp)]

/-- Grouped input cubes before representative restriction and row dedup. -/
def grouped (A : Automaton n a) (side : Bool) : List (Row n a) :=
  precomputerRows (precomputerDiagram A side)

/-- The physical construction keeps the first original key per action list. -/
def physical (A : Automaton n a) (side : Bool) (reps : List (Letter a)) : List (Row n a) :=
  precomputerTable (precomputerDiagram A side) reps

theorem matching_rows {xs ys : List (Row n a)} (h : SameRows xs ys) :
    (∀ x ∈ xs, ∃ y ∈ ys, x.1 = y.1 ∧ x.2.Perm y.2) ∧
    (∀ y ∈ ys, ∃ x ∈ xs, x.1 = y.1 ∧ x.2.Perm y.2) := by
  induction h with
  | nil => simp
  | @cons x y xs ys hp ht ih =>
    constructor
    · intro r hr
      rcases List.mem_cons.mp hr with rfl | hr
      · exact ⟨y, by simp, hp⟩
      · obtain ⟨s, hs, he⟩ := ih.1 r hr
        exact ⟨s, List.mem_cons_of_mem _ hs, he⟩
    · intro r hr
      rcases List.mem_cons.mp hr with rfl | hr
      · exact ⟨x, by simp, hp⟩
      · obtain ⟨s, hs, he⟩ := ih.2 r hr
        exact ⟨s, List.mem_cons_of_mem _ hs, he⟩

/-- Move-to-front caching preserves the quotient relation, including coverage
    of discarded input keys. Row permutations cover priority queue ordering. -/
theorem Refines.cached {A : Automaton n a} {side reps xs ys}
    (h : Refines A side reps ys) (same : SameRows xs ys) : Refines A side reps xs := by
  have hm := matching_rows same
  constructor
  · intro i hi
    obtain ⟨y, hy, hc⟩ := h.covers i hi
    obtain ⟨x, hx, _, hp⟩ := hm.2 y hy
    exact ⟨x, hx, fun t => hp.mem_iff.trans (hc t)⟩
  · intro x hx
    obtain ⟨y, hy, he, hp⟩ := hm.1 x hx
    obtain ⟨i, hi, hk, hc⟩ := h.legal y hy
    exact ⟨i, hi, he ▸ hk, fun t => hp.mem_iff.trans (hc t)⟩

theorem Refines.permuted {A : Automaton n a} {side reps xs ys}
    (h : Refines A side reps ys) (perm : xs.Perm ys) : Refines A side reps xs where
  covers i hi := let ⟨row, hr, hc⟩ := h.covers i hi; ⟨row, perm.mem_iff.mpr hr, hc⟩
  legal row hr := h.legal row (perm.mem_iff.mp hr)

theorem Refines.scan {A : Automaton n a} {side reps rows} (h : Refines A side reps rows)
    (K : Nat) (maxima : List (State n K)) :
    Refines A side reps (criticalCached (applyAction A K) maxima rows).2 :=
  h.cached (scanCriticalMaxima_exact _ _ _ _).1

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

/-- The returned priority cache also preserves the relation: sorting keeps
    rows, splicing keeps action classes, and zip restores every queue entry. -/
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
  unfold criticalPQCached
  change Refines A side reps ((ordered.zipWith _ _).map Prod.snd)
  rw [zip_rows ordered _ _ hl]
  exact hc

/-- The construction supplies both semantic table contracts without assuming
    literal representative keys or literal compatible response valuations. -/
theorem physical_refinement (A : Automaton n a) (side : Bool) (reps : List (Letter a)) :
    Refines A side reps (physical A side reps) :=
  precomputer_refinement A side (precomputerDiagram A side) (precomputer_denotes A side) reps

/-- Each action class has exactly the legal input's full response predecessor. -/
theorem class_predecessor_exact (A : Automaton n a) (K : Nat) (side : Bool) (X : Region n K)
    (ts : List (Action n)) (i : Letter a) (he : Class A side ts i) (f : State n K) :
    ts.any (fun t => X (applyAction A K f t)) = computeT A K side X i f := by
  apply Bool.eq_iff_iff.mpr
  rw [computeT_iff]
  unfold Class at he
  simp only [List.any_eq_true, he]
  constructor
  · rintro ⟨_, ⟨o, rfl⟩, hs⟩
    exact ⟨o, hs⟩
  · rintro ⟨o, hs⟩
    exact ⟨_, ⟨o, rfl⟩, hs⟩

/-- Port of backward payload application; the same endpoint list controls
    both forward max/increment and backward min/subtract. -/
def backwardAction (A : Automaton n a) (K : Nat) (m : State n K) (t : Action n) : State n K :=
  fun q => ⟨min (safeCap A K q) (minStates (K + 1) (fun r =>
    if t q r then (m r).val - (if A.rejecting r then 1 else 0) else K + 1)), by
      have hc : safeCap A K q ≤ K + 1 := by unfold safeCap; split <;> omega
      have := Nat.min_le_left (safeCap A K q) (minStates (K + 1) (fun r =>
        if t q r then (m r).val - (if A.rejecting r then 1 else 0) else K + 1))
      omega⟩

def classBackward (A : Automaton n a) (K : Nat) (maxima : List (State n K)) (ts : List (Action n)) : Region n K :=
  fun f => ts.any fun t => maxima.any fun m => below f (backwardAction A K m t)

/-- Full response completeness also gives the exact backward union, rather
    than just a subset of compatible actions sufficient for exhaustion. -/
theorem class_backward_exact (A : Automaton n a) (K : Nat) (side : Bool)
    (maxima : List (State n K)) (ts : List (Action n)) (i : Letter a) (he : Class A side ts i) :
    classBackward A K maxima ts = backwardT A K side maxima i := by
  funext f
  apply Bool.eq_iff_iff.mpr
  unfold Class at he
  simp only [classBackward, backwardT, List.any_eq_true, he]
  constructor
  · rintro ⟨_, ⟨o, rfl⟩, m, hm, hs⟩
    exact ⟨o, mem_letters o, m, hm, hs⟩
  · rintro ⟨o, _, m, hm, hs⟩
    exact ⟨_, ⟨o, rfl⟩, m, hm, hs⟩

def update (A : Automaton n a) (K : Nat) (G : List (Verified A))
    (maxima : List (State n K)) (row : Row n a) : Region n K :=
  close G (fun f => downset maxima f && classBackward A K maxima row.2 f)

/-- Every selected physical class is exactly an update at a legal input.
    Thus the existing update-preservation theorem applies to the C++ payload. -/
theorem selected_refinement (A : Automaton n a) (K : Nat) (positive : 1 ≤ K) (side : Bool)
    (G : List (Verified A)) (maxima : List (State n K))
    (safeMax : ∀ m ∈ maxima, safe A K m = true) (reps : List (Letter a)) (rows : List (Row n a))
    (refines : Refines A side reps rows) (row : Row n a) (hr : row ∈ rows) :
    ∃ i ∈ reps, row.1 i = true ∧ update A K G maxima row = representativeUpdate K side G i (downset maxima) := by
  obtain ⟨i, hi, hc, he⟩ := refines.legal row hr
  refine ⟨i, hi, hc, ?_⟩
  unfold update representativeUpdate
  rw [class_backward_exact A K side maxima row.2 i he]
  congr 1
  funext f
  exact backward_update_exact K positive side maxima safeMax i f

/-- The virtual table contains all representatives and all their responses.
    It is proof data; the physical solver never constructs these extra rows. -/
def virtual (A : Automaton n a) (side : Bool) (reps : List (Letter a)) :
    List (Letter a × List (Letter a)) :=
  reps.map fun i => (i, (letters a).map (merge A side i))

theorem virtual_retained (A : Automaton n a) (side : Bool) (P : SweepPlan A side) :
    ∀ O ∈ P.orbits, ∃ row ∈ virtual A side (P.orbits.map InputOrbit.representative),
      row.1 = O.representative := by
  intro O ho
  exact ⟨_, List.mem_map.mpr ⟨_, List.mem_map.mpr ⟨O, ho, rfl⟩, rfl⟩, rfl⟩

theorem virtual_compatible (A : Automaton n a) (side : Bool) (reps : List (Letter a)) :
    ∀ row ∈ virtual A side reps, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o := by
  intro row hr v hv
  obtain ⟨i, _, rfl⟩ := List.mem_map.mp hr
  obtain ⟨o, _, rfl⟩ := List.mem_map.mp hv
  exact ⟨o, rfl⟩

/-- Physical exhaustion transfers through action classes to all virtual rows.
    Bare SIMD storage uses the scalar apply; Boolean compression uses closure. -/
theorem virtual_exhausted (A : Automaton n a) (closed : BooleanClosed A) (K : Nat) (side : Bool)
    (maxima : List (State n K)) (safeMax : ∀ m ∈ maxima, safe A K m = true)
    (reps : List (Letter a)) (rows : List (Row n a)) (refines : Refines A side reps rows)
    (exhausted : (criticalCached (applyAction A K) maxima rows).1 = none) :
    (criticalCached (fun f v => storedSucc A K f v) maxima (virtual A side reps)).1 = none := by
  rw [criticalCached_none, criticalPicker_none]
  have hx := (criticalPicker_none _ _ _).mp ((criticalCached_none _ _ _).mp exhausted)
  intro m hm vrow hv
  obtain ⟨i, hi, rfl⟩ := List.mem_map.mp hv
  obtain ⟨row, hr, he⟩ := refines.covers i hi
  obtain ⟨t, ht, hs⟩ := hx m hm row hr
  obtain ⟨o, rfl⟩ := (he t).mp ht
  refine ⟨_, List.mem_map.mpr ⟨o, mem_letters o, rfl⟩, ?_⟩
  rw [storedSucc_exact closed K m (safeMax m hm)]
  exact hs

/-- Completion also holds after cache splices or bound raises: the table
    relation and the region invariant are preserved separately by the loop. -/
theorem refined_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (invariants : Subset (final A K side) (downset maxima) ∧
      Subset (downset maxima) (safe A K) ∧ Symmetric P.generators (downset maxima) ∧
      downset maxima (initial A K) = true)
    (rows : List (Row n a)) (refines : Refines A side (P.orbits.map InputOrbit.representative) rows)
    (exhausted : (criticalCached (applyAction A K) maxima rows).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  have hm : ∀ m ∈ maxima, safe A K m = true := by
    intro m hmem
    exact invariants.2.1 m (List.any_eq_true.mpr ⟨m, hmem, decide_eq_true (fun _ => Nat.le_refl _)⟩)
  exact (picker_completed_of_invariants closed K positive side P maxima invariants _
    (virtual_retained A side P) (virtual_compatible A side _)
    (virtual_exhausted A closed K side maxima hm _ rows refines exhausted)).2

/-- The physical input/output traversal also instantiates completion directly,
    with all table contracts derived from the raw endpoint relation. -/
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

/-- This instantiates picker_completed through the constructed quotient.
    Retained and compatible are derived for virtual rows, never assumed of
    the physical BDD keys, which may have discarded representatives. -/
theorem physical_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (exhausted : (criticalCached (applyAction A K) maxima
      (physical A side (P.orbits.map InputOrbit.representative))).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  have hs := (picker_reach_invariants reached).2.1
  have hm : ∀ m ∈ maxima, safe A K m = true := by
    intro m hmem
    exact hs m (List.any_eq_true.mpr ⟨m, hmem, decide_eq_true (fun _ => Nat.le_refl _)⟩)
  exact (picker_completed closed K positive side P maxima reached _
    (virtual_retained A side P) (virtual_compatible A side _)
    (virtual_exhausted A closed K side maxima hm _ _ (physical_refinement A side _) exhausted)).2

/-- Selected physical updates preserve the winner and remain in PickerReach;
    this connects literal backward payload updates to the modeled while loop. -/
theorem physical_update_reach (A : Automaton n a) (K : Nat) (positive : 1 ≤ K) (side : Bool)
    (P : SweepPlan A side) (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (row : Row n a) (hr : row ∈ physical A side (P.orbits.map InputOrbit.representative))
    (hin : update A K P.generators maxima row (initial A K) = true) :
    Subset (final A K side) (update A K P.generators maxima row) ∧
    PickerReach A K side P.generators (update A K P.generators maxima row) := by
  have inv := picker_reach_invariants reached
  have hm : ∀ m ∈ maxima, safe A K m = true := by
    intro m hmem
    exact inv.2.1 m (List.any_eq_true.mpr ⟨m, hmem, decide_eq_true (fun _ => Nat.le_refl _)⟩)
  obtain ⟨i, _, _, he⟩ := selected_refinement A K positive side P.generators maxima hm _ _
    (physical_refinement A side _) row hr
  rw [he] at hin ⊢
  exact ⟨selected_preserves K side P.generators i _ inv.1, .update reached i hin⟩

end Symmetry.ActionTable

import SymmetryProofs.CachedPickers
import SymmetryProofs.Enumeration

namespace Symmetry
variable {A : Automaton n a}

/-- C++: boolean_states/forward_saturation.hh:48–53. Raise each destination
    to the source's saturated rank. Labels do not restrict this preprocessing. -/
def spreadRank (value : Nat) (edges : List (Edge n a)) (rank : Fin n → Nat) : Fin n → Nat :=
  edges.foldl (fun r e => fun q => if q = e.dst then max (r q) value else r q) rank

theorem spreadRank_grows (value : Nat) (edges : List (Edge n a)) (rank : Fin n → Nat) :
    ∀ q, rank q ≤ spreadRank value edges rank q := by
  induction edges generalizing rank with
  | nil => intro q; exact Nat.le_refl _
  | cons e es ih =>
    intro q
    apply Nat.le_trans _ (ih _ q)
    change rank q ≤ if q = e.dst then max (rank q) value else rank q
    split <;> omega

theorem spreadRank_edge (value : Nat) (edges : List (Edge n a)) (rank : Fin n → Nat)
    (e : Edge n a) (he : e ∈ edges) : value ≤ spreadRank value edges rank e.dst := by
  induction edges generalizing rank with
  | nil => contradiction
  | cons d ds ih =>
    rcases List.mem_cons.mp he with rfl | he
    · exact Nat.le_trans (by simp; omega) (spreadRank_grows value ds _ e.dst)
    · exact ih _ he

/-- C++: boolean_states/forward_saturation.hh:45–55. The source's rank is
    read once, then all its outgoing edges are updated in place. -/
def saturateSource (A : Automaton n a) (limit : Nat) (q : Fin n) (rank : Fin n → Nat) : Fin n → Nat :=
  spreadRank (min (limit + 1) (rank q + if A.rejecting q then 1 else 0))
    (A.edges.filter fun e => e.src == q) rank

/-- C++: boolean_states/forward_saturation.hh:42–56. One complete in-place
    pass. The C++ exits only after a pass performs no strict rank increase. -/
def saturationPass (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat) : Fin n → Nat :=
  (List.finRange n).foldl (fun r q => saturateSource A limit q r) rank

private theorem fold_rank_grows (xs : List α) (step : α → (Fin n → Nat) → Fin n → Nat)
    (grows : ∀ x r q, r q ≤ step x r q) (rank : Fin n → Nat) :
    ∀ q, rank q ≤ xs.foldl (fun r x => step x r) rank q := by
  induction xs generalizing rank with
  | nil => intro q; exact Nat.le_refl _
  | cons x xs ih => intro q; exact Nat.le_trans (grows x rank q) (ih _ q)

private theorem fold_rank_fixed (xs : List α) (step : α → (Fin n → Nat) → Fin n → Nat)
    (grows : ∀ x r q, r q ≤ step x r q) (rank : Fin n → Nat)
    (fixed : xs.foldl (fun r x => step x r) rank = rank) :
    ∀ x ∈ xs, step x rank = rank := by
  induction xs generalizing rank with
  | nil => simp
  | cons x xs ih =>
    have hx : step x rank = rank := by
      funext q
      apply Nat.le_antisymm _ (grows x rank q)
      have h := fold_rank_grows xs step grows (step x rank) q
      have he : xs.foldl (fun r x => step x r) (step x rank) = rank := fixed
      rw [he] at h
      exact h
    have ht : xs.foldl (fun r x => step x r) rank = rank := by simpa [hx] using fixed
    intro y hy
    rcases List.mem_cons.mp hy with rfl | hy
    · exact hx
    · exact ih rank ht y hy

/-- A no-change saturation pass has propagated every source rank across every
    edge. This is all picker correctness needs from the preprocessing loop;
    no assumption about how many earlier passes ran is required. -/
theorem saturationPass_edges (limit : Nat) (rank : Fin n → Nat)
    (fixed : saturationPass A limit rank = rank) (e : Edge n a) (he : e ∈ A.edges) :
    min (limit + 1) (rank e.src + if A.rejecting e.src then 1 else 0) ≤ rank e.dst := by
  have hsource := fold_rank_fixed (List.finRange n) (saturateSource A limit)
    (fun _ r => spreadRank_grows _ _ r) rank fixed e.src (List.mem_finRange _)
  have he' : e ∈ A.edges.filter (fun d => d.src == e.src) := by simp [he]
  have h := spreadRank_edge (min (limit + 1) (rank e.src + if A.rejecting e.src then 1 else 0))
    (A.edges.filter fun d => d.src == e.src) rank e he'
  change _ ≤ saturateSource A limit e.src rank e.dst at h
  rw [hsource] at h
  exact h

private theorem spreadRank_bound (cap value : Nat) (edges : List (Edge n a))
    (rank : Fin n → Nat) (hv : value ≤ cap) (hr : ∀ q, rank q ≤ cap) :
    ∀ q, spreadRank value edges rank q ≤ cap := by
  induction edges generalizing rank with
  | nil => exact hr
  | cons e es ih =>
    apply ih
    intro q
    dsimp only
    split <;> have := hr q <;> omega

private theorem saturationPass_bound (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat)
    (h : ∀ q, rank q ≤ limit + 1) : ∀ q, saturationPass A limit rank q ≤ limit + 1 := by
  have go : ∀ xs : List (Fin n), ∀ r, (∀ q, r q ≤ limit + 1) →
      ∀ q, xs.foldl (fun r q => saturateSource A limit q r) r q ≤ limit + 1 := by
    intro xs
    induction xs with
    | nil => intro r h; exact h
    | cons q qs ih =>
      intro r h
      apply ih
      exact spreadRank_bound _ _ _ r (Nat.min_le_left ..) h
  exact go _ rank h

private theorem sum_map_le (xs : List α) (f g : α → Nat) (h : ∀ x ∈ xs, f x ≤ g x) :
    (xs.map f).sum ≤ (xs.map g).sum := by
  induction xs with
  | nil => simp
  | cons x xs ih =>
    have hx := h x (by simp)
    have ht := ih (fun y hy => h y (by simp [hy]))
    simp only [List.map_cons, List.sum_cons]
    omega

private theorem sum_map_lt (xs : List α) (f g : α → Nat) (h : ∀ x ∈ xs, f x ≤ g x)
    (witness : ∃ x ∈ xs, f x < g x) : (xs.map f).sum < (xs.map g).sum := by
  induction xs with
  | nil => simp at witness
  | cons x xs ih =>
    have hx := h x (by simp)
    obtain ⟨y, hy, hlt⟩ := witness
    simp only [List.map_cons, List.sum_cons]
    rcases List.mem_cons.mp hy with rfl | hy
    · have ht := sum_map_le xs f g (fun z hz => h z (by simp [hz]))
      omega
    · have ht := ih (fun z hz => h z (by simp [hz])) ⟨y, hy, hlt⟩
      omega

private theorem rank_sum_bound (rank : Fin n → Nat) (cap : Nat) (h : ∀ q, rank q ≤ cap) :
    ((List.finRange n).map rank).sum ≤ n * cap := by
  have hs := sum_map_le (List.finRange n) rank (fun _ => cap) (fun q _ => h q)
  have hconst : ∀ xs : List (Fin n), (xs.map (fun _ => cap)).sum = xs.length * cap := by
    intro xs
    induction xs with
    | nil => simp
    | cons q qs ih => simp [ih, Nat.add_mul, Nat.add_comm]
  simpa only [hconst, List.length_finRange] using hs

/-- C++: boolean_states/forward_saturation.hh:40–56. Total in-place rank
    saturation. A strict pass raises the rank sum, bounded by states × cap.
    Since updates only raise ranks, equality is exactly has_changed == false. -/
def saturateRanks (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat)
    (bound : ∀ q, rank q ≤ limit + 1) : {r : Fin n → Nat // saturationPass A limit r = r} :=
  let next := saturationPass A limit rank
  if h : ∀ q, next q = rank q then ⟨rank, funext h⟩
  else saturateRanks A limit next (saturationPass_bound A limit rank bound)
termination_by n * (limit + 1) - ((List.finRange n).map rank).sum
decreasing_by
  have grows := fold_rank_grows (List.finRange n) (saturateSource A limit)
    (fun _ r => spreadRank_grows _ _ r) rank
  have strict : ∃ q ∈ List.finRange n, rank q < saturationPass A limit rank q := by
    apply Classical.byContradiction
    intro hn
    apply h
    intro q
    have hg := grows q
    change rank q ≤ saturationPass A limit rank q at hg
    have hm := List.mem_finRange q
    apply Classical.byContradiction
    intro he
    change saturationPass A limit rank q ≠ rank q at he
    exact hn ⟨q, hm, by omega⟩
  have hs := sum_map_lt (List.finRange n) rank (saturationPass A limit rank)
    (fun q _ => grows q) strict
  have hb := rank_sum_bound (saturationPass A limit rank) (limit + 1)
    (saturationPass_bound A limit rank bound)
  omega

/-- C++: boolean_states/forward_saturation.hh:29–38. Saturate from the actual
    initial rank vector with cap number-of-accepting-states + 1. -/
def saturation (A : Automaton n a) : Sigma fun limit =>
    {r : Fin n → Nat // saturationPass A limit r = r} :=
  let limit := ((List.finRange n).filter A.rejecting).length
  ⟨limit, saturateRanks A limit (fun q => if q = A.init ∧ A.rejecting q = true then 1 else 0)
    (by intro q; split <;> omega)⟩

/-- C++: boolean_states/forward_saturation.hh:60–69. The bounded states are
    Boolean and have their acceptance marks cleared before state reordering. -/
def booleanized (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat) : Automaton n a :=
  { A with
    counting := fun q => decide (rank q > limit)
    rejecting := fun q => A.rejecting q && decide (rank q > limit) }

/-- Every returned saturation result is Boolean-closed: an unbounded source
    cannot reach a bounded destination, and bounded destinations do not reject.
    Thus the forward-storage picker theorem applies after this source phase. -/
theorem booleanized_closed (limit : Nat) (rank : Fin n → Nat)
    (fixed : saturationPass A limit rank = rank) : BooleanClosed (booleanized A limit rank) := by
  intro q r v he hr
  obtain ⟨e, hm, hs, hd, _⟩ := (enabled_iff (booleanized A limit rank) q r v).mp he
  have h := saturationPass_edges limit rank fixed e hm
  simp only [booleanized] at hr ⊢
  have hdest : rank r ≤ limit := by simpa using hr
  have hsrc : rank q ≤ limit := by
    rw [hs, hd] at h
    split at h <;> omega
  simp [show ¬rank q > limit by omega, show ¬rank r > limit by omega]

/-- C++: boolean_states/forward_saturation.hh:82–87. Renaming states changes
    indices only; keep acceptance, the Boolean split, and edges together. -/
def renameStates (A : Automaton n a) (p : Perm (Fin n)) : Automaton n a where
  init := p A.init
  rejecting q := A.rejecting (p.invFun q)
  counting q := A.counting (p.invFun q)
  isInput := A.isInput
  edges := A.edges.map fun e => ⟨p e.src, e.label, p e.dst⟩

/-- State reordering preserves Boolean closure, so moving the Boolean suffix
    behind counting states does not weaken the forward/backward bridge. -/
theorem renameStates_closed (closed : BooleanClosed A) (p : Perm (Fin n)) :
    BooleanClosed (renameStates A p) := by
  intro q r v he hr
  obtain ⟨e, hm, hs, hd, hl⟩ := (enabled_iff (renameStates A p) q r v).mp he
  obtain ⟨original, ho, rfl⟩ := List.mem_map.mp hm
  have he' : A.enabled original.src original.dst v = true :=
    (enabled_iff A _ _ _).mpr ⟨original, ho, rfl, rfl, hl⟩
  have hdest : A.counting original.dst = false := by simpa [renameStates, ← hd] using hr
  obtain ⟨hc, ha⟩ := closed _ _ _ he' hdest
  simpa [renameStates, ← hs, ← hd] using And.intro hc ha

/-- C++: boolean_states/forward_saturation.hh:60–87. Clear bounded-state
    acceptance and put all counting states before the Boolean suffix. -/
def preprocessBoolean (A : Automaton n a) : Automaton n a :=
  let ranks := saturation A
  let B := booleanized A ranks.1 ranks.2.val
  let order := (List.finRange n).filter B.counting ++ (List.finRange n).filter (fun q => !B.counting q)
  renameStates B (Builders.slotsPerm order (List.filter_append_perm B.counting _)).symm

/-- The executable preprocessing port always supplies the Boolean-closure
    condition needed by the stored-forward picker, including after reordering.
    The invalid one-state Boolean/rejecting mutant is therefore excluded. -/
theorem preprocessBoolean_closed (A : Automaton n a) : BooleanClosed (preprocessBoolean A) := by
  exact renameStates_closed (booleanized_closed _ _ (saturation A).2.property) _

end Symmetry

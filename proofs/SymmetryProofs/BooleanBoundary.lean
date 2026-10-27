import SymmetryProofs.Pickers
import SymmetryProofs.Enumeration

namespace Symmetry
variable {A : Automaton n a}
def spreadRank (value : Nat) (edges : List (Edge n a)) (rank : Fin n → Nat) : Fin n → Nat :=
  edges.foldl (fun r e => fun q => if q = e.dst then max (r q) value else r q) rank
theorem spreadRank_grows (value : Nat) (edges : List (Edge n a)) (rank : Fin n → Nat) :
    ∀ q, rank q ≤ spreadRank value edges rank q := by
  induction edges generalizing rank with
  | nil => intro q; exact Nat.le_refl _
  | cons e es ih =>
    intro q; apply Nat.le_trans _ (ih _ q)
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
def saturateSource (A : Automaton n a) (limit : Nat) (q : Fin n) (rank : Fin n → Nat) : Fin n → Nat :=
  spreadRank (min (limit + 1) (rank q + if A.rejecting q then 1 else 0))
    (A.edges.filter fun e => e.src == q) rank
-- In-place rank propagation reads each source once; forward_saturation.hh:42–56.
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
    (fixed : xs.foldl (fun r x => step x r) rank = rank) : ∀ x ∈ xs, step x rank = rank := by
  induction xs generalizing rank with
  | nil => simp
  | cons x xs ih =>
    have hx : step x rank = rank := by
      funext q; apply Nat.le_antisymm _ (grows x rank q)
      have h := fold_rank_grows xs step grows (step x rank) q
      have he : xs.foldl (fun r x => step x r) (step x rank) = rank := fixed
      rw [he] at h; exact h
    have ht : xs.foldl (fun r x => step x r) rank = rank := by simpa [hx] using fixed
    intro y hy; rcases List.mem_cons.mp hy with rfl | hy
    · exact hx
    · exact ih rank ht y hy
-- A stable pass propagates every edge constraint; forward_saturation.hh:42–56.
theorem saturationPass_edges (limit : Nat) (rank : Fin n → Nat)
    (fixed : saturationPass A limit rank = rank) (e : Edge n a) (he : e ∈ A.edges) :
    min (limit + 1) (rank e.src + if A.rejecting e.src then 1 else 0) ≤ rank e.dst := by
  have hsource := fold_rank_fixed (List.finRange n) (saturateSource A limit)
    (fun _ r => spreadRank_grows _ _ r) rank fixed e.src (List.mem_finRange _)
  have he' : e ∈ A.edges.filter (fun d => d.src == e.src) := by simp [he]
  have h := spreadRank_edge (min (limit + 1) (rank e.src + if A.rejecting e.src then 1 else 0))
    (A.edges.filter fun d => d.src == e.src) rank e he'
  change _ ≤ saturateSource A limit e.src rank e.dst at h
  rw [hsource] at h; exact h
private theorem spreadRank_bound (cap value : Nat) (edges : List (Edge n a))
    (rank : Fin n → Nat) (hv : value ≤ cap) (hr : ∀ q, rank q ≤ cap) :
    ∀ q, spreadRank value edges rank q ≤ cap := by
  induction edges generalizing rank with
  | nil => exact hr
  | cons e es ih =>
    apply ih; intro q
    dsimp only; split <;> have := hr q <;> omega
private theorem saturationPass_bound (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat)
    (h : ∀ q, rank q ≤ limit + 1) : ∀ q, saturationPass A limit rank q ≤ limit + 1 := by
  have go : ∀ xs : List (Fin n), ∀ r, (∀ q, r q ≤ limit + 1) →
      ∀ q, xs.foldl (fun r q => saturateSource A limit q r) r q ≤ limit + 1 := by
    intro xs; induction xs with
    | nil => intro r h; exact h
    | cons q qs ih =>
      intro r h; apply ih
      exact spreadRank_bound _ _ _ r (Nat.min_le_left ..) h
  exact go _ rank h
private theorem sum_map_le (xs : List α) (f g : α → Nat) (h : ∀ x ∈ xs, f x ≤ g x) :
    (xs.map f).sum ≤ (xs.map g).sum := by
  induction xs with
  | nil => simp
  | cons x xs ih =>
    have hx := h x (by simp)
    have ht := ih (fun y hy => h y (by simp [hy])); simp only [List.map_cons, List.sum_cons]
    omega
private theorem sum_map_lt (xs : List α) (f g : α → Nat) (h : ∀ x ∈ xs, f x ≤ g x)
    (witness : ∃ x ∈ xs, f x < g x) : (xs.map f).sum < (xs.map g).sum := by
  induction xs with
  | nil => simp at witness
  | cons x xs ih =>
    have hx := h x (by simp)
    obtain ⟨y, hy, hlt⟩ := witness; simp only [List.map_cons, List.sum_cons]
    rcases List.mem_cons.mp hy with rfl | hy
    · have ht := sum_map_le xs f g (fun z hz => h z (by simp [hz]))
      omega
    · have ht := ih (fun z hz => h z (by simp [hz])) ⟨y, hy, hlt⟩
      omega
private theorem rank_sum_bound (rank : Fin n → Nat) (cap : Nat) (h : ∀ q, rank q ≤ cap) :
    ((List.finRange n).map rank).sum ≤ n * cap := by
  have hs := sum_map_le (List.finRange n) rank (fun _ => cap) (fun q _ => h q)
  simpa only [List.map_const', List.sum_replicate_nat, List.length_finRange] using hs
def saturateRanks (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat)
    (bound : ∀ q, rank q ≤ limit + 1) : {r : Fin n → Nat // saturationPass A limit r = r} :=
  let next := saturationPass A limit rank -- Strict passes raise a bounded rank sum, proving termination; forward_saturation.hh:40–56.
  if h : ∀ q, next q = rank q then ⟨rank, funext h⟩
  else saturateRanks A limit next (saturationPass_bound A limit rank bound)
termination_by n * (limit + 1) - ((List.finRange n).map rank).sum
decreasing_by
  have grows := fold_rank_grows (List.finRange n) (saturateSource A limit)
    (fun _ r => spreadRank_grows _ _ r) rank
  have strict : ∃ q ∈ List.finRange n, rank q < saturationPass A limit rank q := by
    apply Classical.byContradiction; intro hn
    apply h; intro q
    have hg := grows q
    change rank q ≤ saturationPass A limit rank q at hg
    have hm := List.mem_finRange q
    apply Classical.byContradiction; intro he
    change saturationPass A limit rank q ≠ rank q at he; exact hn ⟨q, hm, by omega⟩
  have hs := sum_map_lt (List.finRange n) rank (saturationPass A limit rank)
    (fun q _ => grows q) strict
  have hb := rank_sum_bound (saturationPass A limit rank) (limit + 1)
    (saturationPass_bound A limit rank bound)
  omega
def saturation (A : Automaton n a) : Sigma fun limit =>
    {r : Fin n → Nat // saturationPass A limit r = r} :=
  let limit := ((List.finRange n).filter A.rejecting).length
  ⟨limit, saturateRanks A limit (fun q => if q = A.init ∧ A.rejecting q = true then 1 else 0)
    (by intro q; split <;> omega)⟩
def booleanized (A : Automaton n a) (limit : Nat) (rank : Fin n → Nat) : Automaton n a :=
  { A with -- Bounded states become Boolean and lose acceptance marks; forward_saturation.hh:60–69.
    counting := fun q => decide (rank q > limit)
    rejecting := fun q => A.rejecting q && decide (rank q > limit) }
-- Saturation and clearing establish the graph/storage condition; forward_saturation.hh:60–69.
theorem booleanized_closed (limit : Nat) (rank : Fin n → Nat)
    (fixed : saturationPass A limit rank = rank) : BooleanClosed (booleanized A limit rank) := by
  intro q r v he hr; obtain ⟨e, hm, hs, hd, _⟩ := (enabled_iff (booleanized A limit rank) q r v).mp he
  have h := saturationPass_edges limit rank fixed e hm; simp only [booleanized] at hr ⊢
  have hdest : rank r ≤ limit := by simpa using hr
  have hsrc : rank q ≤ limit := by
    rw [hs, hd] at h; split at h <;> omega
  simp [show ¬rank q > limit by omega, show ¬rank r > limit by omega]
def renameStates (A : Automaton n a) (p : Perm (Fin n)) : Automaton n a where
  init := p A.init
  rejecting q := A.rejecting (p.invFun q)
  counting q := A.counting (p.invFun q)
  isInput := A.isInput
  edges := A.edges.map fun e => ⟨p e.src, e.label, p e.dst⟩
-- State reordering preserves the graph/storage condition; forward_saturation.hh:82–87.
theorem renameStates_closed (closed : BooleanClosed A) (p : Perm (Fin n)) :
    BooleanClosed (renameStates A p) := by
  intro q r v he hr; obtain ⟨e, hm, hs, hd, hl⟩ := (enabled_iff (renameStates A p) q r v).mp he
  obtain ⟨original, ho, rfl⟩ := List.mem_map.mp hm
  have he' : A.enabled original.src original.dst v = true :=
    (enabled_iff A _ _ _).mpr ⟨original, ho, rfl, rfl, hl⟩
  have hdest : A.counting original.dst = false := by simpa [renameStates, ← hd] using hr
  obtain ⟨hc, ha⟩ := closed _ _ _ he' hdest; simpa [renameStates, ← hs, ← hd] using And.intro hc ha
def preprocessBoolean (A : Automaton n a) : Automaton n a :=
  let ranks := saturation A -- Saturate, clear acceptance, then move counting states first; forward_saturation.hh:29–87.
  let B := booleanized A ranks.1 ranks.2.val
  let order := (List.finRange n).filter B.counting ++ (List.finRange n).filter (fun q => !B.counting q)
  renameStates B (Builders.slotsPerm order (List.filter_append_perm B.counting _)).symm
-- Executable preprocessing supplies Boolean closure; forward_saturation.hh:29–87.
theorem preprocessBoolean_closed (A : Automaton n a) : BooleanClosed (preprocessBoolean A) := by exact renameStates_closed (booleanized_closed _ _ (saturation A).2.property) _

variable {A : Automaton n a}
theorem initial_safe (K : Nat) (hk : 1 ≤ K) : safe A K (initial A K) = true := by
  apply decide_eq_true; intro q
  unfold initial; split <;> cases A.counting q <;> simp_all
-- Arbitrary input histories retain initial-out tests between calls; equivariant_k_bounded_safety_aut.hh:905–955.
inductive PickerReach (A : Automaton n a) (K : Nat) (side : Bool) (G : List (Verified A)) : Region n K → Prop
  | start (positive : 1 ≤ K) : PickerReach A K side G (safe A K)
  | update {X : Region n K} (prior : PickerReach A K side G X) (i : Fin a → Bool)
      (initial_in : representativeUpdate K side G i X (initial A K) = true) :
      PickerReach A K side G (representativeUpdate K side G i X)
theorem picker_reach_invariants (h : PickerReach A K side G X) :
    Subset (final A K side) X ∧ Subset X (safe A K) ∧ Symmetric G X ∧ X (initial A K) = true := by
  induction h with -- Reachable calls are safe, symmetric and contain winners and init; equivariant_k_bounded_safety_aut.hh:905–955.
  | start positive =>
    exact ⟨stage_safe K side _, (fun _ h => h), (fun s _ => safe_commutes s K), initial_safe K positive⟩
  | update _ i hin ih =>
    refine ⟨selected_preserves K side G i _ ih.1, ?_, close_invariant G _, hin⟩
    intro f hf; exact ih.2.1 f (representativeUpdate_shrinks K side G i _ f hf)
theorem picker_completed_of_invariants (closed : BooleanClosed A) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (invariants : Subset (final A K side) (downset maxima) ∧
      Subset (downset maxima) (safe A K) ∧ Symmetric P.generators (downset maxima) ∧
      downset maxima (initial A K) = true)
    (rows : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ rows, row.1 = O.representative)
    (compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o)
    (exhausted : (criticalCached (fun f v => storedSucc A K f v) maxima rows).1 = none) :
    (∀ row ∈ rows, (fun f => downset maxima f && backwardT A K side maxima row.1 f) = downset maxima) ∧
      downset maxima = final A K side ∧ winner A K side = true := by
  obtain ⟨contains, safeRegion, symmetric, initialIn⟩ := invariants
  have safeMax := maxima_safe safeRegion
  have scalar := stored_picker_none closed K maxima safeMax rows exhausted
  have he := picker_success_eq K side P maxima safeMax contains symmetric rows retained scalar compatible
  refine ⟨picker_none_backward K positive side maxima safeMax rows scalar compatible, he, ?_⟩
  change final A K side (initial A K) = true
  rw [← he]; exact initialIn
-- Reachable stored exhaustion yields backward constraints and exact success; equivariant_k_bounded_safety_aut.hh:936–942.
theorem picker_completed (closed : BooleanClosed A) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (rows : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ rows, row.1 = O.representative)
    (compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o)
    (exhausted : (criticalCached (fun f v => storedSucc A K f v) maxima rows).1 = none) :
    (∀ row ∈ rows, (fun f => downset maxima f && backwardT A K side maxima row.1 f) = downset maxima) ∧
      downset maxima = final A K side ∧ winner A K side = true :=
  picker_completed_of_invariants closed K positive side P maxima
    (picker_reach_invariants reached) rows retained compatible exhausted
-- Preprocessing supplies completion’s Boolean graph premise; forward_saturation.hh:29–87.
theorem preprocessed_picker_completed (raw : Automaton n a) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan (preprocessBoolean raw) side) (maxima : List (State n K))
    (reached : PickerReach (preprocessBoolean raw) K side P.generators (downset maxima))
    (rows : List ((Fin a → Bool) × List (Fin a → Bool)))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ rows, row.1 = O.representative)
    (compatible : ∀ row ∈ rows, ∀ v ∈ row.2, ∃ o, v = merge (preprocessBoolean raw) side row.1 o)
    (exhausted : (criticalCached (fun f v => storedSucc (preprocessBoolean raw) K f v) maxima rows).1 = none) :
    downset maxima = final (preprocessBoolean raw) K side ∧ winner (preprocessBoolean raw) K side = true :=
  (picker_completed (preprocessBoolean_closed raw) K positive side P maxima reached rows retained compatible exhausted).2
-- Actual priority exhaustion gives the same successful verdict; critical_pq.hh:19–89.
theorem priority_picker_completed [DecidableEq (Fin a → Bool)]
    (closed : BooleanClosed A) (K : Nat) (positive : 1 ≤ K)
    (side : Bool) (P : SweepPlan A side) (maxima : List (State n K))
    (reached : PickerReach A K side P.generators (downset maxima))
    (queue : List (Int × ((Fin a → Bool) × List (Fin a → Bool))))
    (retained : ∀ O ∈ P.orbits, ∃ row ∈ queue.map Prod.snd, row.1 = O.representative)
    (compatible : ∀ row ∈ queue.map Prod.snd, ∀ v ∈ row.2, ∃ o, v = merge A side row.1 o)
    (exhausted : (criticalPQCached (fun f v => storedSucc A K f v) maxima queue).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  have hnone := (criticalPQ_none _ _ _).mp ((criticalPQCached_none _ _ _).mp exhausted)
  have hc := (criticalCached_none _ _ _).mpr ((criticalPicker_none _ _ _).mpr hnone)
  exact (picker_completed closed K positive side P maxima reached (queue.map Prod.snd) retained compatible hc).2
-- A sound shrinking selected update cannot discard a winner; equivariant_k_bounded_safety_aut.hh:952–956.
theorem picker_initial_out (K : Nat) (side : Bool) (G : List (Verified A))
    (X : Region n K) (reached : PickerReach A K side G X) (i : Fin a → Bool)
    (out : representativeUpdate K side G i X (initial A K) = false) : winner A K side = false := by
  have contains := selected_preserves K side G i X (picker_reach_invariants reached).1
  cases hw : winner A K side with
  | false => rfl
  | true => have := contains _ hw; rw [out] at this; contradiction

end Symmetry

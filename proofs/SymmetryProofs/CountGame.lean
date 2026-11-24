import SymmetryProofs.Enumeration
import SymmetryProofs.Quotient
import SymmetryProofs.CountTemporal

-- Unqualified C++ line references use da7e5621.

namespace Symmetry.CountGame

open Symmetry.Quotient

noncomputable def counts (x : Fin n → α) (a : α) : Nat := by
  classical exact (List.ofFn x).count a

theorem counts_list [BEq α] [LawfulBEq α] (x : Fin n → α) (a : α) :
    counts x a = (List.ofFn x).count a := by
  simp only [counts, List.count_eq_countP]
  congr 1
  funext b
  apply Bool.eq_iff_iff.mpr
  simp only [beq_iff_eq]

-- Client permutations preserve every histogram, including state/input cells; symmetric_count.cc:506–507,596–600.
theorem permuted_list (p : Perm (Fin n)) (x : Fin n → α) :
    (List.ofFn (act p x)).Perm (List.ofFn x) := by
  have hi : Function.Injective p.invFun := fun _ _ h => by
    have := congrArg p.toFun h; simpa using this
  have hp : ((List.finRange n).map p.invFun).Perm (List.finRange n) := by
    apply (List.perm_ext_iff_of_nodup ((List.nodup_finRange n).map p.invFun (fun _ _ h he => h (hi he))) (List.nodup_finRange n)).mpr
    intro i; simp only [List.mem_finRange, iff_true]
    exact List.mem_map.mpr ⟨p i, List.mem_finRange _, p.left_inv i⟩
  change (List.ofFn fun i => x (p.invFun i)).Perm _
  simpa [List.map_map, List.finRange, Function.comp_def] using hp.map x

theorem counts_act (p : Perm (Fin n)) (x : Fin n → α) : counts (act p x) = counts x := by
  classical
  funext a; exact (permuted_list p x).count_eq a

-- Equal histograms supply an actual permutation of slots; symmetric_count.cc:506–507.
theorem counts_orbit (x y : Fin n → α) : counts x = counts y ↔ ∃ p, act p x = y := by
  classical
  constructor
  · intro h
    have hp : (List.ofFn y).Perm (List.ofFn x) :=
      List.perm_iff_count.mpr (fun a => (congrFun h a).symm)
    let xs := List.ofFn x ++ List.ofFn y
    let code := fun a => xs.idxOf a
    have hc : (List.ofFn (fun i => code (y i))).Perm (List.ofFn (fun i => code (x i))) := by
      simpa [List.map_ofFn, Function.comp_def] using hp.map code
    obtain ⟨p, ht⟩ := Builders.matchSlots_complete _ _ hc
    have hm := Builders.matchSlots_types _ _ ht
    refine ⟨p.symm, funext fun i => ?_⟩
    have hx : x (p i) ∈ xs := List.mem_append_left _ (List.mem_ofFn.mpr ⟨p i, rfl⟩)
    have hy : y i ∈ xs := List.mem_append_right _ (List.mem_ofFn.mpr ⟨i, rfl⟩)
    have he := hm i
    have hget : xs[xs.idxOf (x (p i))]? = xs[xs.idxOf (y i)]? := congrArg (fun j => xs[j]?) he
    have hxi := List.idxOf_lt_length_of_mem hx
    have hyi := List.idxOf_lt_length_of_mem hy
    change x (p i) = y i
    simpa only [List.getElem?_eq_getElem hxi, List.getElem?_eq_getElem hyi,
      List.getElem_idxOf, Option.some.injEq] using hget
  · rintro ⟨p, rfl⟩; exact (counts_act p x).symm

abbrev Histogram (n : Nat) (α : Type) := {h : α → Nat // ∃ x : Fin n → α, counts x = h}
noncomputable def histogram (x : Fin n → α) : Histogram n α := ⟨counts x, x, rfl⟩
noncomputable def representative (h : Histogram n α) : Fin n → α := Classical.choose h.2
noncomputable def canonical (x : Fin n → α) : Fin n → α := representative (histogram x)

theorem canonical_counts (x : Fin n → α) : counts (canonical x) = counts x :=
  Classical.choose_spec (histogram x).2

theorem canonical_eq (x y : Fin n → α) (h : counts x = counts y) : canonical x = canonical y := by
  exact congrArg representative (Subtype.ext h)

noncomputable def transport (x : Fin n → α) : Perm (Fin n) :=
  Classical.choose ((counts_orbit x (canonical x)).mp (canonical_counts x).symm)

theorem transport_spec (x : Fin n → α) : act (transport x) x = canonical x :=
  Classical.choose_spec ((counts_orbit x (canonical x)).mp (canonical_counts x).symm)

-- Local states include all monitor bits; holds includes post-step since/ready bits as well as mark/sink/letter predicates; symmetric_count.cc:408–450,627–658.
structure Machine (Q L GI GO LI LO : Type) where
  localStep : L → GI → GO → LI → LO → L
  holds : L → GI → GO → LI → LO → Nat → Bool
  events : ((Nat → Bool) → Nat) → Nat → Bool
  globalStep : Q → GI → GO → (Nat → Bool) → Q × Nat
  reset : (Nat → Bool) → L → L

-- Incoming colors are explicit even on system vertices, before successor reuse; symmetric_count.cc:524,564–569.
inductive Vertex (n : Nat) (Q L GI LI : Type) where
  | env : Q → Nat → (Fin n → L) → Vertex n Q L GI LI
  | sys : Q → Nat → (Fin n → L) → GI → (Fin n → LI) → Vertex n Q L GI LI

variable {Q L GI GO LI LO : Type} {n : Nat}

-- The joint predicate histogram makes every event symmetric; symmetric_count.cc:637–660.
noncomputable def transition (M : Machine Q L GI GO LI LO) (q : Q) (x : Fin n → L)
    (a : GI) (b : GO) (u : Fin n → LI) (v : Fin n → LO) : Q × Nat × (Fin n → L) := by
  let cells := fun i => ((x i, u i), v i)
  let predicate := fun (cell : (L × LI) × LO) j => M.holds cell.1.1 a b cell.1.2 cell.2 j
  let ev := M.events (counts (fun i => predicate (cells i)))
  let next := M.globalStep q a b ev
  exact (next.1, next.2, fun i => M.reset ev (M.localStep (x i) a b (u i) (v i)))

noncomputable def game (M : Machine Q L GI GO LI LO) : Game (Vertex n Q L GI LI) where
  owner | .env .. => false | .sys .. => true
  prio | .env _ c _ => c | .sys .. => 0
  edge
    | .env q c x, .sys q' c' x' _ _ => q = q' ∧ c = c' ∧ x = x'
    | .sys q _ x a u, .env q' c' x' => ∃ b v, transition M q x a b u v = (q', c', x')
    | _, _ => False

def permute (p : Perm (Fin n)) : Vertex n Q L GI LI → Vertex n Q L GI LI
  | .env q c x => .env q c (act p x)
  | .sys q c x a u => .sys q c (act p x) a (act p u)

-- Canonicalize system cells together, so chosen inputs move with their clients; symmetric_count.cc:582–600.
noncomputable def transporter : Vertex n Q L GI LI → Perm (Fin n)
  | .env _ _ x => transport x
  | .sys _ _ x _ u => transport (fun i => (x i, u i))

noncomputable def key : Vertex n Q L GI LI → (Q × Nat × (L → Nat)) ⊕ (Q × Nat × GI × ((L × LI) → Nat))
  | .env q c x => .inl (q, c, counts x)
  | .sys q c x a u => .inr (q, c, a, counts (fun i => (x i, u i)))

private theorem pair_act (p : Perm (Fin n)) (x : Fin n → L) (u : Fin n → LI) :
    (fun i => (act p x i, act p u i)) = act p (fun i => (x i, u i)) := rfl

theorem transition_act (M : Machine Q L GI GO LI LO) (p : Perm (Fin n))
    (q : Q) (x : Fin n → L) (a : GI) (b : GO) (u : Fin n → LI) (v : Fin n → LO) :
    transition M q (act p x) a b (act p u) (act p v) =
      let z := transition M q x a b u v; (z.1, z.2.1, act p z.2.2) := by
  unfold transition
  have hc : counts (fun i => M.holds (act p x i) a b (act p u i) (act p v i)) =
      counts (fun i => M.holds (x i) a b (u i) (v i)) := counts_act p (fun i => M.holds (x i) a b (u i) (v i))
  simp only [hc]; rfl

theorem edge_act (M : Machine Q L GI GO LI LO) (p : Perm (Fin n))
    (s t : Vertex n Q L GI LI) (h : (game M).edge s t) :
    (game M).edge (permute p s) (permute p t) := by
  cases s with
  | env q c x =>
    cases t with
    | env => exact h
    | sys q' c' x' a u =>
      obtain ⟨rfl, rfl, rfl⟩ := h; exact ⟨rfl, rfl, rfl⟩
  | sys q c x a u =>
    cases t with
    | sys => exact h
    | env q' c' x' =>
      obtain ⟨b, v, hv⟩ := h
      exact ⟨b, act p v, by rw [transition_act, hv]⟩

-- Counts distinguish exactly the S_n orbits of BOTH sorts of vertex; symmetric_count.cc:506–507,596–600.
theorem key_orbit (s t : Vertex n Q L GI LI) : key s = key t ↔ ∃ p, permute p s = t := by
  constructor
  · intro h
    cases s with
    | env q c x =>
      cases t with
      | sys => cases h
      | env q' c' y =>
        obtain ⟨rfl, rfl, hc⟩ := (Prod.mk.inj (Sum.inl.inj h)).imp_right Prod.mk.inj
        obtain ⟨p, hp⟩ := (counts_orbit x y).mp hc
        exact ⟨p, congrArg (Vertex.env q c) hp⟩
    | sys q c x a u =>
      cases t with
      | env => cases h
      | sys q' c' y a' v =>
        have he : q = q' ∧ c = c' ∧ a = a' ∧
            counts (fun i => (x i, u i)) = counts (fun i => (y i, v i)) := by
          simpa only [key, Sum.inr.injEq, Prod.mk.injEq] using h
        obtain ⟨rfl, rfl, rfl, hc⟩ := he
        obtain ⟨p, hp⟩ := (counts_orbit (fun i => (x i, u i)) (fun i => (y i, v i))).mp hc
        have hx : act p x = y := congrArg (fun f i => (f i).1) hp
        have hu : act p u = v := congrArg (fun f i => (f i).2) hp
        exact ⟨p, by change Vertex.sys q c (act p x) a (act p u) = _; rw [hx, hu]⟩
  · rintro ⟨p, rfl⟩
    cases s with
    | env => simp only [permute, key, counts_act]
    | sys => simp only [permute, key, pair_act, counts_act]

private theorem canon_act (p : Perm (Fin n)) (s : Vertex n Q L GI LI) :
    permute (transporter (permute p s)) (permute p s) = permute (transporter s) s := by
  cases s with
  | env q c x =>
    change Vertex.env q c (act (transport (act p x)) (act p x)) =
      Vertex.env q c (act (transport x) x)
    rw [transport_spec, transport_spec, canonical_eq _ _ (counts_act p x)]
  | sys q c x a u =>
    have h1 := transport_spec (fun i => (act p x i, act p u i))
    have h2 := transport_spec (fun i => (x i, u i))
    have hc : counts (fun i => (act p x i, act p u i)) = counts (fun i => (x i, u i)) :=
      counts_act p (fun i => (x i, u i))
    rw [canonical_eq _ _ hc] at h1
    have he := h1.trans h2.symm
    have hx := congrArg (fun f i => (f i).1) he
    have hu := congrArg (fun f i => (f i).2) he
    change Vertex.sys q c _ a _ = Vertex.sys q c _ a _
    congr 1

-- All Action hypotheses are proved for the expanded Mealy game; symmetric_count.cc:506–690.
noncomputable def action (M : Machine Q L GI GO LI LO) : Action (game M (n := n)) (Perm (Fin n)) where
  act := permute
  inv := Perm.symm
  act_inv p s := by cases s <;> simp only [permute, act_inverse]
  owner_act p s := by cases s <;> rfl
  prio_act p s := by cases s <;> rfl
  edge_act := edge_act M
  transporter := transporter
  canon_orbit := canon_act

-- Each nonempty state/input cell is split by output, with no further quotient; symmetric_count.cc:596–610.
theorem stabilizer_orbit (x : Fin n → L) (u v : Fin n → LI) :
    counts (fun i => (x i, u i)) = counts (fun i => (x i, v i)) ↔
      ∃ p, act p x = x ∧ act p u = v := by
  constructor
  · intro h
    obtain ⟨p, hp⟩ := (counts_orbit _ _).mp h
    exact ⟨p, congrArg (fun f i => (f i).1) hp, congrArg (fun f i => (f i).2) hp⟩
  · rintro ⟨p, hx, hu⟩
    have h := counts_act p (fun i => (x i, u i))
    change counts (fun i => (act p x i, act p u i)) = _ at h
    rw [hx, hu] at h; exact h.symm

noncomputable def expand (domain : List α) (h : α → Nat) : List α :=
  domain.flatMap (fun a => List.replicate (h a) a)

private theorem expand_count [DecidableEq α] (domain : List α) (h : α → Nat)
    (unique : domain.Nodup) (a : α) :
    (expand domain h).count a = if a ∈ domain then h a else 0 := by
  induction domain with
  | nil => simp [expand]
  | cons b bs ih =>
    obtain ⟨hb, hbs⟩ := List.nodup_cons.mp unique
    change (List.replicate (h b) b ++ expand bs h).count a = _
    rw [List.count_append, ih hbs]
    by_cases he : b = a
    · subst b; simp [hb]
    · simp [List.count_replicate, he, Ne.symm he]

private theorem expand_perm [DecidableEq α] (domain word : List α) (unique : domain.Nodup)
    (covered : ∀ a ∈ word, a ∈ domain) :
    (expand domain (fun a => word.count a)).Perm word := by
  apply List.perm_iff_count.mpr; intro a
  rw [expand_count domain _ unique]
  by_cases ha : a ∈ domain
  · simp [ha]
  · simp [ha, List.count_eq_zero.mpr (fun h => ha (covered a h))]

noncomputable def pushCounts (domain : List α) (h : α → Nat) (f : α → β) (b : β) : Nat := by
  classical exact (domain.map (fun a => if f a = b then h a else 0)).sum

-- This is the builder's weighted accumulation, not a choice of an indexed successor; symmetric_count.cc:637–663.
theorem push_counts (domain : List α) (x : Fin n → α) (unique : domain.Nodup)
    (covered : ∀ i, x i ∈ domain) (f : α → β) :
    pushCounts domain (counts x) f = counts (fun i => f (x i)) := by
  classical
  have hp := (expand_perm domain (List.ofFn x) unique (by
    intro a ha; obtain ⟨i, rfl⟩ := List.mem_ofFn.mp ha; exact covered i)).map f
  funext b
  have hc := hp.count_eq b
  simpa [expand, List.map_flatMap, List.map_replicate, List.count_flatMap,
    Function.comp_def, List.count_replicate, pushCounts, counts, List.map_ofFn] using hc

-- Only positive cells contribute to any/all and breakpoint tests; symmetric_count.cc:620–658.
theorem count_positive (x : Fin n → α) (a : α) : 0 < counts x a ↔ ∃ i, x i = a := by
  classical
  simp [counts, List.count_pos_iff, List.mem_ofFn]

theorem occupied_events (x : Fin n → α) (p : α → Prop) :
    ((∃ a, 0 < counts x a ∧ p a) ↔ ∃ i, p (x i)) ∧
    ((∀ a, 0 < counts x a → p a) ↔ ∀ i, p (x i)) := by
  simp only [count_positive]
  constructor
  · exact ⟨by rintro ⟨a, ⟨i, rfl⟩, hp⟩; exact ⟨i, hp⟩,
      by rintro ⟨i, hp⟩; exact ⟨x i, ⟨i, rfl⟩, hp⟩⟩
  · exact ⟨fun h i => h (x i) ⟨i, rfl⟩, by rintro h a ⟨i, rfl⟩; exact h i⟩

-- Count transition: aggregate the predicate mask, advance globally, sum reset destinations; symmetric_count.cc:612–665.
noncomputable def countTransition (M : Machine Q L GI GO LI LO) (domain : List ((L × LI) × LO))
    (h : ((L × LI) × LO) → Nat) (q : Q) (a : GI) (b : GO) : Q × Nat × (L → Nat) :=
  let ev := M.events (pushCounts domain h (fun z => M.holds z.1.1 a b z.1.2 z.2))
  let next := M.globalStep q a b ev
  (next.1, next.2, pushCounts domain h (fun z => M.reset ev (M.localStep z.1.1 a b z.1.2 z.2)))

-- The count successor is exactly the histogram of the indexed successor; symmetric_count.cc:661–667.
theorem count_successor (M : Machine Q L GI GO LI LO) (domain : List ((L × LI) × LO))
    (unique : domain.Nodup) (q : Q) (x : Fin n → L) (a : GI) (b : GO)
    (u : Fin n → LI) (v : Fin n → LO) (covered : ∀ i, ((x i, u i), v i) ∈ domain) :
    countTransition M domain (counts (fun i => ((x i, u i), v i))) q a b =
      let z := transition M q x a b u v; (z.1, z.2.1, counts z.2.2) := by
  unfold countTransition transition
  rw [push_counts domain _ unique covered]
  dsimp only
  rw [push_counts domain _ unique covered]

-- In particular the count successor names the canonical concrete successor; symmetric_count.cc:661–667.
theorem count_canonical (M : Machine Q L GI GO LI LO) (domain : List ((L × LI) × LO))
    (unique : domain.Nodup) (q : Q) (x : Fin n → L) (a : GI) (b : GO)
    (u : Fin n → LI) (v : Fin n → LO) (covered : ∀ i, ((x i, u i), v i) ∈ domain) :
    key ((action M).canon (.env (transition M q x a b u v).1
      (transition M q x a b u v).2.1 (transition M q x a b u v).2.2)) =
      Sum.inl (countTransition M domain (counts (fun i => ((x i, u i), v i))) q a b) := by
  rw [count_successor M domain unique q x a b u v covered]
  change key (permute (transport _) (.env _ _ _)) = _
  simp only [permute, key, counts_act]


private theorem align_word (x : Fin n → L) (word : List (L × LI))
    (h : (word.map Prod.fst).Perm (List.ofFn x)) :
    ∃ u : Fin n → LI, (List.ofFn (fun i => (x i, u i))).Perm word := by
  classical
  have hl : word.length = n := by simpa using h.length_eq
  let y : Fin n → L × LI := fun i => word[i.val]'(by omega)
  have hy : List.ofFn y = word := by
    apply List.ext_getElem
    · simp [hl]
    · intro i hi hj; simp [y]
  have hc : counts (fun i => (y i).1) = counts x := by
    funext a
    have hp : (List.ofFn (fun i => (y i).1)).Perm (List.ofFn x) := by
      have he : (List.ofFn y).map Prod.fst = List.ofFn (fun i => (y i).1) := List.map_ofFn
      rw [← he, hy]; exact h
    exact hp.count_eq a
  obtain ⟨p, hp⟩ := (counts_orbit _ _).mp hc
  refine ⟨act p (fun i => (y i).2), ?_⟩
  have he : (fun i => (x i, act p (fun i => (y i).2) i)) = act p y := by
    funext i
    exact Prod.ext (congrFun hp i).symm rfl
  rw [he, ← hy]; exact permuted_list p y

-- Any nonnegative cell counts with the old histogram as marginal have an indexed realization; symmetric_count.cc:582–610.
theorem split_realization (domain : List (L × LI)) (unique : domain.Nodup)
    (x : Fin n → L) (h : (L × LI) → Nat)
    (outside : ∀ z, z ∉ domain → h z = 0) (marginal : pushCounts domain h Prod.fst = counts x) :
    ∃ u : Fin n → LI, counts (fun i => (x i, u i)) = h := by
  classical
  let word := expand domain h
  have hc (z : L × LI) : word.count z = h z := by
    rw [expand_count domain h unique]
    by_cases hz : z ∈ domain
    · simp [hz]
    · simp [hz, outside z hz]
  have hm : (word.map Prod.fst).Perm (List.ofFn x) := by
    apply List.perm_iff_count.mpr; intro a
    have he : (word.map Prod.fst).count a = pushCounts domain h Prod.fst a := by
      simp [word, expand, List.map_flatMap, List.map_replicate, List.count_flatMap,
        Function.comp_def, List.count_replicate, pushCounts]
    rw [he, marginal]; rfl
  obtain ⟨u, hu⟩ := align_word x word hm
  exact ⟨u, funext fun z => (counts_list _ z).trans ((hu.count_eq z).trans (hc z))⟩

def cells (states : List L) (k : Nat) : List (L × Fin k) :=
  states.flatMap (fun q => (List.finRange k).map (q, ·))

private theorem cells_unique (states : List L) (unique : states.Nodup) (k : Nat) :
    (cells states k).Nodup := by
  apply List.pairwise_flatMap.mpr
  constructor
  · intro q _
    exact (List.nodup_finRange k).map (q, ·) (fun _ _ h he => h (Prod.mk.inj he).2)
  · apply unique.imp
    intro q r h x hx y hy he
    obtain ⟨i, _, rfl⟩ := List.mem_map.mp hx
    obtain ⟨j, _, rfl⟩ := List.mem_map.mp hy
    exact h (Prod.mk.inj he).1

private theorem push_rows [DecidableEq L] (states : List L) (unique : states.Nodup) (h : (L × Fin k) → Nat) (q : L) :
    pushCounts (cells states k) h Prod.fst q =
      if q ∈ states then (List.ofFn (fun j => h (q, j))).sum else 0 := by
  classical
  induction states with
  | nil => simp [pushCounts, cells]
  | cons s ss ih =>
    obtain ⟨hs, hu⟩ := List.nodup_cons.mp unique
    rw [show cells (s :: ss) k = (List.finRange k).map (s, ·) ++ cells ss k from rfl]
    unfold pushCounts
    rw [List.map_append, List.sum_append]
    by_cases he : s = q
    · subst s
      have he := ih hu
      simp only [pushCounts] at he
      simp only [Function.comp_def, ite_true, List.finRange, List.map_ofFn]
      rw [he]
      simp [hs]
    · simpa [pushCounts, List.map_map, he, Ne.symm he, Function.comp_def,
        List.map_const', List.sum_replicate_nat] using ih hu

-- The cartesian product of compositions enumerates exactly all input/output split histograms; symmetric_count.cc:249–270,582–610.
theorem compositions_exact (states : List L) (unique : states.Nodup) (x : Fin n → L)
    (covered : ∀ i, x i ∈ states) (h : (L × Fin k) → Nat) :
    ((∀ q ∈ states, List.ofFn (fun j => h (q, j)) ∈ Builders.typeCounts k (counts x q)) ∧
      (∀ z, z ∉ cells states k → h z = 0)) ↔
      ∃ u : Fin n → Fin k, counts (fun i => (x i, u i)) = h := by
  classical
  constructor
  · rintro ⟨rows, outside⟩
    apply split_realization (cells states k) (cells_unique states unique k) x h outside
    funext q
    rw [push_rows states unique]
    by_cases hq : q ∈ states
    · rw [ite_eq_left hq]; exact (Builders.mem_typeCounts _ _ _).mp (rows q hq) |>.2
    · rw [ite_eq_right hq]
      exact (List.count_eq_zero.mpr (by
        intro hm; obtain ⟨i, hi⟩ := List.mem_ofFn.mp hm; exact hq (hi ▸ covered i))).symm
  · rintro ⟨u, rfl⟩
    have hc : ∀ i, (x i, u i) ∈ cells states k := by
      intro i; exact List.mem_flatMap.mpr ⟨x i, covered i, List.mem_map.mpr ⟨u i, List.mem_finRange _, rfl⟩⟩
    refine ⟨fun q hq => (Builders.mem_typeCounts _ _ _).mpr ⟨by simp, ?_⟩, ?_⟩
    · have he := congrFun (push_counts (cells states k) (fun i => (x i, u i))
        (cells_unique states unique k) hc Prod.fst) q
      rw [push_rows states unique, ite_eq_left hq] at he
      exact he
    · intro z hz
      rw [counts_list]
      apply List.count_eq_zero.mpr
      intro hm; obtain ⟨i, rfl⟩ := List.mem_ofFn.mp hm; exact hz (hc i)


open CountTemporal

-- Max-odd acceptance refers only to priorities occurring infinitely often; symmetric_count.cc:453,477–479.
def maxOdd (s : Nat → Nat) : Prop :=
  ∃ c, c % 2 = 1 ∧ GF (fun t => s t = c) 0 ∧ ∀ d, GF (fun t => s t = d) 0 → d ≤ c

-- A zero-priority intervening move cannot change the single recurrent nonzero color; symmetric_count.cc:485–498,592–593.
theorem maxOdd_absorbing (s : Nat → Nat) (c : Nat)
    (tail : ∃ k, ∀ t, k ≤ t → s t = 0 ∨ s t = c) (recurs : GF (fun t => s t = c) 0) :
    maxOdd s ↔ c % 2 = 1 := by
  obtain ⟨k, hk⟩ := tail
  have only (d : Nat) (hd : GF (fun t => s t = d) 0) : d = 0 ∨ d = c := by
    obtain ⟨t, ht, he⟩ := hd k (Nat.zero_le _)
    simpa only [he] using hk t ht
  constructor
  · rintro ⟨d, ho, hd, _⟩
    rcases only d hd with rfl | rfl
    · contradiction
    · exact ho
  · intro ho
    exact ⟨c, ho, recurs, fun d hd => (only d hd).elim (fun h => h ▸ Nat.zero_le _) (fun h => h ▸ Nat.le_refl _)⟩

-- The global automaton's all-self-loop/single-color check; symmetric_count.cc:485–498.
def Absorbing (M : Machine Q L GI GO LI LO) (q : Q) (c : Nat) : Prop :=
  ∀ a b ev, M.globalStep q a b ev = (q, c)

private theorem absorbing_step (M : Machine Q L GI GO LI LO) (q : Q) (c : Nat)
    (loop : Absorbing M q c) (x : Fin n → L) (a : GI) (b : GO) (u : Fin n → LI) (v : Fin n → LO) :
    (transition M q x a b u v).1 = q ∧ (transition M q x a b u v).2.1 = c := by
  simp only [transition]
  rw [loop a b _]
  exact ⟨rfl, rfl⟩

private noncomputable def relaxedGame (M : Machine Q L GI GO LI LO) : Game (Vertex n Q L GI LI) where
  owner := (game M).owner
  prio := (game M).prio
  edge s t := match s, t with
    | .env q _ x, .sys q' _ x' _ _ => q = q' ∧ x = x'
    | .sys q _ x a u, .env q' c' x' => ∃ b v, transition M q x a b u v = (q', c', x')
    | _, _ => False

private def inAbsorbing (q : Q) (c k : Nat) : Vertex n Q L GI LI → Prop
  | .env q' d _ => q' = q ∧ (k = 0 ∨ d = c)
  | .sys q' _ _ _ _ => q' = q

private theorem absorbing_invariant (M : Machine Q L GI GO LI LO) (q : Q) (c d : Nat)
    (loop : Absorbing M q c) (x : Fin n → L) (π : Nat → Vertex n Q L GI LI)
    (play : IsPlay (relaxedGame M) π) (start : π 0 = .env q d x) : ∀ k, inAbsorbing q c k (π k) := by
  intro k; induction k with
  | zero => rw [start]; exact ⟨rfl, Or.inl rfl⟩
  | succ k ih =>
    have he := play k
    cases hs : π k with
    | env q' d' x' =>
      cases ht : π (k + 1) with
      | env => simp [hs, ht, relaxedGame] at he
      | sys r e y a u =>
        rw [hs, ht] at he; obtain ⟨rfl, rfl⟩ := he
        exact (show q' = q from (hs ▸ ih).1)
    | sys q' d' x' a u =>
      have hq : q' = q := by simpa only [hs, inAbsorbing] using ih
      subst q'
      cases ht : π (k + 1) with
      | sys => simp [hs, ht, relaxedGame] at he
      | env r e y =>
        rw [hs, ht] at he
        obtain ⟨b, v, hv⟩ := he
        obtain ⟨hr, hc⟩ := absorbing_step M q c loop x' a b u v
        rw [hv] at hr hc
        exact ⟨hr, Or.inr hc⟩

-- Every expanded play in the collapsed region has the sink color's parity, independently of counts; symmetric_count.cc:529–533,559–562.
private theorem absorbing_play (M : Machine Q L GI GO LI LO) (q : Q) (c d : Nat)
    (loop : Absorbing M q c) (x : Fin n → L) (π : Nat → Vertex n Q L GI LI)
    (play : IsPlay (relaxedGame M) π) (start : π 0 = .env q d x) :
    maxOdd (fun k => (game M).prio (π k)) ↔ c % 2 = 1 := by
  have inv := absorbing_invariant M q c d loop x π play start
  apply maxOdd_absorbing _ c
  · refine ⟨1, fun k hk => ?_⟩
    have hi := inv k
    cases hs : π k with
    | sys => exact Or.inl rfl
    | env r e y =>
      have hc : e = c := (hs ▸ hi).2.resolve_left (by omega)
      exact Or.inr hc
  · intro u _
    let k := max u 1
    have hk : 1 ≤ k := Nat.le_max_right ..
    have hu : u ≤ k := Nat.le_max_left ..
    have hi := inv k
    cases hs : π k with
    | env r e y =>
      refine ⟨k, hu, ?_⟩
      simpa only [hs, game] using (hs ▸ hi).2.resolve_left (by omega)
    | sys r e y a i =>
      have he := play k
      cases ht : π (k + 1) with
      | sys => simp [hs, ht, relaxedGame] at he
      | env r' e' y' =>
        have hi' := inv (k + 1)
        refine ⟨k + 1, by omega, ?_⟩
        simpa only [ht, game] using (ht ▸ hi').2.resolve_left (by omega)

-- Output row compositions produce EXACTLY the concrete output edges, with both directions witnessed; symmetric_count.cc:601–687.
theorem output_edges (M : Machine Q L GI GO LI (Fin k))
    (states : List (L × LI)) (unique : states.Nodup)
    (q : Q) (x : Fin n → L) (a : GI) (b : GO) (u : Fin n → LI)
    (covered : ∀ i, (x i, u i) ∈ states) (target : Q × Nat × (L → Nat)) :
    (∃ h : ((L × LI) × Fin k) → Nat,
      (∀ z ∈ states, List.ofFn (fun j => h (z, j)) ∈ Builders.typeCounts k (counts (fun i => (x i, u i)) z)) ∧
      (∀ z, z ∉ cells states k → h z = 0) ∧
      countTransition M (cells states k) h q a b = target) ↔
    ∃ v : Fin n → Fin k,
      (let z := transition M q x a b u v; (z.1, z.2.1, counts z.2.2)) = target := by
  constructor
  · rintro ⟨h, hr, ho, ht⟩
    obtain ⟨v, hv⟩ := (compositions_exact states unique (fun i => (x i, u i)) covered h).mp ⟨hr, ho⟩
    refine ⟨v, ?_⟩
    rw [← hv, count_successor M _ (cells_unique states unique k) q x a b u v (by
      intro i; exact List.mem_flatMap.mpr ⟨(x i, u i), covered i,
        List.mem_map.mpr ⟨v i, List.mem_finRange _, rfl⟩⟩)] at ht
    exact ht
  · rintro ⟨v, ht⟩
    let h := counts (fun i => ((x i, u i), v i))
    obtain ⟨hr, ho⟩ := (compositions_exact states unique (fun i => (x i, u i)) covered h).mpr ⟨v, rfl⟩
    refine ⟨h, hr, ho, ?_⟩
    rw [count_successor M _ (cells_unique states unique k) q x a b u v (by
      intro i; exact List.mem_flatMap.mpr ⟨(x i, u i), covered i,
        List.mem_map.mpr ⟨v i, List.mem_finRange _, rfl⟩⟩)]
    exact ht


-- Shared system vertices omit the environment's incoming color; symmetric_count.cc:564–569,589–593.
def share : Vertex n Q L GI LI → Vertex n Q L GI LI
  | .env q c x => .env q c x
  | .sys q _ x a u => .sys q 0 x a u

noncomputable def sharedGame (M : Machine Q L GI GO LI LO) : Game (Vertex n Q L GI LI) where
  owner := (game M).owner
  prio := (game M).prio
  edge s t := match s, t with
    | .env q _ x, .sys q' c' x' _ _ => q = q' ∧ c' = 0 ∧ x = x'
    | .sys q _ x a u, .env q' c' x' => ∃ b v, transition M q x a b u v = (q', c', x')
    | _, _ => False

def restore (s t : Vertex n Q L GI LI) : Vertex n Q L GI LI :=
  match s, t with
  | .env _ c _, .sys q _ x a u => .sys q c x a u
  | _, _ => t

private theorem share_edge (M : Machine Q L GI GO LI LO) (s t : Vertex n Q L GI LI)
    (h : (game M).edge s t) : (sharedGame M).edge (share s) (share t) := by
  cases s <;> cases t <;> first
  | exact h
  | obtain ⟨rfl, rfl, rfl⟩ := h; exact ⟨rfl, rfl, rfl⟩

private theorem restore_edge (M : Machine Q L GI GO LI LO) (s t : Vertex n Q L GI LI)
    (h : (sharedGame M).edge (share s) t) :
    (game M).edge s (restore s t) ∧ share (restore s t) = t := by
  cases s with
  | env q c x =>
    cases t with
    | env => exact False.elim h
    | sys q' c' x' a u =>
      obtain ⟨rfl, rfl, rfl⟩ := h; exact ⟨⟨rfl, rfl, rfl⟩, rfl⟩
  | sys q c x a u =>
    cases t with
    | env => exact ⟨h, rfl⟩
    | sys => exact False.elim h

noncomputable def sharedLift (strategy : Vertex n Q L GI LI → Vertex n Q L GI LI) (s : Vertex n Q L GI LI) :
    Vertex n Q L GI LI := restore s (strategy (share s))

-- Cache reuse preserves all priorities, including EVERY original incoming color; symmetric_count.cc:564–569.
theorem successor_reuse (M : Machine Q L GI GO LI LO) {p : Bool}
    {strategy : Vertex n Q L GI LI → Vertex n Q L GI LI} {s : Vertex n Q L GI LI}
    {W : (Nat → Nat) → Prop} (h : Wins (sharedGame M) W p strategy (share s)) :
    Wins (game M) W p (sharedLift strategy) s := by
  have owner (v : Vertex n Q L GI LI) : (sharedGame M).owner (share v) = (game M).owner v := by cases v <;> rfl
  have prio (v : Vertex n Q L GI LI) : (sharedGame M).prio (share v) = (game M).prio v := by cases v <;> rfl
  refine ⟨fun v hv => (restore_edge M v _ (h.1 _ (by rw [owner]; exact hv))).1, fun π hp hs hc => ?_⟩
  have play : IsPlay (sharedGame M) (fun k => share (π k)) := fun k => share_edge M _ _ (hp k)
  have consistent : Consistent (sharedGame M) p strategy (fun k => share (π k)) := by
    intro k hk
    have hko : (game M).owner (π k) = p := by simpa only [owner] using hk
    change share (π (k + 1)) = strategy (share (π k))
    rw [hc k hko]
    exact (restore_edge M (π k) _ (h.1 _ hk)).2
  have win := h.2 _ play (by rw [hs]) consistent
  simpa only [prio] using win

private theorem shared_edge_act (M : Machine Q L GI GO LI LO) (p : Perm (Fin n))
    (s t : Vertex n Q L GI LI) (h : (sharedGame M).edge s t) :
    (sharedGame M).edge (permute p s) (permute p t) := by
  cases s with
  | env q c x =>
    cases t with
    | env => exact h
    | sys q' c' x' a u =>
      obtain ⟨rfl, rfl, rfl⟩ := h; exact ⟨rfl, rfl, rfl⟩
  | sys q c x a u =>
    cases t with
    | sys => exact h
    | env q' c' x' =>
      obtain ⟨b, v, hv⟩ := h
      exact ⟨b, act p v, by rw [transition_act, hv]⟩

private theorem shared_relaxed (M : Machine Q L GI GO LI LO) (s t : Vertex n Q L GI LI)
    (h : (sharedGame M).edge s t) : (relaxedGame M).edge s t := by
  cases s <;> cases t <;> first | exact h | exact ⟨h.1, h.2.2⟩

private theorem gf_shift (p : Trace) (k : Nat) : GF (fun t => p (k + t)) 0 ↔ GF p 0 := by
  constructor
  · intro h u _; obtain ⟨v, hv, hp⟩ := h u (Nat.zero_le _); exact ⟨k + v, by omega, hp⟩
  · intro h u _; obtain ⟨v, hv, hp⟩ := h (k + u) (Nat.zero_le _)
    exact ⟨v - k, by omega, by simpa [Nat.add_sub_cancel' (by omega : k ≤ v)] using hp⟩

private theorem maxOdd_shift (s : Nat → Nat) (k : Nat) : maxOdd (fun t => s (k + t)) ↔ maxOdd s := by
  have he (c : Nat) : GF (fun t => s (k + t) = c) 0 ↔ GF (fun t => s t = c) 0 := gf_shift (fun t => s t = c) k
  simp only [maxOdd, he]

def globalState : Vertex n Q L GI LI → Q
  | .env q _ _ => q
  | .sys q _ _ _ _ => q

-- An absorbing state discards local counts and incoming colors on entry; symmetric_count.cc:529–533.
def collapse (absorbing : Q → Option Nat) (initial : L) (s : Vertex n Q L GI LI) : Vertex n Q L GI LI :=
  match absorbing (globalState s) with
  | none => s
  | some c => .env (globalState s) c (fun _ => initial)

private theorem collapse_state (absorbing : Q → Option Nat) (initial : L) (s : Vertex n Q L GI LI) :
    globalState (collapse absorbing initial s) = globalState s := by
  unfold collapse; split <;> rfl

private theorem collapse_idem (absorbing : Q → Option Nat) (initial : L) (s : Vertex n Q L GI LI) :
    collapse absorbing initial (collapse absorbing initial s) = collapse absorbing initial s := by
  change (match absorbing (globalState (collapse absorbing initial s)) with
    | none => collapse absorbing initial s
    | some c => .env (globalState (collapse absorbing initial s)) c (fun _ => initial)) = _
  rw [collapse_state]
  cases ha : absorbing (globalState s) <;> simp only [collapse, ha]

-- The terminal compression used by the count builder, after successor sharing; symmetric_count.cc:529–569.
noncomputable def collapsedGame (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L) :
    Game (Vertex n Q L GI LI) where
  owner s := (sharedGame M).owner (collapse absorbing initial s)
  prio s := (sharedGame M).prio (collapse absorbing initial s)
  edge s t := match absorbing (globalState s) with
    | some _ => t = collapse absorbing initial s
    | none => ∃ w, (sharedGame M).edge s w ∧ collapse absorbing initial w = t

private theorem closed_absorbing (M : Machine Q L GI GO LI LO) (q : Q) (c : Nat)
    (loop : Absorbing M q c) (s t : Vertex n Q L GI LI) (hs : globalState s = q)
    (he : (sharedGame M).edge s t) : globalState t = q := by
  cases s with
  | env r d x =>
    cases t with
    | env => exact False.elim he
    | sys r' d' y a u => exact he.1.symm.trans hs
  | sys r d x a u =>
    cases t with
    | sys => exact False.elim he
    | env r' d' y =>
      obtain ⟨b, v, hv⟩ := he
      have hh : r = q := hs
      subst r
      have hz := (absorbing_step M q c loop x a b u v).1
      rw [hv] at hz; exact hz

private theorem collapse_edge (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (valid : ∀ q c, absorbing q = some c → Absorbing M q c)
    (s t : Vertex n Q L GI LI) (h : (sharedGame M).edge s t) :
    (collapsedGame M absorbing initial).edge (collapse absorbing initial s) (collapse absorbing initial t) := by
  change (match absorbing (globalState (collapse absorbing initial s)) with
    | some _ => collapse absorbing initial t = collapse absorbing initial (collapse absorbing initial s)
    | none => ∃ w, (sharedGame M).edge (collapse absorbing initial s) w ∧ collapse absorbing initial w = collapse absorbing initial t)
  rw [collapse_state, collapse_idem]
  cases ha : absorbing (globalState s) with
  | none => exact ⟨t, by simpa [collapse, ha] using h, rfl⟩
  | some c =>
    have hg := closed_absorbing M _ c (valid _ c ha) s t rfl h
    simp only [collapse, hg, ha]

private noncomputable def sharedDefault (M : Machine Q L GI GO LI LO) (a : GI) (b : GO) (u : LI) (v : LO) :
    Vertex n Q L GI LI → Vertex n Q L GI LI
  | .env q _ x => .sys q 0 x a (fun _ => u)
  | .sys q _ x g i => let z := transition M q x g b i (fun _ => v); .env z.1 z.2.1 z.2.2

private theorem sharedDefault_edge (M : Machine Q L GI GO LI LO) (a : GI) (b : GO) (u : LI) (v : LO)
    (s : Vertex n Q L GI LI) : (sharedGame M).edge s (sharedDefault M a b u v s) := by
  cases s with
  | env => exact ⟨rfl, rfl, rfl⟩
  | sys => exact ⟨b, fun _ => v, rfl⟩

noncomputable def collapseLift (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (a : GI) (b : GO) (u : LI) (v : LO) (strategy : Vertex n Q L GI LI → Vertex n Q L GI LI)
    (s : Vertex n Q L GI LI) : Vertex n Q L GI LI := by
  classical
  exact if h : ∃ w, (sharedGame M).edge s w ∧ collapse absorbing initial w = strategy (collapse absorbing initial s)
    then Classical.choose h else sharedDefault M a b u v s

private theorem collapseLift_edge (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (a : GI) (b : GO) (u : LI) (v : LO) (strategy : Vertex n Q L GI LI → Vertex n Q L GI LI)
    (s : Vertex n Q L GI LI) : (sharedGame M).edge s (collapseLift M absorbing initial a b u v strategy s) := by
  unfold collapseLift; split
  · rename_i h
    exact (Classical.choose_spec h).1
  · exact sharedDefault_edge M a b u v s

private theorem collapseLift_project (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (a : GI) (b : GO) (u : LI) (v : LO) (strategy : Vertex n Q L GI LI → Vertex n Q L GI LI)
    (s : Vertex n Q L GI LI)
    (h : ∃ w, (sharedGame M).edge s w ∧ collapse absorbing initial w = strategy (collapse absorbing initial s)) :
    collapse absorbing initial (collapseLift M absorbing initial a b u v strategy s) = strategy (collapse absorbing initial s) := by
  unfold collapseLift; rw [dite_eq_left h]; exact (Classical.choose_spec h).2

private theorem collapse_parity (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (valid : ∀ q c, absorbing q = some c → Absorbing M q c)
    (π : Nat → Vertex n Q L GI LI) (play : IsPlay (sharedGame M) π) :
    maxOdd (fun k => (collapsedGame M absorbing initial).prio (collapse absorbing initial (π k))) ↔
      maxOdd (fun k => (sharedGame M).prio (π k)) := by
  classical
  by_cases hit : ∃ k c, absorbing (globalState (π k)) = some c
  · obtain ⟨k, c, hc⟩ := hit
    have enter : ∃ j q d x, π j = .env q d x ∧ absorbing q = some c := by
      cases hs : π k with
      | env q d x => exact ⟨k, q, d, x, hs, by simpa only [hs, globalState] using hc⟩
      | sys q d x a u =>
        have hq : absorbing q = some c := by simpa only [hs, globalState] using hc
        have he := play k
        cases ht : π (k + 1) with
        | sys => simp [hs, ht, sharedGame] at he
        | env r e y =>
          have hg := closed_absorbing M q c (valid q c hq) _ _ (by rw [hs]; rfl) he
          exact ⟨k + 1, r, e, y, ht, by
            have hr : r = q := by simpa only [ht, globalState] using hg
            simpa only [hr] using hq⟩
    obtain ⟨j, q, d, x, hj, hq⟩ := enter
    have hp : IsPlay (relaxedGame M) (fun t => π (j + t)) := by
      intro t; exact shared_relaxed M _ _ (play (j + t))
    have inv := absorbing_invariant M q c d (valid q c hq) x (fun t => π (j + t)) hp (by simpa using hj)
    have hg (t : Nat) : globalState (π (j + t)) = q := by
      have hi := inv t
      cases hs : π (j + t) with
      | env => exact (show _ ∧ _ from hs ▸ hi).1
      | sys => simpa only [inAbsorbing, globalState, hs] using hi
    have proj : (fun t => (collapsedGame M absorbing initial).prio (collapse absorbing initial (π (j + t)))) = fun _ => c := by
      funext t
      change (sharedGame M).prio (collapse absorbing initial (collapse absorbing initial (π (j + t)))) = c
      rw [collapse_idem]
      unfold collapse
      rw [hg t, hq]
      rfl
    rw [← maxOdd_shift _ j, ← maxOdd_shift (fun k => (sharedGame M).prio (π k)) j, proj]
    exact (maxOdd_absorbing (fun _ => c) c ⟨0, fun _ _ => Or.inr rfl⟩
      (fun u _ => ⟨u, Nat.le_refl _, rfl⟩)).trans
      (absorbing_play M q c d (valid q c hq) x (fun t => π (j + t)) hp (by simpa using hj)).symm
  · have hn (k : Nat) : absorbing (globalState (π k)) = none := by
      cases h : absorbing (globalState (π k)) with
      | none => rfl
      | some c => exact False.elim (hit ⟨k, c, h⟩)
    have he : (fun k => (collapsedGame M absorbing initial).prio (collapse absorbing initial (π k))) =
        (fun k => (sharedGame M).prio (π k)) := by
      funext k; simp only [collapsedGame, collapse, hn k]
    rw [he]

-- Terminal compression lifts certified strategies through ALL possible entrance times; symmetric_count.cc:529–562.
theorem collapse_wins (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (valid : ∀ q c, absorbing q = some c → Absorbing M q c)
    (a : GI) (b : GO) (u : LI) (v : LO) {p : Bool}
    {strategy : Vertex n Q L GI LI → Vertex n Q L GI LI} {s : Vertex n Q L GI LI}
    (h : Wins (collapsedGame M absorbing initial) maxOdd p strategy (collapse absorbing initial s)) :
    Wins (sharedGame M) maxOdd p (collapseLift M absorbing initial a b u v strategy) s := by
  refine ⟨fun t _ => collapseLift_edge M absorbing initial a b u v strategy t, fun π hp hs hc => ?_⟩
  have play : IsPlay (collapsedGame M absorbing initial) (fun k => collapse absorbing initial (π k)) :=
    fun k => collapse_edge M absorbing initial valid _ _ (hp k)
  have consistent : Consistent (collapsedGame M absorbing initial) p strategy (fun k => collapse absorbing initial (π k)) := by
    intro k hk
    change collapse absorbing initial (π (k + 1)) = strategy (collapse absorbing initial (π k))
    have hl := h.1 _ hk
    cases ha : absorbing (globalState (π k)) with
    | some c =>
      have he := closed_absorbing M _ c (valid _ c ha) _ _ rfl (hp k)
      have target : strategy (collapse absorbing initial (π k)) = collapse absorbing initial (π k) := by
        simpa only [collapsedGame, collapse_state, ha, collapse_idem] using hl
      rw [target]
      simp only [collapse, he, ha]
    | none =>
      have hko : (sharedGame M).owner (π k) = p := by simpa only [collapsedGame, collapse, ha] using hk
      have witness : ∃ w, (sharedGame M).edge (π k) w ∧
          collapse absorbing initial w = strategy (collapse absorbing initial (π k)) := by
        simpa only [collapsedGame, collapse_state, ha, collapse] using hl
      rw [hc k hko]
      exact collapseLift_project M absorbing initial a b u v strategy _ witness
  have win := h.2 _ play (by rw [hs]) consistent
  have he := collapse_parity M absorbing initial valid π hp
  cases p <;> simpa only [Goal, he] using win


private theorem permute_state (p : Perm (Fin n)) (s : Vertex n Q L GI LI) :
    globalState (permute p s) = globalState s := by cases s <;> rfl

private theorem collapse_permute (absorbing : Q → Option Nat) (initial : L)
    (p : Perm (Fin n)) (s : Vertex n Q L GI LI) :
    collapse absorbing initial (permute p s) = permute p (collapse absorbing initial s) := by
  unfold collapse; rw [permute_state]
  cases h : absorbing (globalState s) <;> rfl

private theorem collapsed_edge_act (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (p : Perm (Fin n)) (s t : Vertex n Q L GI LI) (h : (collapsedGame M absorbing initial).edge s t) :
    (collapsedGame M absorbing initial).edge (permute p s) (permute p t) := by
  change (match absorbing (globalState (permute p s)) with
    | some _ => permute p t = collapse absorbing initial (permute p s)
    | none => ∃ w, (sharedGame M).edge (permute p s) w ∧ collapse absorbing initial w = permute p t)
  rw [permute_state]
  cases ha : absorbing (globalState s) with
  | some c =>
    have he : t = collapse absorbing initial s := by simpa only [collapsedGame, ha] using h
    rw [he, collapse_permute]
  | none =>
    obtain ⟨w, hw, ht⟩ := (show ∃ w, (sharedGame M).edge s w ∧ collapse absorbing initial w = t from by
      simpa only [collapsedGame, ha] using h)
    exact ⟨permute p w, shared_edge_act M p _ _ hw, by rw [collapse_permute, ht]⟩

-- Permutation quotient AFTER the two independent implementation compressions; symmetric_count.cc:506–690.
noncomputable def engineAction (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L) :
    Action (collapsedGame M absorbing initial (n := n)) (Perm (Fin n)) where
  act := permute
  inv := Perm.symm
  act_inv p s := by cases s <;> simp only [permute, act_inverse]
  owner_act p s := by
    change (sharedGame M).owner (collapse absorbing initial (permute p s)) =
      (sharedGame M).owner (collapse absorbing initial s)
    rw [collapse_permute]
    generalize collapse absorbing initial s = v
    cases v <;> rfl
  prio_act p s := by
    change (sharedGame M).prio (collapse absorbing initial (permute p s)) =
      (sharedGame M).prio (collapse absorbing initial s)
    rw [collapse_permute]
    generalize collapse absorbing initial s = v
    cases v <;> rfl
  edge_act := collapsed_edge_act M absorbing initial
  transporter := transporter
  canon_orbit := canon_act

-- Full verdict: counts, shared system vertices, absorbing collapse, and both players, including the initial vertex; symmetric_count.cc:529–723.
theorem engine_verdict (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (valid : ∀ q c, absorbing q = some c → Absorbing M q c)
    (a : GI) (b : GO) (u : LI) (v : LO) {p : Bool}
    {strategy : Vertex n Q L GI LI → Vertex n Q L GI LI} {s : Vertex n Q L GI LI}
    (h : (engineAction M absorbing initial).QWins maxOdd p strategy
      ((engineAction M absorbing initial).canon (collapse absorbing initial (share s)))) :
    Wins (game M) maxOdd p
      (sharedLift (collapseLift M absorbing initial a b u v ((engineAction M absorbing initial).lift strategy))) s ∧
    ∀ other, ¬ Wins (game M) maxOdd (!p) other s := by
  have quotientWin := ((engineAction M absorbing initial).verdict h).1
  have win := successor_reuse M (collapse_wins M absorbing initial valid a b u v quotientWin)
  refine ⟨win, fun other ho => ?_⟩
  cases p
  · exact exclusive ho win
  · exact exclusive win ho


-- Input compositions produce exactly the concrete input edges; symmetric_count.cc:576–600.
theorem input_edges (M : Machine Q L GI GO (Fin k) LO) (states : List L) (unique : states.Nodup)
    (q : Q) (c : Nat) (x : Fin n → L) (a : GI) (covered : ∀ i, x i ∈ states)
    (h : (L × Fin k) → Nat) :
    ((∀ z ∈ states, List.ofFn (fun j => h (z, j)) ∈ Builders.typeCounts k (counts x z)) ∧
      (∀ z, z ∉ cells states k → h z = 0)) ↔
    ∃ u : Fin n → Fin k, (game M).edge (.env q c x) (.sys q c x a u) ∧
      key (.sys q c x a u) = Sum.inr (q, c, a, h) := by
  rw [compositions_exact states unique x covered h]
  constructor
  · rintro ⟨u, hu⟩; exact ⟨u, ⟨rfl, rfl, rfl⟩, by simp only [key, hu]⟩
  · rintro ⟨u, _, hu⟩
    exact ⟨u, (Prod.mk.inj (Prod.mk.inj (Prod.mk.inj (Sum.inr.inj hu)).2).2).2⟩

-- Histogram keys and canonical indexed representatives are interchangeable; symmetric_count.cc:506–507,596–600.
theorem canonical_vertex (s t : Vertex n Q L GI LI) :
    permute (transporter s) s = permute (transporter t) t ↔ key s = key t := by
  have hs : key s = key (permute (transporter s) s) := (key_orbit _ _).mpr ⟨transporter s, rfl⟩
  have ht : key t = key (permute (transporter t) t) := (key_orbit _ _).mpr ⟨transporter t, rfl⟩
  constructor
  · intro h; exact hs.trans ((congrArg key h).trans ht.symm)
  · intro h
    obtain ⟨p, hp⟩ := (key_orbit s t).mp h
    rw [← hp]; exact (canon_act p s).symm

-- This is the engine's count key, with shared inputs and collapsed absorbing states already normalized; symmetric_count.cc:529–600.
theorem engine_counts (M : Machine Q L GI GO LI LO) (absorbing : Q → Option Nat) (initial : L)
    (s t : Vertex n Q L GI LI) :
    (engineAction M absorbing initial).canon (collapse absorbing initial (share s)) =
      (engineAction M absorbing initial).canon (collapse absorbing initial (share t)) ↔
    key (collapse absorbing initial (share s)) = key (collapse absorbing initial (share t)) :=
  canonical_vertex _ _

-- Keys contain realizable histograms, never an independently canonicalized successor.
abbrev RawKey (Q L GI LI : Type) :=
  (Q × Nat × (L → Nat)) ⊕ (Q × Nat × GI × ((L × LI) → Nat))
abbrev CountKey (n : Nat) (Q L GI LI : Type) :=
  {k : RawKey Q L GI LI // ∃ s : Vertex n Q L GI LI, key s = k}
noncomputable def countKey (s : Vertex n Q L GI LI) : CountKey n Q L GI LI := ⟨key s, s, rfl⟩
noncomputable def countRep (k : CountKey n Q L GI LI) : Vertex n Q L GI LI := Classical.choose k.2

theorem countRep_key (k : CountKey n Q L GI LI) : countKey (countRep k) = k :=
  Subtype.ext (Classical.choose_spec k.2)

private noncomputable def distinct [DecidableEq L] : List L → List L
  | [] => []
  | a :: xs => let ys := distinct xs; if a ∈ ys then ys else a :: ys
private theorem distinct_mem [DecidableEq L] (xs : List L) (a : L) : a ∈ distinct xs ↔ a ∈ xs := by
  induction xs generalizing a with
  | nil => simp [distinct]
  | cons b xs ih =>
    simp only [distinct]; split <;> simp_all
private theorem distinct_unique [DecidableEq L] (xs : List L) : (distinct xs).Nodup := by
  induction xs with
  | nil => exact List.nodup_nil
  | cons a xs ih => simp only [distinct]; split <;> simp_all
noncomputable def support (x : Fin n → L) : List L := by
  classical exact distinct (List.ofFn x)
private theorem support_unique (x : Fin n → L) : (support x).Nodup := by
  classical exact distinct_unique _
private theorem support_covers (x : Fin n → L) (i : Fin n) : x i ∈ support x := by
  classical exact (distinct_mem _ _).mpr (List.mem_ofFn.mpr ⟨i, rfl⟩)

def Rows (states : List L) (h : L → Nat) (split : (L × Fin k) → Nat) : Prop :=
  (∀ z ∈ states, List.ofFn (fun j => split (z, j)) ∈ Builders.typeCounts k (h z)) ∧
  (∀ z, z ∉ cells states k → split z = 0)

noncomputable def normalizeCounts (absorbing : Q → Option Nat) (initial : L)
    (z : Q × Nat × (L → Nat)) : RawKey Q L GI LI :=
  match absorbing z.1 with
  | none => .inl z
  | some c => .inl (z.1, c, counts (fun _ : Fin n => initial))

private theorem normalized_key (absorbing : Q → Option Nat) (initial : L)
    (q : Q) (c : Nat) (x : Fin n → L) :
    key (collapse absorbing initial (.env q c x : Vertex n Q L GI LI)) =
      normalizeCounts (n := n) absorbing initial (q, c, counts x) := by
  cases h : absorbing q <;> simp [collapse, globalState, h, normalizeCounts, key]

-- Exactly the occupied input/output compositions and weighted updates in :614–712 at 6c6da84a.
noncomputable def countMoves (M : Machine Q L GI GO (Fin ki) (Fin ko))
    (absorbing : Q → Option Nat) (initial : L) (s : Vertex n Q L GI (Fin ki))
    (target : RawKey Q L GI (Fin ki)) : Prop :=
  match absorbing (globalState s) with
  | some _ => target = key (collapse absorbing initial s)
  | none => match s with
    | .env q _ x => ∃ a h, Rows (support x) (counts x) h ∧ target = .inr (q, 0, a, h)
    | .sys q _ x a u => ∃ b h, Rows (support (fun i => (x i, u i))) (counts (fun i => (x i, u i))) h ∧
        target = normalizeCounts (n := n) absorbing initial
          (countTransition M (cells (support (fun i => (x i, u i))) ko) h q a b)

-- Both directions choose the raw edge witness, before either normalization.
theorem countMoves_edges (M : Machine Q L GI GO (Fin ki) (Fin ko))
    (absorbing : Q → Option Nat) (initial : L) (s : Vertex n Q L GI (Fin ki)) (target) :
    countMoves M absorbing initial s target ↔
      ∃ w, (collapsedGame M absorbing initial).edge s w ∧ key w = target := by
  cases ha : absorbing (globalState s) with
  | some c =>
    simp only [countMoves, collapsedGame, ha]
    exact ⟨fun h => ⟨_, rfl, h.symm⟩, by rintro ⟨w, rfl, h⟩; exact h.symm⟩
  | none =>
    cases s with
    | env q c x =>
      change absorbing q = none at ha
      simp only [countMoves, collapsedGame, globalState, ha]
      constructor
      · rintro ⟨a, h, hr, ht⟩
        obtain ⟨u, hu⟩ := (compositions_exact _ (support_unique x) x (support_covers x) h).mp hr
        refine ⟨.sys q 0 x a u, ⟨.sys q 0 x a u, ⟨rfl, rfl, rfl⟩, ?_⟩, ?_⟩
        · simp [collapse, globalState, ha]
        · simp only [key, hu, ht]
      · rintro ⟨w, ⟨z, hz, rfl⟩, ht⟩
        cases z with
        | env => exact False.elim hz
        | sys q' c' x' a u =>
          obtain ⟨rfl, rfl, rfl⟩ := hz
          refine ⟨a, counts (fun i => (x i, u i)),
            (compositions_exact _ (support_unique x) x (support_covers x) _).mpr ⟨u, rfl⟩, ?_⟩
          simpa [collapse, globalState, ha, key] using ht.symm
    | sys q c x a u =>
      change absorbing q = none at ha
      simp only [countMoves, collapsedGame, globalState, ha]
      constructor
      · rintro ⟨b, h, hr, ht⟩
        obtain ⟨v, hv⟩ := (output_edges M _ (support_unique _) q x a b u (support_covers _) _).mp
          ⟨h, hr.1, hr.2, rfl⟩
        let z := transition M q x a b u v
        refine ⟨collapse absorbing initial (.env z.1 z.2.1 z.2.2),
          ⟨.env z.1 z.2.1 z.2.2, ⟨b, v, rfl⟩, rfl⟩, ?_⟩
        rw [normalized_key, hv, ht]
      · rintro ⟨w, ⟨z, hz, rfl⟩, ht⟩
        cases z with
        | sys => exact False.elim hz
        | env q' c' x' =>
          obtain ⟨b, v, hv⟩ := hz
          obtain ⟨h, hr, ho, hh⟩ := (output_edges M _ (support_unique _) q x a b u (support_covers _)
            (q', c', counts x')).mpr ⟨v, by rw [hv]⟩
          exact ⟨b, h, ⟨hr, ho⟩, by rw [hh, ← normalized_key]; exact ht.symm⟩

noncomputable def countGame (M : Machine Q L GI GO (Fin ki) (Fin ko))
    (absorbing : Q → Option Nat) (initial : L) : Game (CountKey n Q L GI (Fin ki)) where
  owner k := (collapsedGame M absorbing initial).owner (countRep k)
  prio k := (collapsedGame M absorbing initial).prio (countRep k)
  edge k k' := countMoves M absorbing initial (countRep k) k'.val

private theorem countRep_orbit (s : Vertex n Q L GI LI) : ∃ p, permute p s = countRep (countKey s) :=
  (key_orbit _ _).mp (congrArg Subtype.val (countRep_key (countKey s))).symm

theorem countGame_owner (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (s : Vertex n Q L GI (Fin ki)) :
    (countGame M absorbing initial).owner (countKey s) = (collapsedGame M absorbing initial).owner s := by
  obtain ⟨p, hp⟩ := countRep_orbit s
  change (collapsedGame M absorbing initial).owner (countRep (countKey s)) = _
  rw [← hp]
  exact (engineAction M absorbing initial).owner_act p s

theorem countGame_prio (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (s : Vertex n Q L GI (Fin ki)) :
    (countGame M absorbing initial).prio (countKey s) = (collapsedGame M absorbing initial).prio s := by
  obtain ⟨p, hp⟩ := countRep_orbit s
  change (collapsedGame M absorbing initial).prio (countRep (countKey s)) = _
  rw [← hp]
  exact (engineAction M absorbing initial).prio_act p s

theorem countGame_edges (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (s : Vertex n Q L GI (Fin ki)) (k) :
    (countGame M absorbing initial).edge (countKey s) k ↔
      ∃ w, (collapsedGame M absorbing initial).edge s w ∧ countKey w = k := by
  change countMoves M absorbing initial (countRep (countKey s)) k.val ↔ _
  rw [countMoves_edges]
  obtain ⟨p, hp⟩ := countRep_orbit s
  have same (g : Perm (Fin n)) (w : Vertex n Q L GI (Fin ki)) : key (permute g w) = key w :=
    ((key_orbit _ _).mpr ⟨g, rfl⟩).symm
  constructor
  · rintro ⟨w, hw, hk⟩
    refine ⟨permute p.symm w, ?_, Subtype.ext ((same _ _).trans hk)⟩
    have he := collapsed_edge_act M absorbing initial p.symm _ _ hw
    rw [← hp] at he
    have hi : permute p.symm (permute p s) = s := (engineAction M absorbing initial).act_inv p s
    rw [hi] at he
    exact he
  · rintro ⟨w, hw, hk⟩
    exact ⟨permute p w, hp ▸ collapsed_edge_act M absorbing initial p _ _ hw,
      (same _ _).trans (congrArg Subtype.val hk)⟩

theorem countKey_canon (M : Machine Q L GI GO LI LO) (absorbing) (initial)
    (s : Vertex n Q L GI LI) :
    countKey ((engineAction M absorbing initial).canon s) = countKey s :=
  Subtype.ext ((key_orbit _ _).mpr ⟨transporter s, rfl⟩).symm

-- On represented vertices, these are exactly the edges of Quotient.lean.
theorem count_quotient_edges (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (s t : Vertex n Q L GI (Fin ki)) :
    (countGame M absorbing initial).edge (countKey s) (countKey t) ↔
      (engineAction M absorbing initial).quotient.edge
        ((engineAction M absorbing initial).canon s) ((engineAction M absorbing initial).canon t) := by
  rw [← countKey_canon M absorbing initial s, countGame_edges]
  exact ⟨by rintro ⟨w, hw, hk⟩; exact ⟨w, hw, (canonical_vertex w t).mpr (congrArg Subtype.val hk)⟩,
    by rintro ⟨w, hw, hk⟩; exact ⟨w, hw, Subtype.ext ((canonical_vertex w t).mp hk)⟩⟩

private theorem shared_target (M : Machine Q L GI GO LI LO) (s t : Vertex n Q L GI LI)
    (h : (sharedGame M).edge s t) : share t = t := by
  cases s <;> cases t <;> first
  | exact False.elim h
  | rfl
  | obtain ⟨rfl, rfl, rfl⟩ := h; rfl

-- Every generated key has zero system color or the collapsed absorbing environment form.
theorem count_normalized_target (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (k k' : CountKey n Q L GI (Fin ki)) (h : (countGame M absorbing initial).edge k k') :
    ∃ s, countKey (collapse absorbing initial (share s)) = k' := by
  have he : (countGame M absorbing initial).edge (countKey (countRep k)) k' := by rw [countRep_key]; exact h
  obtain ⟨w, hw, hk⟩ := (countGame_edges M absorbing initial (countRep k) k').mp he
  cases ha : absorbing (globalState (countRep k)) with
  | some c =>
    have ht : w = collapse absorbing initial (countRep k) := by simpa only [collapsedGame, ha] using hw
    refine ⟨countRep k, ?_⟩
    have hs : globalState (share (countRep k)) = globalState (countRep k) := by cases countRep k <;> rfl
    have hn : collapse absorbing initial (share (countRep k)) = collapse absorbing initial (countRep k) := by
      simp only [collapse, hs, ha]
    rw [hn, ← ht, hk]
  | none =>
    obtain ⟨z, hz, ht⟩ := (show ∃ z, (sharedGame M).edge (countRep k) z ∧ collapse absorbing initial z = w from by
      simpa only [collapsedGame, ha] using hw)
    exact ⟨z, by rw [shared_target M _ _ hz, ht, hk]⟩

-- The premise is now an ordinary winning strategy on the computed count-key graph.
theorem count_verdict (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (valid : ∀ q c, absorbing q = some c → Absorbing M q c)
    (a : GI) (b : GO) (u : Fin ki) (v : Fin ko) {p : Bool}
    {σ : CountKey n Q L GI (Fin ki) → CountKey n Q L GI (Fin ki)} {s : Vertex n Q L GI (Fin ki)}
    (h : Wins (countGame M absorbing initial) maxOdd p σ (countKey (collapse absorbing initial (share s)))) :
    (∃ strategy, Wins (game M) maxOdd p strategy s) ∧ ∀ other, ¬ Wins (game M) maxOdd (!p) other s := by
  have hc := (engineAction M absorbing initial).key_wins (countGame M absorbing initial) countKey
    (countGame_owner M absorbing initial) (countGame_prio M absorbing initial)
    (countKey_canon M absorbing initial)
    (countGame_edges M absorbing initial) h
  have hv := engine_verdict M absorbing initial valid a b u v hc
  exact ⟨⟨_, hv.1⟩, hv.2⟩

-- The graph is total even outside the explored, normalized keys.
theorem countGame_total (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (a : GI) (b : GO) (u : Fin ki) (v : Fin ko) (k : CountKey n Q L GI (Fin ki)) :
    ∃ k', (countGame M absorbing initial).edge k k' := by
  let s := countRep k
  suffices ∃ w, (collapsedGame M absorbing initial).edge s w by
    obtain ⟨w, hw⟩ := this
    refine ⟨countKey w, ?_⟩
    rw [← countRep_key k]
    exact (countGame_edges M absorbing initial s _).mpr ⟨w, hw, rfl⟩
  cases ha : absorbing (globalState s) with
  | some c => exact ⟨collapse absorbing initial s, by simp [collapsedGame, ha]⟩
  | none => exact ⟨collapse absorbing initial (sharedDefault M a b u v s), by
      simp only [collapsedGame, ha]
      exact ⟨sharedDefault M a b u v s, sharedDefault_edge M a b u v s, rfl⟩⟩

-- This premise is only the reachable count graph checked by the engine's solver.
theorem count_certificate (M : Machine Q L GI GO (Fin ki) (Fin ko)) (absorbing) (initial)
    (valid : ∀ q c, absorbing q = some c → Absorbing M q c)
    (a : GI) (b : GO) (u : Fin ki) (v : Fin ko) {p : Bool}
    {σ : CountKey n Q L GI (Fin ki) → CountKey n Q L GI (Fin ki)} {s : Vertex n Q L GI (Fin ki)}
    (h : Certified (countGame M absorbing initial) maxOdd p σ
      (countKey (collapse absorbing initial (share s)))) :
    (∃ strategy, Wins (game M) maxOdd p strategy s) ∧ ∀ other, ¬ Wins (game M) maxOdd (!p) other s := by
  obtain ⟨τ, ht⟩ := h.wins (countGame_total M absorbing initial a b u v)
  exact count_verdict M absorbing initial valid a b u v ht

end Symmetry.CountGame

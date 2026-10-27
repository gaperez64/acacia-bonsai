import SymmetryProofs.Automaton

namespace Symmetry
def unique [DecidableEq α] : List α → List α
  | [] => []
  | x :: xs => if x ∈ unique xs then unique xs else x :: unique xs
@[simp] theorem mem_unique [DecidableEq α] (x : α) (xs : List α) : x ∈ unique xs ↔ x ∈ xs := by
  induction xs with
  | nil => simp [unique]
  | cons y ys ih => by_cases h : y ∈ unique ys <;> simp_all [unique]; grind
theorem unique_nodup [DecidableEq α] (xs : List α) : (unique xs).Nodup := by
  induction xs with
  | nil => simp [unique]
  | cons x xs ih => by_cases h : x ∈ unique xs <;> simp_all [unique, List.nodup_cons]
theorem finite_closed_iff (xs : List α) (p : Perm α) -- Finite injectivity supplies the unchecked edge converse; symmetry.hh:235–248.
    (closed : ∀ x ∈ xs, p x ∈ xs) (x : α) : p x ∈ xs ↔ x ∈ xs := by
  classical
  refine ⟨fun hx => ?_, closed x⟩
  by_cases hn : x ∈ xs
  · exact hn
  · apply False.elim
    let ys := unique xs
    have nod : (ys.map p).Nodup := (unique_nodup xs).map p (fun _ _ hne he => hne (p.injective he))
    have missing : p x ∉ ys.map p := by
      intro hm; obtain ⟨z, hz, he⟩ := List.mem_map.mp hm
      have : z = x := p.injective he; subst z
      exact hn ((mem_unique _ _).mp hz)
    have sub : p x :: ys.map p ⊆ ys := by
      intro z hz; simp only [List.mem_cons, List.mem_map] at hz
      rcases hz with he | ⟨w, hw, he⟩
      · subst z; exact (mem_unique _ _).mpr hx
      · subst z; exact (mem_unique _ _).mpr (closed _ ((mem_unique _ _).mp hw))
    have hlen := (List.nodup_cons.mpr ⟨missing, nod⟩).length_le_of_subset sub
    simp only [List.length_cons, List.length_map] at hlen; omega

structure Candidate (n a : Nat) where -- Completed detector tables; symmetry.hh:148–209.
  phi : Fin n → Fin n
  phiInv : Fin n → Fin n
  pi : Fin a → Fin a
  piInv : Fin a → Fin a
  families : List (Bool × List (Fin a)) := []
def shapeCheck (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ q, (A.edges.filter (fun e => e.src == q)).length = -- Preserve degrees and label multiplicities; symmetry.hh:120–139,167–182.
      (A.edges.filter (fun e => e.src == c.phi q)).length ∧ (A.edges.filter (fun e => e.dst == q)).length =
      (A.edges.filter (fun e => e.dst == c.phi q)).length) &&
  A.edges.all (fun e =>
    (A.edges.filter (fun d => d.src == e.src &&
      (letters a).all (fun v => d.label.eval v == e.label.eval v))).length ==
    (A.edges.filter (fun d => d.src == c.phi e.src &&
      (letters a).all (fun v => d.label.eval (fun p => v (c.piInv p)) == e.label.eval v))).length)
-- Check inverses, init, acceptance and renamed edges; symmetry.hh:235–277.
def structuralCheck (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ q, c.phiInv (c.phi q) = q ∧ c.phi (c.phiInv q) = q) &&
  decide (∀ p, c.piInv (c.pi p) = p ∧ c.pi (c.piInv p) = p) &&
  (c.phi A.init == A.init) &&
  decide (∀ q, A.rejecting (c.phi q) = A.rejecting q) &&
  A.edges.all (fun e => A.edges.any fun d =>
    d.src == c.phi e.src && d.dst == c.phi e.dst &&
    (letters a).all fun v => d.label.eval (fun p => v (c.piInv p)) == e.label.eval v)
def familyGuard (A : Automaton n a) (c : Candidate n a) : Bool := -- Reject every impure I/O family; symmetry.hh:326–335.
  c.families.all fun family => family.2.all fun p => A.isInput p == family.1
def Generated (c : Candidate n a) : Prop := -- AP transpositions move only within nominated families; symmetry.hh:260–264.
  (∀ p, c.pi (c.pi p) = p) ∧ ∀ p, c.pi p = p ∨ ∃ family ∈ c.families, p ∈ family.2 ∧ c.pi p ∈ family.2
def partitionGuard (A : Automaton n a) (c : Candidate n a) : Bool := -- Preserve each AP owner; symmetry.hh:326–335.
  decide (∀ p, A.isInput (c.pi p) = A.isInput p)
def capGuard (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ q, A.counting (c.phi q) = A.counting q) -- Preserve the Boolean/counting split; equivariant_k_bounded_safety_aut.hh:851–853.
def unguardedCheck (A : Automaton n a) (c : Candidate n a) : Bool := -- Detector before the family filter; symmetry.hh:235–277.
  structuralCheck A c && capGuard A c && shapeCheck A c
def check (A : Automaton n a) (c : Candidate n a) : Bool := -- Executable detector plus downstream cap guard; symmetry.hh:235–277,326–335.
  structuralCheck A c && partitionGuard A c && capGuard A c && familyGuard A c && shapeCheck A c
theorem generated_partition (A : Automaton n a) (c : Candidate n a) -- Pure families imply per-AP ownership; symmetry.hh:326–335.
    (hg : Generated c) (hf : familyGuard A c = true) : partitionGuard A c = true := by
  apply decide_eq_true (p := ∀ p, A.isInput (c.pi p) = A.isInput p); intro p
  rcases hg.2 p with hp | ⟨family, hm, hi, ho⟩
  · rw [hp]
  · have hall := List.all_eq_true.mp hf family hm
    have h1 := beq_iff_eq.mp (List.all_eq_true.mp hall p hi)
    have h2 := beq_iff_eq.mp (List.all_eq_true.mp hall (c.pi p) ho); exact h2.trans h1.symm
theorem check_matches_source (A : Automaton n a) (c : Candidate n a) (hg : Generated c) :
    check A c = (structuralCheck A c && capGuard A c && familyGuard A c && shapeCheck A c) := by
  by_cases hf : familyGuard A c = true -- The explicit partition assertion is redundant; symmetry.hh:326–335.
  · simp [check, generated_partition A c hg hf, hf]
  · have hf' : familyGuard A c = false := Bool.eq_false_iff.mpr hf
    simp [check, hf']
structure Verified (A : Automaton n a) where -- Soundness certificate for checked maps; symmetry.hh:9–17.
  states : Perm (Fin n)
  aps : Perm (Fin a)
  init_fixed : states A.init = A.init
  acceptance : ∀ q, A.rejecting (states q) = A.rejecting q
  partition : ∀ p, A.isInput (aps p) = A.isInput p
  caps : ∀ q, A.counting (states q) = A.counting q
  forward : ∀ e ∈ A.edges, ∃ d ∈ A.edges, d.src = states e.src ∧ d.dst = states e.dst ∧
      ∀ v, d.label.eval (act aps v) = e.label.eval v
-- Successful checks construct the certificate; symmetry.hh:235–277.
def accepted_sound (A : Automaton n a) (c : Candidate n a) (h : check A c = true) : Verified A := by
  simp only [check, structuralCheck, partitionGuard, capGuard, Bool.and_eq_true,
    decide_eq_true_eq, beq_iff_eq] at h
  obtain ⟨⟨⟨⟨⟨⟨⟨⟨hφ, hπ⟩, hi⟩, ha⟩, he⟩, hp⟩, hc⟩, _⟩, _⟩ := h
  let φ : Perm (Fin n) := ⟨c.phi, c.phiInv, fun q => (hφ q).1, fun q => (hφ q).2⟩
  let π : Perm (Fin a) := ⟨c.pi, c.piInv, fun p => (hπ p).1, fun p => (hπ p).2⟩
  refine ⟨φ, π, hi, ha, hp, hc, ?_⟩
  intro e hm; obtain ⟨d, hd, hm⟩ := List.any_eq_true.mp (List.all_eq_true.mp he e hm)
  simp only [Bool.and_eq_true, beq_iff_eq] at hm
  refine ⟨d, hd, hm.1.1, hm.1.2, ?_⟩
  intro v; exact beq_iff_eq.mp (List.all_eq_true.mp hm.2 v (mem_letters v))
theorem accepted_properties (A : Automaton n a) (c : Candidate n a) (h : check A c = true) :
    (∀ x y, c.phi x = c.phi y → x = y) ∧ (∀ y, ∃ x, c.phi x = y) ∧
    (∀ x y, c.pi x = c.pi y → x = y) ∧ (∀ y, ∃ x, c.pi x = y) ∧ c.phi A.init = A.init ∧
    (∀ q, A.rejecting (c.phi q) = A.rejecting q) ∧ (∀ p, A.isInput (c.pi p) = A.isInput p) ∧
    (∀ q, A.counting (c.phi q) = A.counting q) := by
  let s := accepted_sound A c h -- Accepted maps are bijective and preserve init, owners and caps; symmetry.hh:9–17.
  exact ⟨fun _ _ he => s.states.injective he, fun y => ⟨s.states.invFun y, s.states.right_inv y⟩,
    fun _ _ he => s.aps.injective he, fun y => ⟨s.aps.invFun y, s.aps.right_inv y⟩,
    s.init_fixed, s.acceptance, s.partition, s.caps⟩
abbrev EdgeKey (n a : Nat) := Fin n × ((Fin a → Bool) → Bool) × Fin n
def edgeKey (e : Edge n a) : EdgeKey n a := (e.src, e.label.eval, e.dst)

variable {A : Automaton n a}
def keyPerm (s : Verified A) : Perm (EdgeKey n a) where
  toFun e := (s.states e.1, (fun v => e.2.1 (act s.aps.symm v)), s.states e.2.2)
  invFun e := (s.states.invFun e.1, (fun v => e.2.1 (act s.aps v)), s.states.invFun e.2.2)
  left_inv e := by rcases e with ⟨q, l, r⟩; simp
  right_inv e := by rcases e with ⟨q, l, r⟩; simp
theorem key_closed (s : Verified A) (x : EdgeKey n a)
    (hx : x ∈ A.edges.map edgeKey) : keyPerm s x ∈ A.edges.map edgeKey := by
  obtain ⟨e, he, rfl⟩ := List.mem_map.mp hx
  obtain ⟨d, hd, hs, ht, hl⟩ := s.forward e he
  apply List.mem_map.mpr; refine ⟨d, hd, ?_⟩
  apply Prod.ext hs; apply Prod.ext _ ht
  funext v; have h := hl (act s.aps.symm v)
  simpa [edgeKey, keyPerm] using h
theorem edges_both_directions (s : Verified A) (x : EdgeKey n a) :
    keyPerm s x ∈ A.edges.map edgeKey ↔ x ∈ A.edges.map edgeKey :=
  finite_closed_iff _ _ (key_closed s) x -- The one-way edge check also reflects labeled edges; symmetry.hh:235–248.
theorem enabled_commutes (s : Verified A) (q r : Fin n) (v : Fin a → Bool) :
    A.enabled (s.states q) (s.states r) (act s.aps v) = A.enabled q r v := by
  apply Bool.eq_iff_iff.mpr; constructor -- Enabled branching is preserved in both directions; symmetry.hh:9–17.
  · intro h
    obtain ⟨d, hd, hs, ht, hl⟩ := (enabled_iff _ _ _ _).mp h
    let x := (keyPerm s).invFun (edgeKey d)
    have hx : x ∈ A.edges.map edgeKey :=
      (edges_both_directions s x).mp (by simpa [x] using List.mem_map.mpr ⟨d, hd, rfl⟩)
    obtain ⟨e, he, hk⟩ := List.mem_map.mp hx
    have eqs : e.src = q := by
      have := congrArg Prod.fst hk; simpa [x, keyPerm, edgeKey, hs] using this
    have eqt : e.dst = r := by
      have := congrArg (fun k : EdgeKey n a => k.2.2) hk; simpa [x, keyPerm, edgeKey, ht] using this
    have eql : e.label.eval v = true := by
      have := congrArg (fun k : EdgeKey n a => k.2.1 v) hk; simpa [x, keyPerm, edgeKey, hl] using this
    exact (enabled_iff _ _ _ _).mpr ⟨e, he, eqs, eqt, eql⟩
  · intro h
    obtain ⟨e, he, hs, ht, hl⟩ := (enabled_iff _ _ _ _).mp h
    obtain ⟨d, hd, hds, hdt, hdl⟩ := s.forward e he
    exact (enabled_iff _ _ _ _).mpr ⟨d, hd, by simpa [hs] using hds,
      by simpa [ht] using hdt, by simpa [hl] using hdl v⟩

end Symmetry

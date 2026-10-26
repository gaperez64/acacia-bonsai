import SymmetryProofs.Finite

namespace Symmetry

/-- C++: symmetry.hh:148–159, 202–209, 260–264. Raw completed candidate tables. -/
structure Candidate (n a : Nat) where
  phi : Fin n → Fin n
  phiInv : Fin n → Fin n
  pi : Fin a → Fin a
  piInv : Fin a → Fin a
  families : List (Bool × List (Fin a)) := []

/-- C++: symmetry.hh:120–139, 167–182. Degree and label-multiset checks
    used by the signature/consistency pruning on a completed candidate. -/
def shapeCheck (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ q,
    (A.edges.filter (fun e => e.src == q)).length =
      (A.edges.filter (fun e => e.src == c.phi q)).length ∧
    (A.edges.filter (fun e => e.dst == q)).length =
      (A.edges.filter (fun e => e.dst == c.phi q)).length) &&
  A.edges.all (fun e =>
    (A.edges.filter (fun d => d.src == e.src &&
      (letters a).all (fun v => d.label.eval v == e.label.eval v))).length ==
    (A.edges.filter (fun d => d.src == c.phi e.src &&
      (letters a).all (fun v => d.label.eval (fun p => v (c.piInv p)) == e.label.eval v))).length)

/-- C++: symmetry.hh:167–197, 235–248, 270–277. Final candidate validation;
    label identity is truth-table identity, as for canonical BDDs. -/
def structuralCheck (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ q, c.phiInv (c.phi q) = q ∧ c.phi (c.phiInv q) = q) &&
  decide (∀ p, c.piInv (c.pi p) = p ∧ c.pi (c.piInv p) = p) &&
  (c.phi A.init == A.init) &&
  decide (∀ q, A.rejecting (c.phi q) = A.rejecting q) &&
  A.edges.all (fun e => A.edges.any fun d =>
    d.src == c.phi e.src && d.dst == c.phi e.dst &&
    (letters a).all fun v => d.label.eval (fun p => v (c.piInv p)) == e.label.eval v)

/-- C++ at 35a8723a: symmetry.hh:326–335. Family-wide I/O filter. -/
def familyGuard (A : Automaton n a) (c : Candidate n a) : Bool :=
  c.families.all fun family => family.2.all fun p => A.isInput p == family.1

/-- C++: symmetry.hh:260–264. Candidate AP maps are constructed within families. -/
def Generated (c : Candidate n a) : Prop :=
  (∀ p, c.pi (c.pi p) = p) ∧ ∀ p, c.pi p = p ∨
    ∃ family ∈ c.families, p ∈ family.2 ∧ c.pi p ∈ family.2

/-- C++ at 35a8723a: symmetry.hh:326–335. All moved APs stay on their actual
    side; the source enforces the stronger condition that each family is pure. -/
def partitionGuard (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ p, A.isInput (c.pi p) = A.isInput p)

/-- C++: equivariant_k_bounded_safety_aut.hh:636–643, 851–853. -/
def capGuard (A : Automaton n a) (c : Candidate n a) : Bool :=
  decide (∀ q, A.counting (c.phi q) = A.counting q)

/-- C++ before PR #330: structural validation plus the downstream cap guard,
    without the family-purity or per-AP I/O guard. -/
def unguardedCheck (A : Automaton n a) (c : Candidate n a) : Bool :=
  structuralCheck A c && capGuard A c && shapeCheck A c

/-- C++: symmetry.hh:235–277 plus 35a8723a:326–335; equivariant:851–853.
    Combined candidate check, including the downstream Boolean-coordinate guard. -/
def check (A : Automaton n a) (c : Candidate n a) : Bool :=
  structuralCheck A c && partitionGuard A c && capGuard A c && familyGuard A c && shapeCheck A c

/-- Pure families and within-family moves imply the per-AP guard, so its
    explicit validation does not strengthen the C++ check on generated candidates.
    PR #330:326–335, symmetry.hh:9–17, issue #331. -/
theorem generated_partition (A : Automaton n a) (c : Candidate n a)
    (hg : Generated c) (hf : familyGuard A c = true) : partitionGuard A c = true := by
  apply decide_eq_true (p := ∀ p, A.isInput (c.pi p) = A.isInput p)
  intro p
  rcases hg.2 p with hp | ⟨family, hm, hi, ho⟩
  · rw [hp]
  · have hall := List.all_eq_true.mp hf family hm
    have h1 := beq_iff_eq.mp (List.all_eq_true.mp hall p hi)
    have h2 := beq_iff_eq.mp (List.all_eq_true.mp hall (c.pi p) ho)
    exact h2.trans h1.symm

/-- The port's candidate check equals the guarded C++ check on generated candidates;
    the extra per-AP assertion is redundant. PR #330 and issue #331. -/
theorem check_matches_source (A : Automaton n a) (c : Candidate n a) (hg : Generated c) :
    check A c = (structuralCheck A c && capGuard A c && familyGuard A c && shapeCheck A c) := by
  by_cases hf : familyGuard A c = true
  · simp [check, generated_partition A c hg hf, hf]
  · have hf' : familyGuard A c = false := Bool.eq_false_iff.mpr hf
    simp [check, hf']

/-- C++: symmetry_certificate.hh:8–17. Hints nominate candidates; they never
    replace checking. The checked certificate contains only the soundness facts. -/
structure Verified (A : Automaton n a) where
  states : Perm (Fin n)
  aps : Perm (Fin a)
  init_fixed : states A.init = A.init
  acceptance : ∀ q, A.rejecting (states q) = A.rejecting q
  partition : ∀ p, A.isInput (aps p) = A.isInput p
  caps : ∀ q, A.counting (states q) = A.counting q
  forward : ∀ e ∈ A.edges, ∃ d ∈ A.edges,
    d.src = states e.src ∧ d.dst = states e.dst ∧
      ∀ v, d.label.eval (act aps v) = e.label.eval v

/-- Every accepted candidate is bijective, fixes init, preserves acceptance,
    I/O and caps, and maps every edge to a matching edge, satisfying the
    soundness conditions in symmetry.hh:9–17 and issue #331. -/
def accepted_sound (A : Automaton n a) (c : Candidate n a) (h : check A c = true) :
    Verified A := by
  simp only [check, structuralCheck, partitionGuard, capGuard, Bool.and_eq_true,
    decide_eq_true_eq, beq_iff_eq] at h
  obtain ⟨⟨⟨⟨⟨⟨⟨⟨hφ, hπ⟩, hi⟩, ha⟩, he⟩, hp⟩, hc⟩, _⟩, _⟩ := h
  let φ : Perm (Fin n) := ⟨c.phi, c.phiInv, fun q => (hφ q).1, fun q => (hφ q).2⟩
  let π : Perm (Fin a) := ⟨c.pi, c.piInv, fun p => (hπ p).1, fun p => (hπ p).2⟩
  refine ⟨φ, π, hi, ha, hp, hc, ?_⟩
  intro e hm
  obtain ⟨d, hd, hm⟩ := List.any_eq_true.mp (List.all_eq_true.mp he e hm)
  simp only [Bool.and_eq_true, beq_iff_eq] at hm
  refine ⟨d, hd, hm.1.1, hm.1.2, ?_⟩
  intro v
  exact beq_iff_eq.mp (List.all_eq_true.mp hm.2 v (mem_letters v))

/-- State and AP maps accepted by the executable check are injective and
    surjective; init, rejecting status, I/O and caps are preserved. The inverse
    tables encode the source's assign/inv construction, symmetry.hh:144–277;
    these are the first obligations of symmetry.hh:9–17 and issue #331. -/
theorem accepted_properties (A : Automaton n a) (c : Candidate n a) (h : check A c = true) :
    (∀ x y, c.phi x = c.phi y → x = y) ∧ (∀ y, ∃ x, c.phi x = y) ∧
    (∀ x y, c.pi x = c.pi y → x = y) ∧ (∀ y, ∃ x, c.pi x = y) ∧
    c.phi A.init = A.init ∧
    (∀ q, A.rejecting (c.phi q) = A.rejecting q) ∧
    (∀ p, A.isInput (c.pi p) = A.isInput p) ∧
    (∀ q, A.counting (c.phi q) = A.counting q) := by
  let s := accepted_sound A c h
  exact ⟨fun _ _ he => s.states.injective he,
    fun y => ⟨s.states.invFun y, s.states.right_inv y⟩,
    fun _ _ he => s.aps.injective he, fun y => ⟨s.aps.invFun y, s.aps.right_inv y⟩,
    s.init_fixed, s.acceptance, s.partition, s.caps⟩

/-- C++: symmetry.hh:112–117, 235–248. Extensional keys forget BDD syntax. -/
abbrev EdgeKey (n a : Nat) := Fin n × ((Fin a → Bool) → Bool) × Fin n

/-- C++: symmetry.hh:112–117. State acceptance is recorded separately in Automaton.rejecting. -/
def edgeKey (e : Edge n a) : EdgeKey n a := (e.src, e.label.eval, e.dst)

variable {A : Automaton n a}

/-- C++: symmetry.hh:260–269. Relabel both endpoints and the Boolean label. -/
def keyPerm (s : Verified A) : Perm (EdgeKey n a) where
  toFun e := (s.states e.1, (fun v => e.2.1 (act s.aps.symm v)), s.states e.2.2)
  invFun e := (s.states.invFun e.1, (fun v => e.2.1 (act s.aps v)), s.states.invFun e.2.2)
  left_inv e := by rcases e with ⟨q, l, r⟩; simp
  right_inv e := by rcases e with ⟨q, l, r⟩; simp

theorem key_closed (s : Verified A) (x : EdgeKey n a)
    (hx : x ∈ A.edges.map edgeKey) : keyPerm s x ∈ A.edges.map edgeKey := by
  obtain ⟨e, he, rfl⟩ := List.mem_map.mp hx
  obtain ⟨d, hd, hs, ht, hl⟩ := s.forward e he
  apply List.mem_map.mpr
  refine ⟨d, hd, ?_⟩
  apply Prod.ext hs
  apply Prod.ext _ ht
  funext v
  have h := hl (act s.aps.symm v)
  simpa [edgeKey, keyPerm] using h

/-- The C++ checks edges in one direction. Bijectivity and the finite edge list
    prove the converse, without strengthening verify_full; symmetry.hh:235–248,
    symmetry.hh:9–17, issue #331. Labels match on every valuation. -/
theorem edges_both_directions (s : Verified A) (x : EdgeKey n a) :
    keyPerm s x ∈ A.edges.map edgeKey ↔ x ∈ A.edges.map edgeKey :=
  finite_closed_iff _ _ (key_closed s) x

/-- Enabled transitions match in both directions, so renaming preserves all
    branches of a universal successor; symmetry.hh:9–17, issue #331. -/
theorem enabled_commutes (s : Verified A) (q r : Fin n) (v : Fin a → Bool) :
    A.enabled (s.states q) (s.states r) (act s.aps v) = A.enabled q r v := by
  apply Bool.eq_iff_iff.mpr
  constructor
  · intro h
    obtain ⟨d, hd, hs, ht, hl⟩ := (enabled_iff _ _ _ _).mp h
    let x := (keyPerm s).invFun (edgeKey d)
    have hx : x ∈ A.edges.map edgeKey :=
      (edges_both_directions s x).mp (by simpa [x] using List.mem_map.mpr ⟨d, hd, rfl⟩)
    obtain ⟨e, he, hk⟩ := List.mem_map.mp hx
    have eqs : e.src = q := by
      have := congrArg Prod.fst hk
      simpa [x, keyPerm, edgeKey, hs] using this
    have eqt : e.dst = r := by
      have := congrArg (fun k : EdgeKey n a => k.2.2) hk
      simpa [x, keyPerm, edgeKey, ht] using this
    have eql : e.label.eval v = true := by
      have := congrArg (fun k : EdgeKey n a => k.2.1 v) hk
      simpa [x, keyPerm, edgeKey, hl] using this
    exact (enabled_iff _ _ _ _).mpr ⟨e, he, eqs, eqt, eql⟩
  · intro h
    obtain ⟨e, he, hs, ht, hl⟩ := (enabled_iff _ _ _ _).mp h
    obtain ⟨d, hd, hds, hdt, hdl⟩ := s.forward e he
    exact (enabled_iff _ _ _ _).mpr ⟨d, hd, by simpa [hs] using hds,
      by simpa [ht] using hdt, by simpa [hl] using hdl v⟩

end Symmetry

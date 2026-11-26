import SymmetryProofs.CountEngine
import SymmetryProofs.Transports

namespace Symmetry.CountCardinality

open CountGame CountEngine Symmetry.Quotient

-- One family's current-letter Boolean function, with all index transpositions checked.
def Symmetric (f : (Fin n → Bool) → Bool) : Prop :=
  ∀ i j x, f (act (transposition i j) x) = f x

theorem invariant (f : (Fin n → Bool) → Bool) (symmetric : Symmetric f)
    (p : Perm (Fin n)) (x : Fin n → Bool) : f (act p x) = f x := by
  apply transpositions_generate (List.finRange n) List.mem_finRange
    (fun p => ∀ x, f (act p x) = f x) (fun _ => rfl) symmetric
    (fun p q hp hq x => by rw [act_comp, hp, hq]) p x

private theorem bool_counts (xs : List Bool) : xs.count false + xs.count true = xs.length := by
  induction xs with
  | nil => simp
  | cons b xs ih => cases b <;> simp_all <;> omega

-- True-count equality already determines the entire Boolean histogram.
theorem count_factor (f : (Fin n → Bool) → Bool) (symmetric : Symmetric f)
    (x y : Fin n → Bool) (same : counts x true = counts y true) : f x = f y := by
  have hx : counts x false + counts x true = n := by
    simpa only [counts_list, List.length_ofFn] using bool_counts (List.ofFn x)
  have hy : counts y false + counts y true = n := by
    simpa only [counts_list, List.length_ofFn] using bool_counts (List.ofFn y)
  have hc : counts x = counts y := by
    funext b; cases b
    · omega
    · exact same
  obtain ⟨p, rfl⟩ := (counts_orbit x y).mp hc
  exact (invariant f symmetric p x).symm

-- Representatives compute accepted weights; the C++ BDD reconstruction checks this equality again.
theorem accepted_weights (f : (Fin n → Bool) → Bool) (symmetric : Symmetric f) :
    ∃ accepted : Nat → Bool, ∀ x, f x = accepted (counts x true) := by
  classical
  let accepted k := if h : ∃ x : Fin n → Bool, counts x true = k then f (Classical.choose h) else false
  refine ⟨accepted, fun x => ?_⟩
  have h : ∃ y : Fin n → Bool, counts y true = counts x true := ⟨x, rfl⟩
  simp only [accepted, dite_eq_left h]
  exact count_factor f symmetric x (Classical.choose h) (Classical.choose_spec h).symm

-- The same predicate holds at every count representative, hence in the count quotient.
theorem quotient_constraint (f : (Fin n → Bool) → Bool) (symmetric : Symmetric f)
    (x : Fin n → Bool) : f (canonical x) = f x :=
  count_factor f symmetric _ _ (congrFun (canonical_counts x) true)

-- Pointwise equality permits replacement in any temporal context or polarity.
theorem constraint_context (f : (Fin n → Bool) → Bool) (symmetric : Symmetric f)
    (word : Nat → Fin n → Bool) (context : (Nat → Bool) → Prop) :
    ∃ accepted : Nat → Bool,
      context (fun t => f (word t)) ↔ context (fun t => accepted (counts (word t) true)) := by
  obtain ⟨accepted, correct⟩ := accepted_weights f symmetric
  exact ⟨accepted, Iff.of_eq (congrArg context (funext fun t => correct (word t)))⟩

-- Accepted counts on concrete letters are exactly the weighted sum used by count successors.
theorem weighted_constraint (domain : List α) (unique : domain.Nodup) (x : Fin n → α)
    (covered : ∀ i, x i ∈ domain) (letter : α → Bool) (accepted : Nat → Bool) :
    accepted (pushCounts domain (counts x) letter true) =
      accepted (counts (fun i => letter (x i)) true) := by
  rw [push_counts domain x unique covered letter]

-- Deleting excluded system moves means a system strategy must keep all its outcomes safe.
-- For the environment, only safe outcomes remain to be checked. An empty safe action set
-- is a system loss, not a vacuous system win (the first conjunct below prevents that).
-- This formulation also covers plays stopped at their first safety violation.
def Restricted (outcomes objective safe : Ω → Prop) (p : Bool) : Prop :=
  (p = true → ∀ r, outcomes r → safe r) ∧
    ∀ r, outcomes r → safe r → if p then objective r else ¬ objective r

theorem restrict_safety (outcomes objective safe : Ω → Prop)
    (topG : ∀ r, objective r → safe r) (p : Bool) :
    (∀ r, outcomes r → if p then objective r else ¬ objective r) ↔
      Restricted outcomes objective safe p := by
  cases p with
  | false =>
    simp only [Bool.false_eq_true, ↓reduceIte, Restricted, false_implies, true_and]
    constructor
    · intro h r hr _; exact h r hr
    · intro h r hr ho; exact h r hr (topG r ho) ho
  | true =>
    simp only [↓reduceIte, Restricted, forall_const]
    constructor
    · intro h; exact ⟨fun r hr => topG r (h r hr), fun r hr _ => h r hr⟩
    · rintro ⟨hs, h⟩ r hr; exact h r hr (hs r hr)

-- Keep safe system moves, or one ORIGINAL output when none is safe. The G
-- monitor still sees that output. This models C++ completion without new edges.
def safetyCompletion (G : Game V) (safe : V → V → Prop) (fallback : V → V) : Game V where
  owner := G.owner
  prio := G.prio
  edge v w := G.edge v w ∧ (G.owner v = false ∨ safe v w ∨
    (¬ ∃ z, G.edge v z ∧ safe v z) ∧ w = fallback v)

theorem safetyCompletion_empty (G : Game V) (safe : V → V → Prop) (fallback : V → V)
    (legal : ∀ v, G.edge v (fallback v)) (v : V) (system : G.owner v = true)
    (empty : ¬ ∃ z, G.edge v z ∧ safe v z) (w : V) :
    (safetyCompletion G safe fallback).edge v w ↔ w = fallback v := by
  constructor
  · rintro ⟨edge, env | good | ⟨_, same⟩⟩
    · rw [system] at env; cases env
    · exact False.elim (empty ⟨w, edge, good⟩)
    · exact same
  · rintro rfl
    exact ⟨legal v, Or.inr (Or.inr ⟨empty, rfl⟩)⟩

theorem safetyCompletion_total (G : Game V) (safe : V → V → Prop) (fallback : V → V)
    (legal : ∀ v, G.edge v (fallback v)) (v : V) :
    ∃ w, (safetyCompletion G safe fallback).edge v w := by
  classical
  by_cases h : ∃ w, G.edge v w ∧ safe v w
  · obtain ⟨w, edge, good⟩ := h
    exact ⟨w, edge, Or.inr (Or.inl good)⟩
  · exact ⟨fallback v, legal v, Or.inr (Or.inr ⟨h, rfl⟩)⟩

-- A retained unconditional G monitor implies edge safety on every winning play.
-- Thus exclusion is DERIVED from the monitor, also for an empty safe action set.
theorem safetyCompletion_excluded (G : Game V) (safe : V → V → Prop) (fallback : V → V)
    (start : V) (W : (Nat → Nat) → Prop)
    (topG : ∀ π, IsPlay G π → π 0 = start → W (fun k => G.prio (π k)) →
      ∀ k, safe (π k) (π (k + 1)))
    (π : Nat → V) (play : IsPlay G π) (initial : π 0 = start)
    (excluded : ¬ IsPlay (safetyCompletion G safe fallback) π) :
    ¬ W (fun k => G.prio (π k)) := by
  intro win
  exact excluded (fun k => ⟨play k, Or.inr (Or.inl (topG π play initial win k))⟩)

-- Arena-level transfer: deleted system edges lose immediately under the retained G monitor.
-- H may have dead ends. Its system certificate must still provide a move on every
-- reachable system vertex; an environment certificate correctly wins at a dead end.
theorem pruned_certificate (G H : Game V) (W : (Nat → Nat) → Prop) (start : V)
    (owner : H.owner = G.owner) (prio : H.prio = G.prio)
    (subset : ∀ v w, H.edge v w → G.edge v w)
    (environment : ∀ v w, G.owner v = false → G.edge v w → H.edge v w)
    (total : ∀ v, ∃ w, G.edge v w)
    (excluded : ∀ π, IsPlay G π → π 0 = start → ¬ IsPlay H π → ¬ W (fun k => G.prio (π k)))
    {p : Bool} {σ : V → V} (certificate : Certified H W p σ start) :
    ∃ τ, Wins G W p τ start := by
  classical
  let τ v := if Reach H start v then σ v else Classical.choose (total v)
  have legal : Legal G p τ := by
    intro v hv
    dsimp [τ]; split
    · rename_i hr
      exact subset v _ (certificate.1 v hr (by rw [owner]; exact hv))
    · exact Classical.choose_spec (total v)
  refine ⟨τ, legal, fun π hp h0 hc => ?_⟩
  have use (play : IsPlay H π) : Goal W p (fun k => G.prio (π k)) := by
    have reach (k : Nat) : Reach H start (π k) := by
      induction k with
      | zero => rw [h0]; exact .initial
      | succ k ih => exact .step ih (play k)
    have consistent : Consistent H p σ π := by
      intro k hk
      simpa only [τ, ite_eq_left (reach k)] using hc k (by rw [← owner]; exact hk)
    simpa only [prio] using certificate.2 π play h0 consistent
  cases p with
  | false =>
    by_cases play : IsPlay H π
    · exact use play
    · exact excluded π hp h0 play
  | true =>
    have reach (k : Nat) : Reach H start (π k) := by
      induction k with
      | zero => rw [h0]; exact .initial
      | succ k ih =>
        apply Reach.step ih
        cases ho : G.owner (π k) with
        | false => exact environment _ _ ho (hp k)
        | true =>
          have he := certificate.1 (π k) ih (by rw [owner]; exact ho)
          have hs := hc k ho
          simp only [τ, ite_eq_left ih] at hs
          rw [hs]; exact he
    apply use
    intro k
    cases ho : G.owner (π k) with
    | false => exact environment _ _ ho (hp k)
    | true =>
      have he := certificate.1 (π k) (reach k) (by rw [owner]; exact ho)
      have hs := hc k ho
      simp only [τ, ite_eq_left (reach k)] at hs
      rw [hs]; exact he

variable {Q L GI GO LI LO : Type} {marks monitors events n : Nat}
variable {E : Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := LI) (LO := LO)
  (marks := marks) (monitors := monitors) (events := events)}

-- Translation and membership of the selected G clause force its accepted weight
-- on every winning original run; this is the safety obligation used by pruning.
theorem translated_cardinality_G (spec : List (Clause E)) (q0 : Q)
    (translated : Translates E q0 spec) (k : Fin events) (kind : E.kind k = .cardinality)
    (selected : Clause.temporal (fun _ atoms => CountTemporal.G (atoms ⟨k, .cardinality, kind⟩) 0) ∈ spec)
    (R : Run E n) (hq : R.q 0 = q0) (hc : R.color 0 = 0)
    (win : maxOdd (fun t => (game (engineMachine E)).prio (R.vertices t))) :
    ∀ t, E.accepted k (counts (fun i => current E (R.states t i) (R.global t) (R.letter i t) k) true) = true := by
  have source := (R.specification spec q0 translated hq hc).mpr win _ selected
  have encoded := (Clause.correct _ R).mp source
  intro t
  exact (R.cardinality_event k kind t).mp (encoded t (Nat.zero_le _))

-- The top-level G guard is a property of the original objective, never of an assumption,
-- F, X or a disjunct. Outputs-only ensures that deleting its violations restricts system moves.
theorem engine_safety_restriction (spec : List (Clause E)) (q0 : Q)
    (safe : Run E n → Prop) (topG : ∀ R, (∀ C ∈ spec, C.source R) → safe R)
    (p : Bool) (σ : Vertex n Q (Local L monitors events) GI LI → Vertex n Q (Local L monitors events) GI LI) :
    SpecWins spec q0 p σ ↔ Legal (game (engineMachine E)) p σ ∧
      Restricted (fun R : Run E n => R.q 0 = q0 ∧ R.color 0 = 0 ∧
        Consistent (game (engineMachine E)) p σ R.vertices)
        (fun R => ∀ C ∈ spec, C.source R) safe p := by
  unfold SpecWins
  rw [← restrict_safety _ _ _ topG p]
  simp only [and_imp]

-- Cardinality atoms participate in Clause.correct. Transfer a certificate from the
-- filtered count arena, then invoke the existing engine specification theorem.
theorem engine_pruned_verdict
    (E : Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := Fin ki) (LO := Fin ko)
      (marks := marks) (monitors := monitors) (events := events))
    (spec : List (Clause E)) (q0 : Q) (translated : Translates E q0 spec)
    (absorbing : Q → Option Nat)
    (valid : ∀ q c, absorbing q = some c → Absorbing (engineMachine E) q c)
    (a : GI) (b : GO) (u : Fin ki) (v : Fin ko) :
    let G := countGame (engineMachine E) absorbing (CountEngine.initial E) (n := n)
    let start := countKey (collapse absorbing (CountEngine.initial E) (.env q0 0 (fun _ => CountEngine.initial E)))
    ∀ (safe : CountKey n Q (Local L monitors events) GI (Fin ki) →
          CountKey n Q (Local L monitors events) GI (Fin ki) → Prop) (fallback),
      (∀ x, G.edge x (fallback x)) →
      (∀ π, IsPlay G π → π 0 = start → maxOdd (fun k => G.prio (π k)) →
        ∀ k, safe (π k) (π (k + 1))) →
      ∀ (p : Bool) (σ), Certified (safetyCompletion G safe fallback) maxOdd p σ start →
        (∃ strategy, SpecWins (n := n) spec q0 p strategy) ∧
          ∀ other, ¬ SpecWins (n := n) spec q0 (!p) other := by
  intro G start safe fallback legal topG p σ certificate
  obtain ⟨τ, win⟩ := pruned_certificate G (safetyCompletion G safe fallback) maxOdd start
    rfl rfl (fun _ _ edge => edge.1) (fun _ _ env edge => ⟨edge, Or.inl env⟩)
    (countGame_total (engineMachine E) absorbing (CountEngine.initial E) a b u v)
    (safetyCompletion_excluded G safe fallback start maxOdd topG) certificate
  exact engine_spec_verdict E spec q0 translated absorbing valid a b u v
    (σ := τ) ⟨fun x _ hx => win.1 x hx, win.2⟩

end Symmetry.CountCardinality

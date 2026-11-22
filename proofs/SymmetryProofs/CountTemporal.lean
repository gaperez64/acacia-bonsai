import Init

namespace Symmetry.CountTemporal

abbrev Trace := Nat → Prop

def G (p : Trace) (t : Nat) : Prop := ∀ u, t ≤ u → p u
def F (p : Trace) (t : Nat) : Prop := ∃ u, t ≤ u ∧ p u
def GF (p : Trace) (t : Nat) : Prop := ∀ u, t ≤ u → ∃ v, u ≤ v ∧ p v
def FG (p : Trace) (t : Nat) : Prop := ∃ u, t ≤ u ∧ G p u

theorem gf_suffix (p : Trace) (t : Nat) : GF p t ↔ GF p 0 := by
  constructor
  · intro h u _; obtain ⟨v, hv, hp⟩ := h (max t u) (Nat.le_max_left ..)
    exact ⟨v, Nat.le_trans (Nat.le_max_right ..) hv, hp⟩
  · intro h u _; exact h u (Nat.zero_le _)

theorem fg_suffix (p : Trace) (t : Nat) : FG p t ↔ FG p 0 := by
  constructor
  · rintro ⟨u, _, h⟩; exact ⟨u, Nat.zero_le _, h⟩
  · rintro ⟨u, _, h⟩
    exact ⟨max t u, Nat.le_max_left .., fun v hv => h v (Nat.le_trans (Nat.le_max_right ..) hv)⟩

theorem not_gf (p : Trace) (t : Nat) : ¬ GF p t ↔ FG (fun u => ¬ p u) t := by
  classical
  simp [GF, FG, G, Classical.not_forall, Classical.not_imp]

-- Finite clients have a common eventual bound; symmetric_count.cc:165–168.
theorem fg_all (p : Fin n → Trace) (t : Nat) :
    (∀ i, FG (p i) t) ↔ FG (fun u => ∀ i, p i u) t := by
  induction n with
  | zero => simp [FG, G]; exact ⟨t, Nat.le_refl _⟩
  | succ n ih =>
    constructor
    · intro h
      obtain ⟨u, hu, h0⟩ := h 0
      obtain ⟨v, hv, hs⟩ := (ih (fun i => p i.succ)).mp (fun i => h i.succ)
      refine ⟨max u v, Nat.le_trans hu (Nat.le_max_left ..), fun w hw i => ?_⟩
      exact Fin.cases (h0 w (Nat.le_trans (Nat.le_max_left ..) hw))
        (fun j => hs w (Nat.le_trans (Nat.le_max_right ..) hw) j) i
    · rintro ⟨u, hu, h⟩ i; exact ⟨u, hu, fun v hv => h v hv i⟩

-- The any event is the disjunctive GF aggregate; symmetric_count.cc:159–162,647–648.
theorem gf_any (p : Fin n → Trace) (t : Nat) :
    GF (fun u => ∃ i, p i u) t ↔ ∃ i, GF (p i) t := by
  classical
  constructor
  · intro h
    apply Classical.byContradiction
    intro hn
    have hs : ∀ i, FG (fun u => ¬ p i u) t := fun i => (not_gf _ _).mp (fun hi => hn ⟨i, hi⟩)
    obtain ⟨u, hu, hb⟩ := (fg_all _ _).mp hs
    obtain ⟨v, hv, i, hi⟩ := h u hu
    exact hb v hv i hi
  · rintro ⟨i, h⟩ u hu
    obtain ⟨v, hv, hp⟩ := h u hu; exact ⟨v, hv, i, hp⟩

-- Set on this transition, test all, then reset; symmetric_count.cc:637–660.
variable {n : Nat} {p : Fin n → Trace}

structure Breakpoint (p : Fin n → Trace) where
  bit : Nat → Fin n → Prop
  event : Trace
  fire : ∀ t, event t ↔ ∀ i, bit t i ∨ p i t
  update : ∀ t i, bit (t + 1) i ↔ (bit t i ∨ p i t) ∧ ¬ event t

noncomputable def bits (p : Fin n → Trace) (initial : Fin n → Prop) : Nat → Fin n → Prop
  | 0 => initial
  | t + 1 => fun i => (bits p initial t i ∨ p i t) ∧ ¬ ∀ j, bits p initial t j ∨ p j t

noncomputable def breakpoint (p : Fin n → Trace) (initial : Fin n → Prop) : Breakpoint p where
  bit := bits p initial
  event t := ∀ i, bits p initial t i ∨ p i t
  fire _ := Iff.rfl
  update _ _ := Iff.rfl

private theorem persist (B : Breakpoint p) (a b : Nat) (i : Fin n)
    (hab : a ≤ b) (hi : B.bit a i) (hn : ∀ u, a ≤ u → u < b → ¬ B.event u) : B.bit b i := by
  induction b with
  | zero =>
    have : a = 0 := by omega
    subst a; exact hi
  | succ b ih =>
    by_cases he : a = b + 1
    · simpa [he] using hi
    · apply (B.update b i).mpr
      exact ⟨Or.inl (ih (by omega) (fun u hu hv => hn u hu (by omega))), hn b (by omega) (by omega)⟩

private theorem witness (B : Breakpoint p) (a b : Nat) (i : Fin n)
    (hab : a ≤ b) (hi : ¬ B.bit a i) (hb : B.bit b i ∨ p i b) :
    ∃ u, a ≤ u ∧ u ≤ b ∧ p i u := by
  induction b with
  | zero =>
    have : a = 0 := by omega
    subst a; exact ⟨0, by omega, by omega, hb.resolve_left hi⟩
  | succ b ih =>
    rcases hb with hb | hp
    · by_cases he : a = b + 1
      · exact False.elim (hi (he ▸ hb))
      · obtain ⟨u, hu, hv, hp⟩ := ih (by omega) ((B.update b i).mp hb).1
        exact ⟨u, hu, by omega, hp⟩
    · exact ⟨b + 1, hab, Nat.le_refl _, hp⟩

-- All-client recurrence equals breakpoint recurrence at EVERY suffix, with arbitrary prior bits; symmetric_count.cc:159–162,637–660.
theorem breakpoint_gf (B : Breakpoint p) (t : Nat) :
    GF B.event t ↔ ∀ i, GF (p i) t := by
  classical
  constructor
  · intro h i u hu
    obtain ⟨a, ha, he⟩ := h u hu
    have hi : ¬ B.bit (a + 1) i := fun hh => ((B.update a i).mp hh).2 he
    obtain ⟨b, hb, hf⟩ := h (a + 1) (by omega)
    obtain ⟨v, hv, _, hp⟩ := witness B (a + 1) b i hb hi ((B.fire b).mp hf i)
    exact ⟨v, by omega, hp⟩
  · intro h u hu
    apply Classical.byContradiction
    intro hn
    have ne : ∀ v, u ≤ v → ¬ B.event v := by simpa only [not_exists, not_and] using hn
    have each : ∀ i, FG (fun v => B.bit v i) u := by
      intro i
      obtain ⟨a, ha, hp⟩ := h i u hu
      have hb : B.bit (a + 1) i := (B.update a i).mpr ⟨Or.inr hp, ne a ha⟩
      exact ⟨a + 1, by omega, fun b hab => persist B (a + 1) b i hab hb
        (fun v hv _ => ne v (by omega))⟩
    obtain ⟨a, ha, hb⟩ := (fg_all _ _).mp each
    exact ne a ha ((B.fire a).mpr (fun i => Or.inl (hb a (Nat.le_refl _) i)))

-- Disjunctive FG is the negated breakpoint on negated letters; symmetric_count.cc:165–168.
theorem fg_any (p : Fin n → Trace) (B : Breakpoint (fun i t => ¬ p i t)) (t : Nat) :
    (∃ i, FG (p i) t) ↔ ¬ GF B.event t := by
  classical
  rw [breakpoint_gf]
  simp only [Classical.not_forall, not_gf, Classical.not_not]

-- These are the four formulas emitted by atom(), at every position and with prior breakpoint history; symmetric_count.cc:155–168.
theorem aggregate_encodings (p : Fin n → Trace) (B : Breakpoint p)
    (N : Breakpoint (fun i t => ¬ p i t)) (t : Nat) :
    ((∀ i, GF (p i) t) ↔ GF B.event t) ∧
    ((∃ i, GF (p i) t) ↔ GF (fun u => ∃ i, p i u) t) ∧
    ((∀ i, FG (p i) t) ↔ FG (fun u => ¬ ∃ i, ¬ p i u) t) ∧
    ((∃ i, FG (p i) t) ↔ FG (fun u => ¬ N.event u) t) := by
  classical
  exact ⟨(breakpoint_gf B t).symm, (gf_any p t).symm,
    by simpa only [not_exists, Classical.not_not] using fg_all p t,
    (fg_any p N t).trans (not_gf N.event t)⟩

-- Pointwise equality permits substitution in every context, including X and negative polarity; symmetric_count.cc:192–219.
theorem context (C : Trace → Trace) (p q : Trace) (h : ∀ t, p t ↔ q t) : C p = C q := by
  have : p = q := funext (fun t => propext (h t)); exact congrArg C this

-- The letter events are pointwise, without any temporal assumption; symmetric_count.cc:155–157,647–650.
theorem letter_events (p : Fin n → Trace) (t : Nat) :
    ((∀ i, p i t) ↔ (fun u => ∀ i, p i u) t) ∧
    ((∃ i, p i t) ↔ (fun u => ∃ i, p i u) t) := ⟨Iff.rfl, Iff.rfl⟩

-- Local Inf marks use all_inf; local Fin marks use any; symmetric_count.cc:364–404.
theorem local_acceptance (inf : Fin a → Fin n → Trace) (fin : Fin b → Fin n → Trace)
    (bp : ∀ j, Breakpoint (inf j)) (sink : Fin n → Trace) (t : Nat) :
    ((∀ i, (∀ j, GF (inf j i) t) ∧ (∀ j, FG (fun u => ¬ fin j i u) t)) ∧
      FG (fun u => ¬ ∃ i, sink i u) t) ↔
    ((∀ j, GF (bp j).event t) ∧ (∀ j, FG (fun u => ¬ ∃ i, fin j i u) t) ∧
      FG (fun u => ¬ ∃ i, sink i u) t) := by
  classical
  simp only [breakpoint_gf, not_exists]
  constructor
  · rintro ⟨h, hs⟩
    exact ⟨fun j i => (h i).1 j, fun j => (fg_all _ _).mp (fun i => (h i).2 j), hs⟩
  · rintro ⟨hi, hf, hs⟩
    exact ⟨fun i => ⟨fun j => hi j i, fun j => (fg_all _ _).mpr (hf j) i⟩, hs⟩

-- Absorbing missing-transition sink: finitely many sink events means no missing transition; symmetric_count.cc:399–404,416–429.
theorem rejecting_sink (sink : Trace) (absorbs : ∀ t, sink t → sink (t + 1)) (t : Nat) :
    FG (fun u => ¬ sink u) t ↔ ∀ u, ¬ sink u := by
  constructor
  · rintro ⟨a, _, ha⟩ u hu
    have persistSink : ∀ k, sink (u + k) := by
      intro k; induction k with
      | zero => exact hu
      | succ k ih => exact absorbs (u + k) ih
    exact ha (u + a) (by omega) (persistSink a)
  · intro h; exact ⟨t, Nat.le_refl _, fun u _ => h u⟩

-- Monitor bit AFTER the current step, with m(-1)=false; symmetric_count.cc:108,629–633.
def monitor (gamma lambda : Trace) : Trace
  | 0 => gamma 0 ∧ ¬ lambda 0
  | t + 1 => gamma (t + 1) ∧ (monitor gamma lambda t ∨ ¬ lambda (t + 1))

private theorem monitor_iff (gamma lambda : Trace) (t : Nat) :
    monitor gamma lambda t ↔ ∃ u, u ≤ t ∧ ¬ lambda u ∧
      ∀ v, u ≤ v → v ≤ t → gamma v := by
  induction t with
  | zero =>
    constructor
    · rintro ⟨hg, hl⟩; exact ⟨0, Nat.le_refl _, hl, fun v _ hv => by
        have : v = 0 := by omega
        simpa [this]⟩
    · rintro ⟨u, hu, hl, hg⟩; have : u = 0 := by omega
      subst u; exact ⟨hg 0 (Nat.le_refl _) (Nat.le_refl _), hl⟩
  | succ t ih =>
    constructor
    · rintro ⟨hg, hm | hl⟩
      · obtain ⟨u, hu, hl, hs⟩ := ih.mp hm
        exact ⟨u, by omega, hl, fun v huv hv => by
          by_cases he : v = t + 1
          · simpa [he] using hg
          · exact hs v huv (by omega)⟩
      · exact ⟨t + 1, Nat.le_refl _, hl, fun v hv hv' => by
          have : v = t + 1 := by omega
          simpa [this]⟩
    · rintro ⟨u, hu, hl, hg⟩
      refine ⟨hg (t + 1) hu (Nat.le_refl _), ?_⟩
      by_cases he : u = t + 1
      · exact Or.inr (he ▸ hl)
      · exact Or.inl (ih.mpr ⟨u, by omega, hl, fun v hv hv' => hg v hv (by omega)⟩)

private theorem stable_start (gamma : Trace) (h : FG gamma 0) :
    ∃ a, G gamma a ∧ (a = 0 ∨ ∃ b, a = b + 1 ∧ ¬ gamma b) := by
  classical
  obtain ⟨a, _, ha⟩ := h
  induction a with
  | zero => exact ⟨0, ha, Or.inl rfl⟩
  | succ a ih =>
    by_cases hg : gamma a
    · apply ih (Nat.zero_le _)
      intro v hv; by_cases he : v = a
      · simpa [he] using hg
      · exact ha v (by omega)
    · exact ⟨a + 1, ha, Or.inr ⟨a, rfl, hg⟩⟩

-- If gamma is stable, eventual false monitors prohibit EVERY later lambda failure; symmetric_count.cc:322–333.
theorem monitor_no_failure (gamma lambda : Trace) (a : Nat) (hg : G gamma a)
    (hm : FG (fun t => ¬ monitor gamma lambda t) 0) : G lambda a := by
  classical
  obtain ⟨b, _, hb⟩ := hm
  intro u hu
  apply Classical.byContradiction
  intro hl
  exact hb (max b u) (Nat.le_max_left ..)
    ((monitor_iff _ _ _).mpr ⟨u, Nat.le_max_right .., hl,
      fun v hv _ => hg v (by omega)⟩)

-- At the first permanently-gamma suffix, the converse also holds; symmetric_count.cc:322–333.
theorem monitor_stable (gamma lambda : Trace) (a : Nat) (hg : G gamma a)
    (start : a = 0 ∨ ∃ b, a = b + 1 ∧ ¬ gamma b) :
    FG (fun t => ¬ monitor gamma lambda t) 0 ↔ G lambda a := by
  constructor
  · exact monitor_no_failure gamma lambda a hg
  · intro hl
    refine ⟨a, Nat.zero_le _, fun t ht hm => ?_⟩
    obtain ⟨u, hu, hnl, hs⟩ := (monitor_iff _ _ _).mp hm
    by_cases hua : a ≤ u
    · exact hnl (hl u hua)
    · rcases start with rfl | ⟨b, rfl, hb⟩
      · omega
      · exact hb (hs b (by omega) (by omega))

def sinceSpec (gamma : Trace) (lambda : Fin n → Trace) : Trace :=
  G (fun t => G gamma t → ∃ i, G (lambda i) t)

-- Position 0 and initially false monitors are essential; symmetric_count.cc:108,322–333.
theorem since_zero (gamma : Trace) (lambda : Fin n → Trace) :
    sinceSpec gamma lambda 0 ↔
      GF (fun t => ¬ gamma t) 0 ∨ ∃ i, FG (fun t => ¬ monitor gamma (lambda i) t) 0 := by
  classical
  have vacuous (h : GF (fun t => ¬ gamma t) 0) : sinceSpec gamma lambda 0 := by
    intro u _ hg
    obtain ⟨v, hv, hn⟩ := h u (Nat.zero_le _)
    exact False.elim (hn (hg v hv))
  constructor
  · intro hs
    by_cases hi : GF (fun t => ¬ gamma t) 0
    · exact Or.inl hi
    · have he : FG gamma 0 := by simpa only [Classical.not_not] using (not_gf _ _).mp hi
      obtain ⟨a, ha, hstart⟩ := stable_start gamma he
      obtain ⟨i, hl⟩ := hs a (Nat.zero_le _) ha
      exact Or.inr ⟨i, (monitor_stable gamma (lambda i) a ha hstart).mpr hl⟩
  · rintro (h | ⟨i, hm⟩)
    · exact vacuous h
    · intro u _ hg; exact ⟨i, monitor_no_failure gamma (lambda i) u hg hm⟩

-- The exact breakpoint formula emitted for since; symmetric_count.cc:329–333.
theorem since_encoded (gamma : Trace) (lambda : Fin n → Trace)
    (B : Breakpoint (fun i => monitor gamma (lambda i))) :
    sinceSpec gamma lambda 0 ↔ GF (fun t => ¬ gamma t) 0 ∨ FG (fun t => ¬ B.event t) 0 := by
  rw [← not_gf, since_zero, breakpoint_gf]
  simp only [Classical.not_forall, not_gf]

-- At position 1 the old lambda failure remains in the running monitor; symmetric_count.cc:322.
theorem since_later_counterexample :
    sinceSpec (fun _ => True) (fun _ : Fin 2 => fun t => 0 < t) 1 ∧
    ¬ (GF (fun _ => ¬ True) 1 ∨
      ∃ _i : Fin 2, FG (fun t => ¬ monitor (fun _ => True) (fun t => 0 < t) t) 1) := by
  have hm (t : Nat) : monitor (fun _ => True) (fun t => 0 < t) t :=
    (monitor_iff _ _ _).mpr ⟨0, Nat.zero_le _, by omega, fun _ _ _ => True.intro⟩
  refine ⟨fun u hu _ => ⟨0, fun v hv => by omega⟩, ?_⟩
  rintro (h | ⟨_, a, _, ha⟩)
  · obtain ⟨_, _, hn⟩ := h 1 (Nat.le_refl _); exact hn True.intro
  · exact ha a (Nat.le_refl _) (hm a)

-- Flatten only conjunctive consequents and positive outer G; symmetric_count.cc:67–97.
theorem flatten (p q r : Trace) (t : Nat) :
    (G (fun u => p u ∧ q u) t ↔ G p t ∧ G q t) ∧
    (G (fun u => p u → q u ∧ r u) t ↔ G (fun u => p u → q u) t ∧ G (fun u => p u → r u) t) ∧
    ((p t → q t ∧ r t) ↔ (p t → q t) ∧ (p t → r t)) ∧
    (G (G p) t ↔ G p t) := by
  refine ⟨⟨fun h => ⟨fun u hu => (h u hu).1, fun u hu => (h u hu).2⟩,
    fun h u hu => ⟨h.1 u hu, h.2 u hu⟩⟩, ?_, ?_, ?_⟩
  · exact ⟨fun h => ⟨fun u hu hp => (h u hu hp).1, fun u hu hp => (h u hu hp).2⟩,
      fun h u hu hp => ⟨h.1 u hu hp, h.2 u hu hp⟩⟩
  · exact ⟨fun h => ⟨fun hp => (h hp).1, fun hp => (h hp).2⟩, fun h hp => ⟨h.1 hp, h.2 hp⟩⟩
  · exact ⟨fun h u hu => h u hu u (Nat.le_refl _), fun h u hu v hv => h v (Nat.le_trans hu hv)⟩


-- One deterministic local automaton, instantiated at every index; symmetric_count.cc:386–429.
def localRun (step : L → A → B → Option (L × (Fin marks → Bool)))
    (initial : L) (global : Nat → A) (letter : Nat → B) : Nat → Option L
  | 0 => some initial
  | t + 1 => (localRun step initial global letter t).bind (fun q => (step q (global t) (letter t)).map Prod.fst)

def localMark (step : L → A → B → Option (L × (Fin marks → Bool)))
    (initial : L) (global : Nat → A) (letter : Nat → B) (j : Fin marks) (t : Nat) : Prop :=
  ∃ q q' m, localRun step initial global letter t = some q ∧ step q (global t) (letter t) = some (q', m) ∧ m j = true

def localAccepts (step : L → A → B → Option (L × (Fin marks → Bool)))
    (initial : L) (global : Nat → A) (letter : Nat → B) (inf : Fin a → Fin marks) (fin : Fin b → Fin marks) : Prop :=
  (∀ t, localRun step initial global letter t ≠ none) ∧
  (∀ j, GF (localMark step initial global letter (inf j)) 0) ∧
  (∀ j, FG (fun t => ¬ localMark step initial global letter (fin j) t) 0)

-- A missing transition enters a permanent sink, even if its marks were Fin-only; symmetric_count.cc:399–404,416–429.
theorem deterministic_local_events (step : L → A → B → Option (L × (Fin marks → Bool)))
    (initial : L) (global : Nat → A) (letter : Fin n → Nat → B)
    (inf : Fin a → Fin marks) (fin : Fin b → Fin marks)
    (bp : ∀ j, Breakpoint (fun i => localMark step initial global (letter i) (inf j))) :
    (∀ i, localAccepts step initial global (letter i) inf fin) ↔
    (∀ j, GF (bp j).event 0) ∧
      (∀ j, FG (fun t => ¬ ∃ i, localMark step initial global (letter i) (fin j) t) 0) ∧
      FG (fun t => ¬ ∃ i, localRun step initial global (letter i) (t + 1) = none) 0 := by
  classical
  let sink : Trace := fun t => ∃ i, localRun step initial global (letter i) (t + 1) = none
  have absorbs : ∀ t, sink t → sink (t + 1) := by
    rintro t ⟨i, hi⟩
    refine ⟨i, ?_⟩
    change (localRun step initial global (letter i) (t + 1)).bind _ = none
    rw [hi]; rfl
  have valid : FG (fun t => ¬ sink t) 0 ↔ ∀ i t, localRun step initial global (letter i) t ≠ none := by
    rw [rejecting_sink sink absorbs 0]
    constructor
    · intro h i t; cases t with
      | zero => simp [localRun]
      | succ t => exact fun hi => h t ⟨i, hi⟩
    · intro h t ⟨i, hi⟩; exact h i (t + 1) hi
  have he := local_acceptance (fun j i => localMark step initial global (letter i) (inf j))
    (fun j i => localMark step initial global (letter i) (fin j)) bp
    (fun i t => localRun step initial global (letter i) (t + 1) = none) 0
  change (∀ i, localAccepts step initial global (letter i) inf fin) ↔ _
  rw [← he]
  change (∀ i, localAccepts step initial global (letter i) inf fin) ↔ _ ∧ FG (fun t => ¬ sink t) 0
  rw [valid]
  constructor
  · intro h; exact ⟨fun i => ⟨(h i).2.1, (h i).2.2⟩, fun i => (h i).1⟩
  · rintro ⟨h, hv⟩ i; exact ⟨hv i, (h i).1, (h i).2⟩

-- This small syntax records precisely the parser positions that flatten traverses; symmetric_count.cc:67–97.
inductive Formula where
  | top
  | atom (p : Trace)
  | conj (p q : Formula)
  | imp (p q : Formula)
  | always (p : Formula)
  | neg (p : Formula)
  | next (p : Formula)

def Formula.eval : Formula → Trace
  | .top => fun _ => True
  | .atom p => p
  | .conj p q => fun t => p.eval t ∧ q.eval t
  | .imp p q => fun t => p.eval t → q.eval t
  | .always p => G p.eval
  | .neg p => fun t => ¬ p.eval t
  | .next p => fun t => p.eval (t + 1)

-- No rule traverses X, negation, or an implication antecedent; symmetric_count.cc:67–97,310–333.
inductive Flatten : Formula → List Formula → Prop where
  | leaf (p : Formula) : Flatten p [p]
  | top : Flatten .top []
  | conj {p q : Formula} {ps qs : List Formula} : Flatten p ps → Flatten q qs → Flatten (.conj p q) (ps ++ qs)
  | always_conj {p q : Formula} {ps qs : List Formula} : Flatten (.always p) ps → Flatten (.always q) qs → Flatten (.always (.conj p q)) (ps ++ qs)
  | always_imp {p q r : Formula} {qs rs : List Formula} : Flatten (.always (.imp p q)) qs → Flatten (.always (.imp p r)) rs →
      Flatten (.always (.imp p (.conj q r))) (qs ++ rs)
  | imp {p q r : Formula} {qs rs : List Formula} : Flatten (.imp p q) qs → Flatten (.imp p r) rs → Flatten (.imp p (.conj q r)) (qs ++ rs)
  | always_always {p : Formula} {ps : List Formula} : Flatten (.always p) ps → Flatten (.always (.always p)) ps

-- Flattened leaves are positive conjuncts AT THE ORIGINAL POSITION; symmetric_count.cc:67–97,310–333.
theorem flatten_correct {p : Formula} {ps : List Formula} (h : Flatten p ps) (t : Nat) : p.eval t ↔ ∀ q ∈ ps, q.eval t := by
  induction h with
  | leaf => simp
  | top => simp [Formula.eval]
  | conj hp hq ihp ihq =>
    simp only [Formula.eval, ihp, ihq, List.mem_append, or_imp, forall_and]
  | always_conj hp hq ihp ihq =>
    simp only [Formula.eval] at ihp ihq
    change G (fun u => _ ∧ _) t ↔ _
    rw [(flatten _ _ (fun _ => True) t).1, ihp, ihq]
    simp only [List.mem_append, or_imp, forall_and]
  | always_imp hq hr ihq ihr =>
    simp only [Formula.eval] at ihq ihr
    change G (fun u => _ → _ ∧ _) t ↔ _
    rw [(flatten _ _ _ t).2.1, ihq, ihr]
    simp only [List.mem_append, or_imp, forall_and]
  | imp hq hr ihq ihr =>
    simp only [Formula.eval] at ihq ihr
    change (_ → _ ∧ _) ↔ _
    rw [(flatten _ _ _ t).2.2.1, ihq, ihr]
    simp only [List.mem_append, or_imp, forall_and]
  | always_always hp ih =>
    simp only [Formula.eval] at ih
    change G (G _) t ↔ _
    rw [(flatten _ (fun _ => True) (fun _ => True) t).2.2.2, ih]

-- A position-0 equivalence, such as since_zero, may replace exposed leaves only; symmetric_count.cc:310–333.
theorem top_conjunct_replacement {p : Formula} {ps : List Formula} (h : Flatten p ps) (replacement : Formula → Trace)
    (equiv : ∀ q ∈ ps, q.eval 0 ↔ replacement q 0) :
    p.eval 0 ↔ ∀ q ∈ ps, replacement q 0 := by
  rw [flatten_correct h 0]
  exact ⟨fun hp q hq => (equiv q hq).mp (hp q hq), fun hp q hq => (equiv q hq).mpr (hp q hq)⟩

end Symmetry.CountTemporal

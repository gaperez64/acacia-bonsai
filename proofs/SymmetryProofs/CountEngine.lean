import SymmetryProofs.CountGame

namespace Symmetry.CountEngine

open CountGame CountTemporal Symmetry.Quotient

inductive Kind where
  | any | allNow | allInf | cardinality
  deriving DecidableEq

inductive Predicate (marks monitors : Nat) (G X : Type) where
  | mark (j : Fin marks)
  | sink
  | letter (test : G → X → Bool)
  | since (j : Fin monitors)

structure Local (L : Type) (monitors events : Nat) where
  state : Option L
  since : Fin monitors → Bool
  bit : Fin events → Bool

-- Tables at symmetric_count.cc:415–465,468–484,506–519 (6c6da84a).
structure Tables (Q L G X : Type) (marks monitors events : Nat) where
  step : L → G → X → Option (L × (Fin marks → Bool))
  initial : L
  gamma : Fin monitors → G → Bool
  lambda : Fin monitors → G → X → Bool
  pred : Fin events → Predicate marks monitors G X
  kind : Fin events → Kind
  globalStep : Q → G → (Fin events → Bool) → Q × Nat
  accepted : Fin events → Nat → Bool := fun _ _ => false

variable {Q L GI GO LI LO : Type} {marks monitors events n : Nat}
abbrev Engine := Tables Q L (GI × GO) (LI × LO) marks monitors events

variable (E : Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := LI) (LO := LO)
  (marks := marks) (monitors := monitors) (events := events))

def initial : Local L monitors events :=
  ⟨some E.initial, fun _ => false, fun _ => false⟩

def result (s : Local L monitors events) (g : GI × GO) (x : LI × LO) := s.state.bind (fun q => E.step q g x)
def sinceNext (s : Local L monitors events) (g : GI × GO) (x : LI × LO) (j : Fin monitors) : Bool :=
  E.gamma j g && (s.since j || !(E.lambda j g x))

def current (s : Local L monitors events) (g : GI × GO) (x : LI × LO) (k : Fin events) : Bool :=
  match E.pred k with
  | .mark j => ((result E s g x).map (fun z => z.2 j)).getD false
  | .sink => (result E s g x).isNone
  | .letter p => p g x
  | .since j => sinceNext E s g x j

def ready (s : Local L monitors events) (g : GI × GO) (x : LI × LO) (k : Fin events) : Bool :=
  s.bit k || current E s g x k

def advance (s : Local L monitors events) (g : GI × GO) (x : LI × LO) : Local L monitors events :=
  ⟨(result E s g x).map Prod.fst, sinceNext E s g x,
    fun k => if E.kind k = .allInf then ready E s g x k else s.bit k⟩

def reset (ev : Nat → Bool) (s : Local L monitors events) : Local L monitors events :=
  {s with bit := fun k => if E.kind k = .allInf then s.bit k && !(ev k) else s.bit k}

-- A joint mask carries current predicates and the ready bits separately.
def mask (s : Local L monitors events) (g : GI × GO) (x : LI × LO) (j : Nat) : Bool :=
  if h : j < events then current E s g x ⟨j, h⟩
  else if h : j - events < events then ready E s g x ⟨j - events, h⟩ else false

-- Finite support for the joint current/ready masks used by the generic count machine.
def maskWord (v : Fin (events + events) → Bool) : Nat → Bool :=
  fun j => if h : j < events + events then v ⟨j, h⟩ else false

noncomputable def maskDomain (events : Nat) : List (Nat → Bool) := by
  classical exact unique ((vectors [false, true] (events + events)).map maskWord)

theorem mask_mem (s : Local L monitors events) (g : GI × GO) (x : LI × LO) :
    mask E s g x ∈ maskDomain events := by
  classical
  have he : maskWord (fun j : Fin (events + events) => mask E s g x j) = mask E s g x := by
    funext j
    by_cases h : j < events + events
    · simp [maskWord, h]
    · simp [maskWord, h, mask, show ¬ j < events by omega,
        show ¬ j - events < events by omega]
  simp only [maskDomain, mem_unique, List.mem_map]
  exact ⟨_, mem_vectors _ (by intro b; cases b <;> simp) _, he⟩

noncomputable def event (h : (Nat → Bool) → Nat) (j : Nat) : Bool := by
  classical
  exact if hj : j < events then
    match E.kind ⟨j, hj⟩ with
    | .any => decide (∃ m, 0 < h m ∧ m j = true)
    | .allNow => decide (∀ m, 0 < h m → m j = true)
    | .allInf => decide (∀ m, 0 < h m → m (events + j) = true)
    | .cardinality => E.accepted ⟨j, hj⟩ (pushCounts (maskDomain events) h (fun m => m j) true)
  else false

noncomputable def engineMachine : Machine Q (Local L monitors events) GI GO LI LO where
  localStep s a b u v := advance E s (a, b) (u, v)
  holds s a b u v := mask E s (a, b) (u, v)
  events := event E
  globalStep q a b ev := E.globalStep q (a, b) (fun k => ev k)
  reset := reset E

variable {E}

@[simp] theorem mask_current (s : Local L monitors events) (g : GI × GO) (x : LI × LO) (k : Fin events) :
    mask E s g x k = current E s g x k := by simp [mask, k.isLt]
@[simp] theorem mask_ready (s : Local L monitors events) (g : GI × GO) (x : LI × LO) (k : Fin events) :
    mask E s g x (events + k) = ready E s g x k := by
  simp [mask, show ¬ events + k.val < events by omega]

-- The event fed to the global table is the occupied-cell any/all test, including OLD ready bits.
theorem event_values (s : Fin n → Local L monitors events) (g : GI × GO) (x : Fin n → LI × LO) (k : Fin events) :
    event E (counts (fun i => mask E (s i) g (x i))) k = true ↔
      match E.kind k with
      | .any => ∃ i, current E (s i) g (x i) k = true
      | .allNow => ∀ i, current E (s i) g (x i) k = true
      | .allInf => ∀ i, (s i).bit k = true ∨ current E (s i) g (x i) k = true
      | .cardinality => E.accepted k (counts (fun i => current E (s i) g (x i) k) true) = true := by
  classical
  have now := occupied_events (fun i => mask E (s i) g (x i)) (fun m => m k = true)
  have later := occupied_events (fun i => mask E (s i) g (x i)) (fun m => m (events + k) = true)
  simp only [mask_current] at now
  simp only [mask_ready, ready, Bool.or_eq_true] at later
  cases hk : E.kind k with
  | any => simpa [event, k.isLt, hk] using now.1
  | allNow => simpa [event, k.isLt, hk] using now.2
  | allInf => simpa [event, k.isLt, hk] using later.2
  | cardinality =>
    have hw := push_counts (maskDomain events) (fun i => mask E (s i) g (x i))
      (unique_nodup _) (fun i => mask_mem E (s i) g (x i)) (fun m => m k)
    simp [event, k.isLt, hk, hw]


-- A round is exactly one environment input followed by one system output.
structure Run (E : Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := LI) (LO := LO)
    (marks := marks) (monitors := monitors) (events := events)) (n : Nat) where
  q : Nat → Q
  color : Nat → Nat
  states : Nat → Fin n → Local L monitors events
  a : Nat → GI
  b : Nat → GO
  u : Nat → Fin n → LI
  v : Nat → Fin n → LO
  start : ∀ i, states 0 i = initial E
  step : ∀ t, transition (engineMachine E) (q t) (states t) (a t) (b t) (u t) (v t) =
    (q (t + 1), color (t + 1), states (t + 1))

namespace Run
variable (R : Run E n)
def global (t : Nat) := (R.a t, R.b t)
def letter (i : Fin n) (t : Nat) := (R.u t i, R.v t i)
def predicate (k : Fin events) (i : Fin n) (t : Nat) : Prop :=
  current E (R.states t i) (R.global t) (R.letter i t) k = true
noncomputable def emitted (t : Nat) (k : Nat) : Bool :=
  event E (counts (fun i => mask E (R.states t i) (R.global t) (R.letter i t))) k

theorem local_step (t : Nat) (i : Fin n) : R.states (t + 1) i =
    reset E (R.emitted t) (advance E (R.states t i) (R.global t) (R.letter i t)) := by
  exact (congrFun (congrArg (fun z => z.2.2) (R.step t)) i).symm

theorem local_run (t : Nat) (i : Fin n) :
    (R.states t i).state = localRun E.step E.initial R.global (R.letter i) t := by
  induction t with
  | zero => rw [R.start]; rfl
  | succ t ih =>
    rw [R.local_step]
    change (result E (R.states t i) (R.global t) (R.letter i t)).map Prod.fst = _
    simp only [result, ih, localRun]
    cases localRun E.step E.initial R.global (R.letter i) t <;> rfl

theorem since_history (j : Fin monitors) (t : Nat) (i : Fin n) :
    (R.states (t + 1) i).since j = true ↔
      monitor (fun t => E.gamma j (R.global t) = true)
        (fun t => E.lambda j (R.global t) (R.letter i t) = true) t := by
  induction t with
  | zero => simp [R.local_step, R.start, reset, advance, sinceNext, initial, monitor]
  | succ t ih => simp [R.local_step (t + 1), reset, advance, sinceNext, monitor, ih]

theorem mark (k : Fin events) (j : Fin marks) (hk : E.pred k = .mark j) (t : Nat) (i : Fin n) :
    R.predicate k i t ↔ localMark E.step E.initial R.global (R.letter i) j t := by
  unfold predicate current
  rw [hk]
  change ((result E (R.states t i) (R.global t) (R.letter i t)).map (fun z => z.2 j)).getD false = true ↔ _
  simp only [result, R.local_run, localMark]
  cases hr : localRun E.step E.initial R.global (R.letter i) t with
  | none => simp
  | some q =>
    cases hs : E.step q (R.global t) (R.letter i t) with
    | none => simp [hs]
    | some z => rcases z with ⟨q', m⟩; simp [hs, and_assoc]

theorem sink (k : Fin events) (hk : E.pred k = .sink) (t : Nat) (i : Fin n) :
    R.predicate k i t ↔ localRun E.step E.initial R.global (R.letter i) (t + 1) = none := by
  unfold predicate current
  rw [hk, ← R.local_run, R.local_step]
  change (result E (R.states t i) (R.global t) (R.letter i t)).isNone = true ↔
    (result E (R.states t i) (R.global t) (R.letter i t)).map Prod.fst = none
  cases result E (R.states t i) (R.global t) (R.letter i t) <;> simp

theorem since (k : Fin events) (j : Fin monitors) (hk : E.pred k = .since j) (t : Nat) (i : Fin n) :
    R.predicate k i t ↔ monitor (fun t => E.gamma j (R.global t) = true)
      (fun t => E.lambda j (R.global t) (R.letter i t) = true) t := by
  rw [← R.since_history]
  unfold predicate current
  rw [hk, R.local_step]; rfl

theorem letter_event (k : Fin events) (p : (GI × GO) → (LI × LO) → Bool)
    (hk : E.pred k = .letter p) (t : Nat) (i : Fin n) :
    R.predicate k i t ↔ p (R.global t) (R.letter i t) = true := by
  simp only [predicate, current, hk]

noncomputable def breakpoint (k : Fin events) (hk : E.kind k = .allInf) : Breakpoint (R.predicate k) where
  bit t i := (R.states t i).bit k = true
  event t := R.emitted t k = true
  fire t := by simpa only [hk, emitted, predicate] using event_values (E := E) (R.states t) (R.global t) (R.letter · t) k
  update t i := by simp [R.local_step, reset, advance, hk, ready, predicate, Bool.not_eq_true]

theorem any_event (k : Fin events) (hk : E.kind k = .any) (t : Nat) :
    R.emitted t k = true ↔ ∃ i, R.predicate k i t := by
  simpa only [hk, emitted, predicate] using event_values (E := E) (R.states t) (R.global t) (R.letter · t) k

theorem all_event (k : Fin events) (hk : E.kind k = .allNow) (t : Nat) :
    R.emitted t k = true ↔ ∀ i, R.predicate k i t := by
  simpa only [hk, emitted, predicate] using event_values (E := E) (R.states t) (R.global t) (R.letter · t) k

theorem cardinality_event (k : Fin events) (hk : E.kind k = .cardinality) (t : Nat) :
    R.emitted t k = true ↔
      E.accepted k (counts (fun i => current E (R.states t i) (R.global t) (R.letter i t) k) true) = true := by
  simpa only [hk, emitted] using event_values (E := E) (R.states t) (R.global t) (R.letter · t) k

-- These are the actual emitted events, rather than free traces supplied to the temporal lemmas.
theorem all_inf (k : Fin events) (hk : E.kind k = .allInf) (t : Nat) :
    GF (fun u => R.emitted u k = true) t ↔ ∀ i, GF (R.predicate k i) t :=
  breakpoint_gf (R.breakpoint k hk) t

theorem any_inf (k : Fin events) (hk : E.kind k = .any) (t : Nat) :
    GF (fun u => R.emitted u k = true) t ↔ ∃ i, GF (R.predicate k i) t := by
  simp only [R.any_event k hk]
  exact gf_any _ _

theorem all_fin (k : Fin events) (hk : E.kind k = .any) (t : Nat) :
    FG (fun u => R.emitted u k ≠ true) t ↔ ∀ i, FG (fun u => ¬ R.predicate k i u) t := by
  simp only [ne_eq, R.any_event k hk, not_exists]
  exact (fg_all _ _).symm

theorem any_fin (k : Fin events) (hk : E.kind k = .allInf) (t : Nat) :
    FG (fun u => R.emitted u k ≠ true) t ↔ ∃ i, FG (fun u => ¬ R.predicate k i u) t := by
  rw [← not_gf, R.all_inf k hk]
  simp only [Classical.not_forall, not_gf]

theorem since_encoding (k : Fin events) (j : Fin monitors)
    (hk : E.kind k = .allInf) (hp : E.pred k = .since j) :
    sinceSpec (fun t => E.gamma j (R.global t) = true)
      (fun i t => E.lambda j (R.global t) (R.letter i t) = true) 0 ↔
    GF (fun t => E.gamma j (R.global t) ≠ true) 0 ∨ FG (fun t => R.emitted t k ≠ true) 0 := by
  rw [since_zero, ← not_gf, R.all_inf k hk]
  simp only [Classical.not_forall, not_gf, R.since k j hp]

-- Inf, Fin and sink requirements generated for the deterministic local automaton.
theorem local_encoding {a b : Nat} (inf : Fin a → Fin marks) (fin : Fin b → Fin marks)
    (ie : Fin a → Fin events) (fe : Fin b → Fin events) (se : Fin events)
    (hi : ∀ j, E.kind (ie j) = .allInf ∧ E.pred (ie j) = .mark (inf j))
    (hf : ∀ j, E.kind (fe j) = .any ∧ E.pred (fe j) = .mark (fin j))
    (hs : E.kind se = .any ∧ E.pred se = .sink) :
    (∀ i, localAccepts E.step E.initial R.global (R.letter i) inf fin) ↔
      (∀ j, GF (fun t => R.emitted t (ie j) = true) 0) ∧
      (∀ j, FG (fun t => R.emitted t (fe j) ≠ true) 0) ∧
      G (fun t => R.emitted t se ≠ true) 0 := by
  have valid : G (fun t => R.emitted t se ≠ true) 0 ↔
      ∀ i t, localRun E.step E.initial R.global (R.letter i) t ≠ none := by
    simp only [G, ne_eq, R.any_event se hs.1, R.sink se hs.2]
    constructor
    · intro h i t ht
      cases t with
      | zero => simp [localRun] at ht
      | succ t => exact h t (Nat.zero_le _) ⟨i, ht⟩
    · intro h t _ ⟨i, ht⟩; exact h i (t + 1) ht
  have infEq (j : Fin a) : GF (fun t => R.emitted t (ie j) = true) 0 ↔
      ∀ i, GF (localMark E.step E.initial R.global (R.letter i) (inf j)) 0 := by
    rw [R.all_inf _ (hi j).1]
    simp only [GF, R.mark _ _ (hi j).2]
  have finEq (j : Fin b) : FG (fun t => R.emitted t (fe j) ≠ true) 0 ↔
      ∀ i, FG (fun t => ¬ localMark E.step E.initial R.global (R.letter i) (fin j) t) 0 := by
    rw [R.all_fin _ (hf j).1]
    simp only [R.mark _ _ (hf j).2]
  simp only [infEq, finEq, valid, localAccepts]
  exact ⟨fun h => ⟨fun j i => (h i).2.1 j, fun j i => (h i).2.2 j, fun i => (h i).1⟩,
    fun ⟨hi, hf, hs⟩ i => ⟨hs i, fun j => hi j i, fun j => hf j i⟩⟩

-- Original predicates refer only to the input/output word and deterministic local run.
def source (k : Fin events) (i : Fin n) (t : Nat) : Prop :=
  match E.pred k with
  | .mark j => localMark E.step E.initial R.global (R.letter i) j t
  | .sink => localRun E.step E.initial R.global (R.letter i) (t + 1) = none
  | .letter p => p (R.global t) (R.letter i t) = true
  | .since j => monitor (fun t => E.gamma j (R.global t) = true)
      (fun t => E.lambda j (R.global t) (R.letter i t) = true) t

theorem predicate_source (k : Fin events) (i : Fin n) (t : Nat) : R.predicate k i t ↔ R.source k i t := by
  unfold source
  cases h : E.pred k with
  | mark j => exact R.mark k j h t i
  | sink => exact R.sink k h t i
  | letter p => exact R.letter_event k p h t i
  | since j => exact R.since k j h t i

noncomputable def vertices (k : Nat) : Vertex n Q (Local L monitors events) GI LI :=
  if k % 2 = 0 then .env (R.q (k / 2)) (R.color (k / 2)) (R.states (k / 2))
  else .sys (R.q (k / 2)) (R.color (k / 2)) (R.states (k / 2)) (R.a (k / 2)) (R.u (k / 2))

@[simp] theorem vertices_even (t : Nat) : R.vertices (2 * t) = .env (R.q t) (R.color t) (R.states t) := by
  simp [vertices, Nat.mul_comm]
@[simp] theorem vertices_odd (t : Nat) :
    R.vertices (2 * t + 1) = .sys (R.q t) (R.color t) (R.states t) (R.a t) (R.u t) := by
  simp [vertices, Nat.add_div, Nat.mul_comm]

private theorem round_cases (k : Nat) : ∃ t, k = 2 * t ∨ k = 2 * t + 1 := by
  refine ⟨k / 2, ?_⟩
  omega

theorem play : IsPlay (game (engineMachine E)) R.vertices := by
  intro k
  obtain ⟨t, rfl | rfl⟩ := round_cases k
  · simp only [R.vertices_even, R.vertices_odd]; exact ⟨rfl, rfl, rfl⟩
  · rw [show 2 * t + 1 + 1 = 2 * (t + 1) by omega, R.vertices_even, R.vertices_odd]
    exact ⟨R.b t, R.v t, R.step t⟩

theorem vertex_start (q0 : Q) (hq : R.q 0 = q0) (hc : R.color 0 = 0) :
    R.vertices 0 = .env q0 0 (fun _ => initial E) := by
  have hx : R.states 0 = fun _ => initial E := funext R.start
  change R.vertices (2 * 0) = _
  rw [R.vertices_even, hq, hc, hx]

end Run

-- Every indexed game play has a word realization; no event or monitor invariant is assumed.
theorem play_run (π : Nat → Vertex n Q (Local L monitors events) GI LI) (q0 : Q)
    (hp : IsPlay (game (engineMachine E)) π) (h0 : π 0 = .env q0 0 (fun _ => initial E)) :
    ∃ R : Run E n, R.vertices = π ∧ R.q 0 = q0 ∧ R.color 0 = 0 := by
  classical
  have env (t : Nat) : ∃ z : Q × Nat × (Fin n → Local L monitors events),
      π (2 * t) = .env z.1 z.2.1 z.2.2 := by
    induction t with
    | zero => exact ⟨(q0, 0, fun _ => initial E), h0⟩
    | succ t ih =>
      obtain ⟨z, hz⟩ := ih
      have he := hp (2 * t)
      cases hs : π (2 * t + 1) with
      | env => simp [hz, hs, game] at he
      | sys q c x a u =>
        have hn := hp (2 * t + 1)
        cases ht : π (2 * t + 1 + 1) with
        | sys => simp [hs, ht, game] at hn
        | env q' c' x' => exact ⟨(q', c', x'), rfl⟩
  let z t := Classical.choose (env t)
  have hz t : π (2 * t) = .env (z t).1 (z t).2.1 (z t).2.2 := Classical.choose_spec (env t)
  have inp (t : Nat) : ∃ a : GI × (Fin n → LI),
      π (2 * t + 1) = .sys (z t).1 (z t).2.1 (z t).2.2 a.1 a.2 := by
    have he := hp (2 * t)
    cases hs : π (2 * t + 1) with
    | env => simp [hz t, hs, game] at he
    | sys q c x a u =>
      rw [hz t, hs] at he
      obtain ⟨rfl, rfl, rfl⟩ := he
      exact ⟨(a, u), rfl⟩
  let a t := Classical.choose (inp t)
  have ha t : π (2 * t + 1) = .sys (z t).1 (z t).2.1 (z t).2.2 (a t).1 (a t).2 := Classical.choose_spec (inp t)
  have out (t : Nat) : ∃ b : GO × (Fin n → LO),
      transition (engineMachine E) (z t).1 (z t).2.2 (a t).1 b.1 (a t).2 b.2 = z (t + 1) := by
    have he := hp (2 * t + 1)
    rw [ha t, show 2 * t + 1 + 1 = 2 * (t + 1) by omega, hz (t + 1)] at he
    obtain ⟨b, v, hv⟩ := he
    exact ⟨(b, v), hv⟩
  let b t := Classical.choose (out t)
  have hstart : (q0, 0, fun _ : Fin n => initial E) = z 0 := by
    have he := Vertex.env.inj ((hz 0).symm.trans h0)
    exact Prod.ext he.1.symm (Prod.ext he.2.1.symm he.2.2.symm)
  let R : Run E n :=
    ⟨(fun t => (z t).1), (fun t => (z t).2.1), (fun t => (z t).2.2),
      (fun t => (a t).1), (fun t => (b t).1), (fun t => (a t).2), (fun t => (b t).2),
      (fun i => by change (z 0).2.2 i = _; rw [← hstart]), fun t => Classical.choose_spec (out t)⟩
  refine ⟨R, funext fun k => ?_, ?_, ?_⟩
  · obtain ⟨t, rfl | rfl⟩ := Run.round_cases k
    · exact (R.vertices_even t).trans (hz t).symm
    · exact (R.vertices_odd t).trans (ha t).symm
  · change (z 0).1 = q0; rw [← hstart]
  · change (z 0).2.1 = 0; rw [← hstart]

-- Each mode records the event kind emitted by atom(); Fin modes use the negated predicate.
inductive Mode where
  | anyNow | allNow | anyInf | allInf | allFin | anyFin | cardinality

def Mode.kind : Mode → Kind
  | .anyNow | .anyInf | .allFin => .any
  | .allNow => .allNow
  | .allInf | .anyFin => .allInf
  | .cardinality => .cardinality

structure Atom {GlobalLetter LocalLetter : Type} (E : Tables Q L GlobalLetter LocalLetter marks monitors events) where
  index : Fin events
  mode : Mode
  valid : E.kind index = mode.kind

noncomputable def Atom.source (A : Atom E) (R : Run E n) : Trace := by
  classical
  exact match A.mode with
  | .anyNow => fun t => ∃ i, R.source A.index i t
  | .allNow => fun t => ∀ i, R.source A.index i t
  | .anyInf => fun t => ∃ i, GF (R.source A.index i) t
  | .allInf => fun t => ∀ i, GF (R.source A.index i) t
  | .allFin => fun t => ∀ i, FG (fun u => ¬ R.source A.index i u) t
  | .anyFin => fun t => ∃ i, FG (fun u => ¬ R.source A.index i u) t
  | .cardinality => fun t => E.accepted A.index
      (counts (fun i => decide (R.source A.index i t)) true) = true

def Atom.encoded (A : Atom E) (ev : Nat → Nat → Bool) : Trace :=
  match A.mode with
  | .anyNow | .allNow | .cardinality => fun t => ev t A.index = true
  | .anyInf | .allInf => GF (fun t => ev t A.index = true)
  | .allFin | .anyFin => FG (fun t => ev t A.index ≠ true)

theorem Atom.correct (A : Atom E) (R : Run E n) (t : Nat) :
    A.source R t ↔ A.encoded R.emitted t := by
  classical
  have he : R.source A.index = R.predicate A.index :=
    funext fun i => funext fun u => propext (R.predicate_source A.index i u).symm
  simp only [Atom.source, he, Atom.encoded]
  rcases A with ⟨k, mode, h⟩
  cases mode with
  | anyNow => exact (R.any_event k h t).symm
  | allNow => exact (R.all_event k h t).symm
  | anyInf => exact (R.any_inf k h t).symm
  | allInf => exact (R.all_inf k h t).symm
  | allFin => exact (R.all_fin k h t).symm
  | anyFin => exact (R.any_fin k h t).symm
  | cardinality =>
    have hs : (fun i => decide (R.predicate k i t)) =
        fun i => current E (R.states t i) (R.global t) (R.letter i t) k := by
      funext i
      cases hb : current E (R.states t i) (R.global t) (R.letter i t) k <;>
        simp [Run.predicate, hb]
    change E.accepted k (counts (fun i => decide (R.predicate k i t)) true) = true ↔
      R.emitted t k = true
    rw [hs]
    exact (R.cardinality_event k h t).symm

-- The parser's positive top conjuncts. Arbitrary temporal contexts are allowed for
-- pointwise-correct atoms; since and local acceptance are rewritten only at position 0.
inductive Clause {GlobalLetter LocalLetter : Type} (E : Tables Q L GlobalLetter LocalLetter marks monitors events) where
  | temporal (context : (Nat → GlobalLetter) → (Atom E → Trace) → Prop)
  | since (j : Fin monitors) (k : Fin events)
      (kind : E.kind k = .allInf) (pred : E.pred k = .since j)
  | localAcceptance {a b : Nat} (inf : Fin a → Fin marks) (fin : Fin b → Fin marks)
      (ie : Fin a → Fin events) (fe : Fin b → Fin events) (se : Fin events)
      (hi : ∀ j, E.kind (ie j) = .allInf ∧ E.pred (ie j) = .mark (inf j))
      (hf : ∀ j, E.kind (fe j) = .any ∧ E.pred (fe j) = .mark (fin j))
      (hs : E.kind se = .any ∧ E.pred se = .sink)
  | totalLocal {a b : Nat} (inf : Fin a → Fin marks) (fin : Fin b → Fin marks)
      (ie : Fin a → Fin events) (fe : Fin b → Fin events)
      (hi : ∀ j, E.kind (ie j) = .allInf ∧ E.pred (ie j) = .mark (inf j))
      (hf : ∀ j, E.kind (fe j) = .any ∧ E.pred (fe j) = .mark (fin j))
      (total : ∀ q g x, ∃ z, E.step q g x = some z)

noncomputable def Clause.source (C : Clause E) (R : Run E n) : Prop :=
  match C with
  | .temporal context => context R.global (fun A => A.source R)
  | .since j _ _ _ => sinceSpec (fun t => E.gamma j (R.global t) = true)
      (fun i t => E.lambda j (R.global t) (R.letter i t) = true) 0
  | .localAcceptance inf fin .. | .totalLocal inf fin .. =>
      ∀ i, localAccepts E.step E.initial R.global (R.letter i) inf fin

def Clause.encoded (C : Clause E) (g : Nat → GI × GO) (ev : Nat → Nat → Bool) : Prop :=
  match C with
  | .temporal context => context g (fun A => A.encoded ev)
  | .since j k _ _ => GF (fun t => E.gamma j (g t) ≠ true) 0 ∨ FG (fun t => ev t k ≠ true) 0
  | .localAcceptance _ _ ie fe se .. =>
      (∀ j, GF (fun t => ev t (ie j) = true) 0) ∧
      (∀ j, FG (fun t => ev t (fe j) ≠ true) 0) ∧ G (fun t => ev t se ≠ true) 0
  | .totalLocal _ _ ie fe .. =>
      (∀ j, GF (fun t => ev t (ie j) = true) 0) ∧ (∀ j, FG (fun t => ev t (fe j) ≠ true) 0)

theorem Clause.correct (C : Clause E) (R : Run E n) : C.source R ↔ C.encoded R.global R.emitted := by
  cases C with
  | temporal context =>
    have he : (fun A : Atom E => A.source R) = fun A => A.encoded R.emitted :=
      funext fun A => funext fun t => propext (A.correct R t)
    exact Iff.of_eq (congrArg (context R.global) he)
  | since j k hk hp => exact R.since_encoding k j hk hp
  | localAcceptance inf fin ie fe se hi hf hs => exact R.local_encoding inf fin ie fe se hi hf hs
  | totalLocal inf fin ie fe hi hf total =>
    have never (i : Fin n) (t : Nat) : ∃ q, localRun E.step E.initial R.global (R.letter i) t = some q := by
      induction t with
      | zero => exact ⟨E.initial, rfl⟩
      | succ t ih =>
        obtain ⟨q, hq⟩ := ih
        obtain ⟨z, hz⟩ := total q (R.global t) (R.letter i t)
        exact ⟨z.1, by simp [localRun, hq, hz]⟩
    have infEq (j) : GF (fun t => R.emitted t (ie j) = true) 0 ↔
        ∀ i, GF (localMark E.step E.initial R.global (R.letter i) (inf j)) 0 := by
      rw [R.all_inf _ (hi j).1]; simp only [GF, R.mark _ _ (hi j).2]
    have finEq (j) : FG (fun t => R.emitted t (fe j) ≠ true) 0 ↔
        ∀ i, FG (fun t => ¬ localMark E.step E.initial R.global (R.letter i) (fin j) t) 0 := by
      rw [R.all_fin _ (hf j).1]; simp only [R.mark _ _ (hf j).2]
    simp only [Clause.source, Clause.encoded, infEq, finEq, localAccepts]
    constructor
    · intro h; exact ⟨fun j i => (h i).2.1 j, fun j i => (h i).2.2 j⟩
    · rintro ⟨hi, hf⟩ i
      refine ⟨?_, fun j => hi j i, fun j => hf j i⟩
      intro t ht
      obtain ⟨q, hq⟩ := never i t
      rw [ht] at hq
      cases hq

-- This is solely the global parity translator's correspondence boundary. Its
-- input is the encoded formula on global letters/events, not the original spec.
def Translates (E : Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := LI) (LO := LO)
    (marks := marks) (monitors := monitors) (events := events)) (q0 : Q) (spec : List (Clause E)) : Prop :=
  ∀ (g : Nat → GI × GO) (ev : Nat → Nat → Bool) (q : Nat → Q) (c : Nat → Nat),
    q 0 = q0 → c 0 = 0 →
    (∀ t, E.globalStep (q t) (g t) (fun k => ev t k) = (q (t + 1), c (t + 1))) →
    (maxOdd (fun k => if k % 2 = 0 then c (k / 2) else 0) ↔ ∀ C ∈ spec, C.encoded g ev)

theorem Run.specification (R : Run E n) (spec : List (Clause E)) (q0 : Q)
    (translated : Translates E q0 spec) (hq : R.q 0 = q0) (hc : R.color 0 = 0) :
    (∀ C ∈ spec, C.source R) ↔ maxOdd (fun k => (game (engineMachine E)).prio (R.vertices k)) := by
  have step (t) : E.globalStep (R.q t) (R.global t) (fun k => R.emitted t k) =
      (R.q (t + 1), R.color (t + 1)) :=
    congrArg (fun z => (z.1, z.2.1)) (R.step t)
  have hp : (fun k => (game (engineMachine E)).prio (R.vertices k)) =
      fun k => if k % 2 = 0 then R.color (k / 2) else 0 := by
    funext k; unfold Run.vertices; split <;> rfl
  rw [hp, translated R.global R.emitted R.q R.color hq hc step]
  exact ⟨fun h C hm => (C.correct R).mp (h C hm), fun h C hm => (C.correct R).mpr (h C hm)⟩

-- A strategy in the specification's game quantifies over every original I/O
-- word consistent with it; its objective contains no encoded events or priorities.
def SpecWins (spec : List (Clause E)) (q0 : Q) (p : Bool)
    (σ : Vertex n Q (Local L monitors events) GI LI → Vertex n Q (Local L monitors events) GI LI) : Prop :=
  Legal (game (engineMachine E)) p σ ∧ ∀ R : Run E n, R.q 0 = q0 → R.color 0 = 0 →
    Consistent (game (engineMachine E)) p σ R.vertices →
    if p then (∀ C ∈ spec, C.source R) else ¬ (∀ C ∈ spec, C.source R)

theorem spec_wins_iff (spec : List (Clause E)) (q0 : Q) (translated : Translates E q0 spec)
    (p : Bool) (σ : Vertex n Q (Local L monitors events) GI LI → Vertex n Q (Local L monitors events) GI LI) :
    SpecWins spec q0 p σ ↔ Wins (game (engineMachine E)) maxOdd p σ (.env q0 0 (fun _ => initial E)) := by
  constructor
  · rintro ⟨legal, win⟩
    refine ⟨legal, fun π hp h0 hσ => ?_⟩
    obtain ⟨R, rfl, hq, hc⟩ := play_run π q0 hp h0
    have hs := win R hq hc hσ
    cases p <;> simpa only [Goal, R.specification spec q0 translated hq hc] using hs
  · rintro ⟨legal, win⟩
    refine ⟨legal, fun R hq hc hσ => ?_⟩
    have hs := win R.vertices R.play (R.vertex_start q0 hq hc) hσ
    cases p <;> simpa only [Goal, R.specification spec q0 translated hq hc] using hs

-- A checked winner of the engine's count graph is exactly a winner for the original spec.
theorem engine_spec_verdict
    (E : Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := Fin ki) (LO := Fin ko)
      (marks := marks) (monitors := monitors) (events := events))
    (spec : List (Clause E)) (q0 : Q) (translated : Translates E q0 spec)
    (absorbing : Q → Option Nat)
    (valid : ∀ q c, absorbing q = some c → Absorbing (engineMachine E) q c)
    (a : GI) (b : GO) (u : Fin ki) (v : Fin ko) {p : Bool}
    {σ : CountKey n Q (Local L monitors events) GI (Fin ki) → CountKey n Q (Local L monitors events) GI (Fin ki)}
    (certificate : Certified (countGame (engineMachine E) absorbing (initial E)) maxOdd p σ
      (countKey (collapse absorbing (initial E) (.env q0 0 (fun _ => initial E))))) :
    (∃ strategy, SpecWins (n := n) spec q0 p strategy) ∧ ∀ other, ¬ SpecWins (n := n) spec q0 (!p) other := by
  obtain ⟨⟨strategy, win⟩, excludes⟩ := count_certificate (engineMachine E) absorbing (initial E) valid a b u v
    (s := .env q0 0 (fun _ => initial E)) certificate
  exact ⟨⟨strategy, (spec_wins_iff spec q0 translated p strategy).mpr win⟩,
    fun other h => excludes other ((spec_wins_iff spec q0 translated (!p) other).mp h)⟩

end Symmetry.CountEngine

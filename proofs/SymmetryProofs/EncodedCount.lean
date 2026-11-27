import SymmetryProofs.CountCardinality

namespace Symmetry.EncodedCount

variable {Coord Code Raw Atom : Type}

def decode (encode : Coord → Code) (code : Code) (coord : Coord) : Prop :=
  code = encode coord

theorem decode_encode (encode : Coord → Code)
    (injective : ∀ {i j}, encode i = encode j → i = j) (i j : Coord) :
    decode encode (encode i) j ↔ i = j :=
  ⟨injective, fun h => congrArg encode h⟩

theorem cube_decodes {Bit : Type} (encode : Coord → Code) (bits : Code → Bit → Bool)
    (injective : ∀ {c d}, bits c = bits d → c = d) (c : Code) (i : Coord) :
    (∀ b, bits c b = bits (encode i) b) ↔ decode encode c i :=
  ⟨fun h => injective (funext h), fun h b => congrArg (fun d => bits d b) h⟩

theorem decode_action (encode : Coord → Code) (p : Perm Coord) (q : Perm Code)
    (checked : ∀ i, q (encode i) = encode (p i)) (c : Code) (i : Coord) :
    decode encode (q c) (p i) ↔ decode encode c i := by
  simp only [decode, ← checked]
  exact ⟨q.injective, congrArg q.toFun⟩

theorem unused_action (encode : Coord → Code) (p : Perm Coord) (q : Perm Code)
    (checked : ∀ i, q (encode i) = encode (p i)) (c : Code) :
    (∀ i, ¬ decode encode (q c) i) ↔ ∀ i, ¬ decode encode c i := by
  constructor
  · intro h i hc
    exact h (p i) ((decode_action encode p q checked c i).mpr hc)
  · intro h i hc
    apply h (p.invFun i)
    apply (decode_action encode p q checked c (p.invFun i)).mp
    simpa only [p.right] using hc

abbrev Valuation (Atom : Type) := Atom → Nat → Prop
abbrev Objective (Atom : Type) := Valuation Atom → Prop

def tableTrace (table : Raw → Raw → Prop) (word : Nat → Raw) (t : Nat) : Prop :=
  table (word t) (word (t + 1))

theorem table_action (table : Raw → Raw → Prop) (q : Perm Raw)
    (checked : ∀ a b, table (q a) (q b) ↔ table a b) (word : Nat → Raw) (t : Nat) :
    tableTrace table (q ∘ word) t ↔ tableTrace table word t :=
  checked _ _

-- A formula is a context in its AP traces, including arbitrary temporal operators.
theorem encoded_decomposition_correct (objective : Objective Atom) (decoded expanded : Valuation Atom)
    (checked : ∀ a t, decoded a t ↔ expanded a t) :
    objective decoded ↔ objective expanded := by
  have h : decoded = expanded := funext fun a => funext fun t => propext (checked a t)
  rw [h]

def Preserves (objective : (Nat → Raw) → Prop) (q : Perm Raw) : Prop :=
  ∀ word, objective (q ∘ word) ↔ objective word

theorem generator_checked (objective : Objective Atom) (decodeWord : (Nat → Raw) → Valuation Atom)
    (q : Perm Raw) (p : Perm Atom)
    (atoms : ∀ word a t, decodeWord (q ∘ word) a t ↔ decodeWord word (p.invFun a) t)
    (rename : ∀ letter, objective (fun a => letter (p.invFun a)) ↔ objective letter) :
    Preserves (objective ∘ decodeWord) q := by
  intro word
  exact (encoded_decomposition_correct objective _ _ (atoms word)).trans (rename _)

inductive Generated (generators : List (Perm Raw)) : Perm Raw → Prop
  | identity : Generated generators Perm.id
  | generator {q} : q ∈ generators → Generated generators q
  | inverse {q} : Generated generators q → Generated generators q.symm
  | comp {p q} : Generated generators p → Generated generators q → Generated generators (p.comp q)

theorem family_check_sound (objective : (Nat → Raw) → Prop)
    {generators : List (Perm Raw)} (checked : ∀ q ∈ generators, Preserves objective q)
    {p : Perm Raw} (generated : Generated generators p) : Preserves objective p := by
  induction generated with
  | identity => intro word; rfl
  | generator h => exact checked _ h
  | @inverse q _ ih =>
    intro word
    have h : q ∘ (q.symm ∘ word) = word := funext fun t => q.right (word t)
    simpa only [h] using (ih (q.symm ∘ word)).symm
  | comp _ _ hp hq =>
    intro word
    exact (hp (_ ∘ word)).trans (hq word)

theorem raw_objective_preserved (raw : (Nat → Raw) → Prop) (objective : Objective Atom)
    (decodeWord : (Nat → Raw) → Valuation Atom)
    (reconstruct : ∀ word, raw word ↔ objective (decodeWord word))
    {generators : List (Perm Raw)} (checked : ∀ q ∈ generators, ∃ p : Perm Atom,
      (∀ word a t, decodeWord (q ∘ word) a t ↔ decodeWord word (p.invFun a) t) ∧
      (∀ letter, objective (fun a => letter (p.invFun a)) ↔ objective letter))
    {q : Perm Raw} (witness : Generated generators q) : Preserves raw q := by
  have decoded : Preserves (objective ∘ decodeWord) q :=
    family_check_sound (objective ∘ decodeWord) (fun g hg => by
      obtain ⟨p, atoms, rename⟩ := checked g hg
      exact generator_checked objective decodeWord g p atoms rename) witness
  intro word
  exact (reconstruct _).trans ((decoded word).trans (reconstruct word).symm)

-- Both the coupled coordinates and each residual class use a checked star.
theorem raw_permutation_generated {A : Type} [DecidableEq A] (all : List A)
    (full : ∀ x, x ∈ all) (root : A) (onCodes : Perm A → Perm Raw)
    (identity : onCodes Perm.id = Perm.id)
    (comp : ∀ p q, onCodes (p.comp q) = (onCodes p).comp (onCodes q))
    {generators : List (Perm Raw)}
    (stars : ∀ a, a ≠ root → onCodes (transposition root a) ∈ generators)
    (p : Perm A) : Generated generators (onCodes p) := by
  have hid : Generated generators (onCodes Perm.id) := identity ▸ .identity
  have hc (p q) (hp : Generated generators (onCodes p)) (hq : Generated generators (onCodes q)) :
      Generated generators (onCodes (p.comp q)) := comp p q ▸ .comp hp hq
  apply transpositions_generate all full _ hid _ hc p
  intro x y
  by_cases he : x = y
  · subst y
    have eq : transposition x x = Perm.id := Perm.ext fun z => by
      by_cases h : z = x <;> simp [transposition, swap, Perm.id, h]
    simpa only [eq] using hid
  by_cases hx : x = root
  · subst x; exact .generator (stars y (Ne.symm he))
  by_cases hy : y = root
  · subst y; rw [transposition_comm]; exact .generator (stars x hx)
  rw [transposition_star root x y hx hy he]
  exact hc _ _ (.generator (stars x hx)) (hc _ _ (.generator (stars y hy)) (.generator (stars x hx)))

open CountGame CountTemporal

variable {Global Look QState : Type}

-- There is a legal zero-weight mode exactly when some available raw code
-- selects no moving coordinate. This includes fixed codes and linked aliases.
theorem legal_split {Bus : Bool → Type} {n : Nat} (owner : Bool)
    (encode : Bus owner → Fin n → Code)
    (available : Bus owner → Code → Prop)
    (injective : ∀ b i j, encode b i = encode b j → i = j)
    (letter : Bus owner → Fin n → Prop) :
    (∃ raw : Bus owner → Code, ∀ b, available b (raw b) ∧
      ∀ i, letter b i ↔ decode (encode b) (raw b) i) ↔
    ∀ b, (∃ j, available b (encode b j) ∧ ∀ i, letter b i ↔ j = i) ∨
         (∃ c, available b c ∧ (∀ i, ¬ decode (encode b) c i) ∧ ∀ i, ¬ letter b i) := by
  classical
  constructor
  · rintro ⟨raw, h⟩ b
    by_cases hit : ∃ j, decode (encode b) (raw b) j
    · obtain ⟨j, hj⟩ := hit
      change raw b = encode b j at hj
      refine Or.inl ⟨j, hj ▸ (h b).1, fun i => ((h b).2 i).trans ?_⟩
      rw [hj]
      exact decode_encode _ (injective b _ _) _ _
    · have idle : ∀ i, ¬ decode (encode b) (raw b) i := fun i hi => hit ⟨i, hi⟩
      exact Or.inr ⟨raw b, (h b).1, idle, fun i hi => idle i (((h b).2 i).mp hi)⟩
  · intro h
    have witness (b : Bus owner) : ∃ c, available b c ∧
        ∀ i, letter b i ↔ decode (encode b) c i := by
      rcases h b with ⟨j, ha, hl⟩ | ⟨c, ha, hi, hl⟩
      · exact ⟨encode b j, ha, fun i => (hl i).trans (decode_encode _ (injective b _ _) _ _).symm⟩
      · exact ⟨c, ha, fun i => ⟨fun e => False.elim (hl i e), fun e => False.elim (hi i e)⟩⟩
    exact ⟨fun b => Classical.choose (witness b), fun b => Classical.choose_spec (witness b)⟩

-- A marked cell is a local state together with the slots naming its members.
-- Slot order in the implementation supplies a canonical name for each nonempty tag.
def markedCell {L Slot : Type} {n : Nat} (states : Fin n → L)
    (anchor : Slot → Option (Fin n)) (i : Fin n) : L × (Slot → Prop) :=
  (states i, fun k => anchor k = some i)

theorem anchor_key_orbit {L Slot : Type} {n : Nat} (x y : Fin n → L)
    (a b : Slot → Option (Fin n)) :
    counts (markedCell x a) = counts (markedCell y b) ↔
      ∃ p : Perm (Fin n), act p x = y ∧ ∀ k, (a k).map p = b k := by
  rw [counts_orbit]
  constructor
  · rintro ⟨p, h⟩
    have point (i) := congrFun h (p i)
    have first (i) : x i = y (p i) := by
      simpa [act, markedCell] using congrArg Prod.fst (point i)
    have tags (i k) : (a k = some i) = (b k = some (p i)) := by
      simpa [act, markedCell] using congrFun (congrArg Prod.snd (point i)) k
    refine ⟨p, funext fun i => ?_, fun k => ?_⟩
    · simpa [act] using first (p.invFun i)
    · cases ha : a k with
      | some i =>
        change some (p i) = b k
        exact ((tags i k).mp ha).symm
      | none =>
        cases hb : b k with
        | none => rfl
        | some j =>
          have impossible := (tags (p.invFun j) k).mpr (by simpa using hb)
          simp [ha] at impossible
  · rintro ⟨p, h, hm⟩
    refine ⟨p, funext fun i => Prod.ext (congrFun h i) ?_⟩
    apply funext
    intro k
    apply propext
    simp only [act, markedCell]
    rw [← hm]
    cases hk : a k with
    | none => simp
    | some j =>
      simp only [Option.map_some, Option.some.injEq]
      exact ⟨fun e => by rw [e, p.right], fun e => by rw [← e, p.left]⟩

theorem free_member {L Slot : Type} {n : Nat} (x : Fin n → L)
    (anchor : Slot → Option (Fin n)) (state : L) :
    0 < counts (markedCell x anchor) (state, fun _ => False) ↔
      ∃ i, x i = state ∧ ∀ k, anchor k ≠ some i := by
  rw [count_positive]
  constructor
  · rintro ⟨i, hi⟩
    refine ⟨i, congrArg Prod.fst hi, fun k => ?_⟩
    exact (congrFun (congrArg Prod.snd hi) k).mp
  · rintro ⟨i, hi, free⟩
    exact ⟨i, Prod.ext hi (funext fun k => propext ⟨free k, False.elim⟩)⟩

-- Extending a marked prefix with a legal coordinate choice preserves both
-- coincidence with old marks and the count key after that choice.
theorem extend_anchor_key {L Slot : Type} {n : Nat} (x y : Fin n → L)
    (a b : Slot → Option (Fin n))
    (equal : counts (markedCell x a) = counts (markedCell y b)) (i : Fin n) :
    ∃ j,
      counts (markedCell x (Sum.elim a (fun _ : Unit => some i))) =
      counts (markedCell y (Sum.elim b (fun _ : Unit => some j))) := by
  obtain ⟨p, h, marks⟩ := (anchor_key_orbit x y a b).mp equal
  refine ⟨p i, (anchor_key_orbit _ _ _ _).mpr ⟨p, h, ?_⟩⟩
  intro slot
  cases slot with
  | inl k => exact marks k
  | inr _ => rfl

-- The same orbit lemma applies to residual codes by taking local state to be
-- the certified class ID. A previous and a current slot share a tag on repeat.
theorem residual_key_orbit {Class : Type} {n : Nat} (classes : Fin n → Class)
    (a b : Bool → Option (Fin n)) :
    counts (markedCell classes a) = counts (markedCell classes b) ↔
      ∃ p : Perm (Fin n), act p classes = classes ∧ ∀ k, (a k).map p = b k :=
  anchor_key_orbit classes classes a b

-- The buffered letter includes immediate global data and the emitted count
-- events. The previous raw tuple is needed only for the table evaluation.
def logicalLetter (g : Nat → Global) (w : Nat → Raw) (table : Raw → Raw → Look) (t : Nat) :=
  (g t, table (w t) (w (t + 1)))

def globalRun (step : QState → Global × Look → QState × Nat)
    (initial : QState) (letter : Nat → Global × Look) : Nat → QState × Nat
  | 0 => (initial, 0)
  | t + 1 => step (globalRun step initial letter t).1 (letter t)

def bufferedRun (step : QState → Global × Look → QState × Nat) (initial : QState)
    (g : Nat → Global) (w : Nat → Raw) (table : Raw → Raw → Look) :
    Nat → (QState × Nat) × Option (Global × Raw)
  | 0 => ((initial, 0), none)
  | t + 1 =>
    let (s, buf) := bufferedRun step initial g w table t
    (buf.elim s (fun (old, prev) => step s.1 (old, table prev (w t))), some (g t, w t))

theorem buffered_trace (step : QState → Global × Look → QState × Nat) (initial : QState)
    (g : Nat → Global) (w : Nat → Raw) (table : Raw → Raw → Look) (t : Nat) :
    bufferedRun step initial g w table (t + 1) =
      (globalRun step initial (logicalLetter g w table) t, some (g t, w t)) := by
  induction t with
  | zero => rfl
  | succ t ih => rw [bufferedRun, ih]; rfl

def alternating (c : Nat → Nat) (k : Nat) := if k % 2 = 0 then c (k / 2) else 0

theorem alternating_delay (c d : Nat → Nat) (align : ∀ t, d (t + 1) = c t) :
    maxOdd (alternating d) ↔ maxOdd (alternating c) := by
  have tail (k) : alternating d (k + 2) = alternating c k := by
    have hm : (k + 2) % 2 = k % 2 := by omega
    have hd : (k + 2) / 2 = k / 2 + 1 := by omega
    simp only [alternating, hm, hd, align]
  have recurrent (a) :
      GF (fun k => alternating d k = a) 0 ↔ GF (fun k => alternating c k = a) 0 := by
    constructor
    · intro h k _
      obtain ⟨t, ht, e⟩ := h (k + 2) (Nat.zero_le _)
      refine ⟨t - 2, by omega, ?_⟩
      have eq : t - 2 + 2 = t := by omega
      exact (tail (t - 2)).symm.trans (by simpa only [eq] using e)
    · intro h k _
      obtain ⟨t, ht, e⟩ := h k (Nat.zero_le _)
      exact ⟨t + 2, by omega, (tail t).trans e⟩
  simp only [maxOdd, recurrent]

theorem buffered_priority (step : QState → Global × Look → QState × Nat) (initial : QState)
    (g : Nat → Global) (w : Nat → Raw) (table : Raw → Raw → Look) :
    maxOdd (alternating fun t => (bufferedRun step initial g w table t).1.2) ↔
      maxOdd (alternating fun t => (globalRun step initial (logicalLetter g w table) t).2) :=
  alternating_delay _ _ (fun t => congrArg (fun z => z.1.2) (buffered_trace step initial g w table t))

-- The count-engine clause equivalences still apply to the undelayed local run.
theorem encoded_local_acceptance {Q L GI GO LI LO : Type} {m k e n : Nat}
    {E : CountEngine.Engine (Q := Q) (L := L) (GI := GI) (GO := GO) (LI := LI) (LO := LO)
      (marks := m) (monitors := k) (events := e)}
    (R : CountEngine.Run E n) (C : CountEngine.Clause E) :
    C.source R ↔ C.encoded R.global R.emitted :=
  C.correct R

open Symmetry.Quotient in
/-- A checked reachable key strategy wins in the legal raw arena for the same
player. The arena correspondence supplies the canonical key and move witness
on each owner's turn. -/
theorem encoded_certificate {V K Γ : Type} {G : Game V} (S : Action G Γ)
    (H : Game K) (key : V → K)
    (owner : ∀ v, H.owner (key v) = G.owner v)
    (prio : ∀ v, H.prio (key v) = G.prio v)
    (canon : ∀ v, key (S.canon v) = key v)
    (edges : ∀ v k, H.edge (key v) k ↔ ∃ w, G.edge v w ∧ key w = k)
    (total : ∀ k, ∃ k', H.edge k k') {p : Bool} {σ : K → K} {root : V}
    (checked : Certified H maxOdd p σ (key root)) :
    (∃ τ, Wins G maxOdd p τ root) ∧ ∀ other, ¬ Wins G maxOdd (!p) other root := by
  obtain ⟨τ, hτ⟩ := checked.wins total
  have win := S.lift_wins (S.key_wins H key owner prio canon edges hτ)
  refine ⟨⟨_, win⟩, fun other opposite => ?_⟩
  cases p
  · exact exclusive opposite win
  · exact exclusive win opposite

end Symmetry.EncodedCount

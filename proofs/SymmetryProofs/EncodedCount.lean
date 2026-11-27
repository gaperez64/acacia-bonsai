import SymmetryProofs.Automaton

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

end Symmetry.EncodedCount

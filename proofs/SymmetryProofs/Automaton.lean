import Std

namespace Symmetry

/-- C++: symmetry.hh:144–209, 252–277. A completed permutation and its inverse. -/
structure Perm (α : Type) where
  toFun : α → α
  invFun : α → α
  left_inv : ∀ x, invFun (toFun x) = x
  right_inv : ∀ x, toFun (invFun x) = x

instance : CoeFun (Perm α) (fun _ => α → α) := ⟨Perm.toFun⟩

@[simp] theorem Perm.left (p : Perm α) (x : α) : p.invFun (p x) = x := p.left_inv x
@[simp] theorem Perm.right (p : Perm α) (x : α) : p (p.invFun x) = x := p.right_inv x

theorem Perm.injective (p : Perm α) {x y : α} (h : p x = p y) : x = y := by
  have := congrArg p.invFun h
  simpa using this

/-- C++: symmetry.hh:252–277. Reverse the completed state/AP permutation. -/
def Perm.symm (p : Perm α) : Perm α := ⟨p.invFun, p.toFun, p.right_inv, p.left_inv⟩

/-- C++: symmetry.hh:387–424. Compose verified generators. -/
def Perm.comp (p q : Perm α) : Perm α where
  toFun x := p (q x)
  invFun x := q.invFun (p.invFun x)
  left_inv x := by simp
  right_inv x := by simp

/-- C++: symmetry.hh:265. Identity on APs/states outside the indexed families. -/
def Perm.id : Perm α := ⟨(fun x => x), (fun x => x), fun _ => rfl, fun _ => rfl⟩

@[ext] theorem Perm.ext {p q : Perm α} (h : ∀ x, p x = q x) : p = q := by
  have hf : p.toFun = q.toFun := funext h
  have hi : p.invFun = q.invFun := by
    funext x
    apply p.injective
    rw [p.right_inv, h, q.right_inv]
  cases p; cases q; simp_all

/-- C++: symmetry.hh:260–264. Exchange two clients. -/
def swap [DecidableEq α] (a b : α) (x : α) : α :=
  if x = a then b else if x = b then a else x

@[simp] theorem swap_swap [DecidableEq α] (a b x : α) : swap a b (swap a b x) = x := by
  by_cases ha : x = a <;> by_cases hb : x = b <;> simp_all [swap]

/-- C++: symmetry.hh:260–264. A client transposition is its own inverse. -/
def transposition [DecidableEq α] (a b : α) : Perm α :=
  ⟨swap a b, swap a b, swap_swap a b, swap_swap a b⟩

/-- C++: equivariant_k_bounded_safety_aut.hh:57–68. Explicit finite enumeration. -/
def vectors (xs : List α) : (n : Nat) → List (Fin n → α)
  | 0 => [fun i => Fin.elim0 i]
  | n + 1 => xs.flatMap fun a => (vectors xs n).map fun f => Fin.cases a f

theorem mem_vectors (xs : List α) (full : ∀ x, x ∈ xs) (f : Fin n → α) :
    f ∈ vectors xs n := by
  induction n with
  | zero =>
    have : f = (fun i => Fin.elim0 i) := by funext i; exact Fin.elim0 i
    simp [vectors, this]
  | succ n ih =>
    have he : Fin.cases (f 0) (fun i : Fin n => f i.succ) = f := by
      funext i
      exact Fin.cases rfl (fun _ => rfl) i
    simp only [vectors, List.mem_flatMap, List.mem_map]
    exact ⟨f 0, full _, _, ih _, he⟩

/-- C++: equivariant_k_bounded_safety_aut.hh:57–68. Complete letters over the APs. -/
def letters (a : Nat) : List (Fin a → Bool) := vectors [false, true] a

theorem mem_letters (v : Fin a → Bool) : v ∈ letters a :=
  mem_vectors _ (by intro b; cases b <;> simp) v

/-- C++: symmetry.hh:112–117, 260–269. BDDs represented by Boolean formulas. -/
inductive Formula (a : Nat) where
  | top
  | atom (p : Fin a)
  | neg (f : Formula a)
  | conj (f g : Formula a)
  | disj (f g : Formula a)
  deriving DecidableEq

/-- C++: equivariant_k_bounded_safety_aut.hh:71–79. Interpret a label on a full letter. -/
def Formula.eval (v : Fin a → Bool) : Formula a → Bool
  | .top => true
  | .atom p => v p
  | .neg f => !(f.eval v)
  | .conj f g => f.eval v && g.eval v
  | .disj f g => f.eval v || g.eval v

/-- C++: symmetry.hh:260–269. Simultaneous AP substitution. -/
def Formula.rename (p : Fin a → Fin a) : Formula a → Formula a
  | .top => .top
  | .atom i => .atom (p i)
  | .neg f => .neg (f.rename p)
  | .conj f g => .conj (f.rename p) (g.rename p)
  | .disj f g => .disj (f.rename p) (g.rename p)

/-- C++: equivariant_k_bounded_safety_aut.hh:388–432. out[pi p] = in[p]. -/
def act (p : Perm α) (f : α → β) : α → β := fun q => f (p.invFun q)

@[simp] theorem act_apply (p : Perm α) (f : α → β) (q : α) : act p f (p q) = f q := by
  simp [act]

@[simp] theorem act_inverse (p : Perm α) (f : α → β) : act p.symm (act p f) = f := by
  funext x; simp [act, Perm.symm]

@[simp] theorem act_inverse' (p : Perm α) (f : α → β) : act p (act p.symm f) = f := by
  funext x; simp [act, Perm.symm]

@[simp] theorem act_id (f : α → β) : act Perm.id f = f := rfl

theorem act_comp (p q : Perm α) (f : α → β) :
    act (p.comp q) f = act p (act q f) := rfl

theorem eval_rename (p : Perm (Fin a)) (f : Formula a) (v : Fin a → Bool) :
    (f.rename p).eval (act p v) = f.eval v := by
  induction f <;> simp_all [Formula.rename, Formula.eval]

/-- C++: symmetry.hh:108–117. Acceptance is state based on this solver path. -/
structure Edge (n a : Nat) where
  src : Fin n
  label : Formula a
  dst : Fin n
  deriving DecidableEq

/-- C++: symmetry.hh:112–117; k_bounded_safety_aut.hh:101–110.
    `counting` records the bool_threshold split checked at equivariant:636–643. -/
structure Automaton (n a : Nat) where
  init : Fin n
  rejecting : Fin n → Bool
  counting : Fin n → Bool
  isInput : Fin a → Bool
  edges : List (Edge n a)

/-- C++: equivariant_k_bounded_safety_aut.hh:71–79. Enabled transition relation. -/
def Automaton.enabled (A : Automaton n a) (q r : Fin n) (v : Fin a → Bool) : Bool :=
  A.edges.any fun e => e.src == q && e.dst == r && e.label.eval v

theorem enabled_iff (A : Automaton n a) (q r : Fin n) (v : Fin a → Bool) :
    A.enabled q r v = true ↔ ∃ e ∈ A.edges, e.src = q ∧ e.dst = r ∧ e.label.eval v = true := by
  simp [Automaton.enabled, Bool.and_eq_true, beq_iff_eq, and_assoc]

end Symmetry

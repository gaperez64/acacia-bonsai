import Init

namespace Symmetry
structure Perm (α : Type) where -- Completed permutation and inverse; symmetry.hh:144–209.
  toFun : α → α
  invFun : α → α
  left_inv : ∀ x, invFun (toFun x) = x
  right_inv : ∀ x, toFun (invFun x) = x
instance : CoeFun (Perm α) (fun _ => α → α) := ⟨Perm.toFun⟩
@[simp] theorem Perm.left (p : Perm α) (x : α) : p.invFun (p x) = x := p.left_inv x
@[simp] theorem Perm.right (p : Perm α) (x : α) : p (p.invFun x) = x := p.right_inv x
theorem Perm.injective (p : Perm α) {x y : α} (h : p x = p y) : x = y := by
  have := congrArg p.invFun h; simpa using this
def Perm.symm (p : Perm α) : Perm α := ⟨p.invFun, p.toFun, p.right_inv, p.left_inv⟩
def Perm.comp (p q : Perm α) : Perm α where
  toFun x := p (q x)
  invFun x := q.invFun (p.invFun x)
  left_inv x := by simp
  right_inv x := by simp
def Perm.id : Perm α := ⟨(fun x => x), (fun x => x), fun _ => rfl, fun _ => rfl⟩
@[ext] theorem Perm.ext {p q : Perm α} (h : ∀ x, p x = q x) : p = q := by
  have hf : p.toFun = q.toFun := funext h
  have hi : p.invFun = q.invFun := by
    funext x; apply p.injective
    rw [p.right_inv, h, q.right_inv]
  cases p; cases q; simp_all
def swap [DecidableEq α] (a b : α) (x : α) : α :=
  if x = a then b else if x = b then a else x
@[simp] theorem swap_swap [DecidableEq α] (a b x : α) : swap a b (swap a b x) = x := by by_cases ha : x = a <;> by_cases hb : x = b <;> simp_all [swap]
def transposition [DecidableEq α] (a b : α) : Perm α :=
  ⟨swap a b, swap a b, swap_swap a b, swap_swap a b⟩
def vectors (xs : List α) : (n : Nat) → List (Fin n → α)
  | 0 => [fun i => Fin.elim0 i]
  | n + 1 => xs.flatMap fun a => (vectors xs n).map fun f => Fin.cases a f
theorem mem_vectors (xs : List α) (full : ∀ x, x ∈ xs) (f : Fin n → α) : f ∈ vectors xs n := by
  induction n with
  | zero =>
    have : f = (fun i => Fin.elim0 i) := by funext i; exact Fin.elim0 i
    simp [vectors, this]
  | succ n ih =>
    have he : Fin.cases (f 0) (fun i : Fin n => f i.succ) = f := by
      funext i; exact Fin.cases rfl (fun _ => rfl) i
    simp only [vectors, List.mem_flatMap, List.mem_map]; exact ⟨f 0, full _, _, ih _, he⟩
def letters (a : Nat) : List (Fin a → Bool) := vectors [false, true] a
theorem mem_letters (v : Fin a → Bool) : v ∈ letters a :=
  mem_vectors _ (by intro b; cases b <;> simp) v
inductive Formula (a : Nat) where -- Boolean label model; symmetry.hh:112–117.
  | top
  | atom (p : Fin a)
  | neg (f : Formula a)
  | conj (f g : Formula a)
  | disj (f g : Formula a)
  deriving DecidableEq
def Formula.eval (v : Fin a → Bool) : Formula a → Bool
  | .top => true
  | .atom p => v p
  | .neg f => !(f.eval v)
  | .conj f g => f.eval v && g.eval v
  | .disj f g => f.eval v || g.eval v
def Formula.rename (p : Fin a → Fin a) : Formula a → Formula a -- Simultaneous AP substitution; symmetry.hh:260–269.
  | .top => .top
  | .atom i => .atom (p i)
  | .neg f => .neg (f.rename p)
  | .conj f g => .conj (f.rename p) (g.rename p)
  | .disj f g => .disj (f.rename p) (g.rename p)
-- Send coordinate p to its renamed position; equivariant_k_bounded_safety_aut.hh:388–432.
def act (p : Perm α) (f : α → β) : α → β := fun q => f (p.invFun q)
@[simp] theorem act_apply (p : Perm α) (f : α → β) (q : α) : act p f (p q) = f q := by simp [act]
@[simp] theorem act_inverse (p : Perm α) (f : α → β) : act p.symm (act p f) = f := by funext x; simp [act, Perm.symm]
@[simp] theorem act_inverse' (p : Perm α) (f : α → β) : act p (act p.symm f) = f := by funext x; simp [act, Perm.symm]
@[simp] theorem act_id (f : α → β) : act Perm.id f = f := rfl
theorem act_comp (p q : Perm α) (f : α → β) : act (p.comp q) f = act p (act q f) := rfl
theorem eval_rename (p : Perm (Fin a)) (f : Formula a) (v : Fin a → Bool) :
    (f.rename p).eval (act p v) = f.eval v := by induction f <;> simp_all [Formula.rename, Formula.eval]
structure Edge (n a : Nat) where
  src : Fin n
  label : Formula a
  dst : Fin n
  deriving DecidableEq
structure Automaton (n a : Nat) where -- State acceptance, cap split and I/O graph; symmetry.hh:108–139.
  init : Fin n
  rejecting : Fin n → Bool
  counting : Fin n → Bool
  isInput : Fin a → Bool
  edges : List (Edge n a)
def Automaton.enabled (A : Automaton n a) (q r : Fin n) (v : Fin a → Bool) : Bool :=
  A.edges.any fun e => e.src == q && e.dst == r && e.label.eval v
theorem enabled_iff (A : Automaton n a) (q r : Fin n) (v : Fin a → Bool) :
    A.enabled q r v = true ↔ ∃ e ∈ A.edges, e.src = q ∧ e.dst = r ∧ e.label.eval v = true := by simp [Automaton.enabled, Bool.and_eq_true, beq_iff_eq, and_assoc]

def takeResponse (good : α → Bool) : List α → Option (α × List α) -- Extract the first surviving response for splicing; critical.hh:46–57.
  | [] => none
  | x :: xs => if good x then some (x, xs) else
      (takeResponse good xs).map fun (found, rest) => (found, x :: rest)
theorem takeResponse_none (good : α → Bool) (xs : List α) :
    takeResponse good xs = none ↔ ∀ x ∈ xs, good x = false := by
  induction xs with
  | nil => simp [takeResponse]
  | cons x xs ih => cases hx : good x <;> simp [takeResponse, hx, ih]
theorem takeResponse_exact (good : α → Bool) (xs : List α)
    (h : takeResponse good xs = some (found, rest)) : good found = true ∧ (found :: rest).Perm xs := by
  induction xs generalizing rest with
  | nil => simp [takeResponse] at h
  | cons x xs ih =>
    cases hx : good x with
    | true =>
      simp [takeResponse, hx] at h
      obtain ⟨rfl, rfl⟩ := h; exact ⟨hx, .refl _⟩
    | false =>
      cases ht : takeResponse good xs with
      | none => simp [takeResponse, hx, ht] at h
      | some pair =>
        rcases pair with ⟨f, rs⟩
        simp [takeResponse, hx, ht] at h
        obtain ⟨rfl, rfl⟩ := h
        obtain ⟨hf, hp⟩ := ih ht; exact ⟨hf, (List.Perm.swap _ _ _).trans (hp.cons x)⟩

end Symmetry

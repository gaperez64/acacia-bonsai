/-
Orbit quotients of turn-based games (#454, stage 3).

A group acts on the vertices of a game by automorphisms: it preserves owner,
priority and edges. `canon` picks a representative that depends only on the
orbit and is reached by an explicit transporter. The quotient game plays on
representatives: from `r` a move goes to `canon w` for a successor `w` of `r`.

A positional quotient strategy is a concrete successor of each representative.
Transporting it back gives a positional strategy of the original game, and if
the quotient strategy wins for a player from `canon v`, the transported one
wins for that player from `v`. This holds for any objective on the sequence of
priorities, parity included, and for both players, so whichever verdict a
quotient solver certifies with a strategy is the original game's: no vertex is
won by both players. Outputs on edges that the group preserves are emitted
unchanged, so a controller read off the transported strategy produces the
quotient strategy's outputs.
-/

namespace Symmetry.Quotient

structure Game (V : Type) where
  owner : V → Bool -- the player who moves at a vertex
  edge : V → V → Prop
  prio : V → Nat

/-- A positional strategy of player `p` moves along edges at `p`'s vertices. -/
def Legal (G : Game V) (p : Bool) (σ : V → V) : Prop :=
  ∀ v, G.owner v = p → G.edge v (σ v)

def IsPlay (G : Game V) (π : Nat → V) : Prop := ∀ k, G.edge (π k) (π (k + 1))

def Consistent (G : Game V) (p : Bool) (σ : V → V) (π : Nat → V) : Prop :=
  ∀ k, G.owner (π k) = p → π (k + 1) = σ (π k)

/-- Player `true` wins a play when `W` holds of its priorities, `false` otherwise. -/
def Goal (W : (Nat → Nat) → Prop) (p : Bool) (s : Nat → Nat) : Prop :=
  if p then W s else ¬ W s

def Wins (G : Game V) (W : (Nat → Nat) → Prop) (p : Bool) (σ : V → V) (v : V) : Prop :=
  Legal G p σ ∧ ∀ π, IsPlay G π → π 0 = v → Consistent G p σ π →
    Goal W p (fun k => G.prio (π k))

/-- A group acting by automorphisms, with orbit-invariant representatives. -/
structure Action (G : Game V) (Γ : Type) where
  act : Γ → V → V
  inv : Γ → Γ
  act_inv : ∀ g v, act (inv g) (act g v) = v
  owner_act : ∀ g v, G.owner (act g v) = G.owner v
  prio_act : ∀ g v, G.prio (act g v) = G.prio v
  edge_act : ∀ g v w, G.edge v w → G.edge (act g v) (act g w)
  transporter : V → Γ
  canon_orbit : ∀ g v, act (transporter (act g v)) (act g v) = act (transporter v) v

variable {G : Game V} (S : Action G Γ)

def Action.canon (v : V) : V := S.act (S.transporter v) v

theorem Action.canon_act (g : Γ) (v : V) : S.canon (S.act g v) = S.canon v :=
  S.canon_orbit g v

theorem Action.owner_canon (v : V) : G.owner (S.canon v) = G.owner v := S.owner_act _ _

theorem Action.prio_canon (v : V) : G.prio (S.canon v) = G.prio v := S.prio_act _ _

theorem Action.untransport (v : V) : S.act (S.inv (S.transporter v)) (S.canon v) = v :=
  S.act_inv _ _

/-- The quotient game: same owner and priority, moves between representatives. -/
def Action.quotient : Game V where
  owner := G.owner
  edge r r' := ∃ w, G.edge r w ∧ S.canon w = r'
  prio := G.prio

/-- Quotient consistency: at `p`'s vertices the play follows `canon ∘ σ'`. -/
def Action.QConsistent (p : Bool) (σ' : V → V) (ρ : Nat → V) : Prop :=
  ∀ k, G.owner (ρ k) = p → ρ (k + 1) = S.canon (σ' (ρ k))

/-- `σ'` wins the quotient from `r`: it moves along edges of the original game
at `p`'s vertices, and every quotient play from `r` that follows it is won. -/
def Action.QWins (W : (Nat → Nat) → Prop) (p : Bool) (σ' : V → V) (r : V) : Prop :=
  Legal G p σ' ∧ ∀ ρ, IsPlay S.quotient ρ → ρ 0 = r → S.QConsistent p σ' ρ →
    Goal W p (fun k => G.prio (ρ k))

/-- The transported strategy: carry `v` to its representative, follow `σ'` there,
and carry the chosen successor back. -/
def Action.lift (σ' : V → V) (v : V) : V :=
  S.act (S.inv (S.transporter v)) (σ' (S.canon v))

theorem Action.lift_legal {p : Bool} {σ' : V → V} (h : Legal G p σ') :
    Legal G p (S.lift σ') := by
  intro v hv
  have hc : G.edge (S.canon v) (σ' (S.canon v)) := h _ (by rw [S.owner_canon]; exact hv)
  have := S.edge_act (S.inv (S.transporter v)) _ _ hc
  rw [S.untransport] at this
  exact this

/-- A play of the original game projects to a play of the quotient. -/
theorem Action.project_play {π : Nat → V} (h : IsPlay G π) :
    IsPlay S.quotient (fun k => S.canon (π k)) := by
  intro k
  refine ⟨S.act (S.transporter (π k)) (π (k + 1)), S.edge_act _ _ _ (h k), ?_⟩
  exact S.canon_act _ _

/-- Following the transported strategy projects to following `σ'`. -/
theorem Action.project_consistent {p : Bool} {σ' : V → V} {π : Nat → V}
    (h : Consistent G p (S.lift σ') π) :
    S.QConsistent p σ' (fun k => S.canon (π k)) := by
  intro k hk
  have hk' : G.owner (π k) = p := by rw [← S.owner_canon]; exact hk
  show S.canon (π (k + 1)) = S.canon (σ' (S.canon (π k)))
  rw [h k hk']
  exact S.canon_act _ _

/-- Soundness of the quotient: a winning quotient strategy, transported, wins. -/
theorem Action.lift_wins {W : (Nat → Nat) → Prop} {p : Bool} {σ' : V → V} {v : V}
    (h : S.QWins W p σ' (S.canon v)) : Wins G W p (S.lift σ') v := by
  refine ⟨S.lift_legal h.1, fun π hπ h0 hc => ?_⟩
  have hg := h.2 _ (S.project_play hπ) (by simp [h0]) (S.project_consistent hc)
  have hs : (fun k => G.prio (S.canon (π k))) = (fun k => G.prio (π k)) := by
    funext k; exact S.prio_canon _
  simpa [hs] using hg

/-- The play from `v` that follows both players' positional strategies. -/
def joint (G : Game V) (σt σf : V → V) (v : V) : Nat → V
  | 0 => v
  | k + 1 => let u := joint G σt σf v k; if G.owner u then σt u else σf u

/-- No vertex is won by both players. -/
theorem exclusive {G : Game V} {W : (Nat → Nat) → Prop} {σt σf : V → V} {v : V}
    (ht : Wins G W true σt v) (hf : Wins G W false σf v) : False := by
  let π := joint G σt σf v
  have play : IsPlay G π := by
    intro k
    show G.edge (π k) (if G.owner (π k) then σt (π k) else σf (π k))
    cases ho : G.owner (π k)
    · simpa using hf.1 _ ho
    · simpa using ht.1 _ ho
  have ct : Consistent G true σt π := by
    intro k hk; show (if G.owner (π k) then σt (π k) else σf (π k)) = σt (π k); simp [hk]
  have cf : Consistent G false σf π := by
    intro k hk; show (if G.owner (π k) then σt (π k) else σf (π k)) = σf (π k); simp [hk]
  have gt := ht.2 π play rfl ct
  have gf := hf.2 π play rfl cf
  simp only [Goal] at gt gf
  exact gf gt

/-- The quotient's certified verdict is the original's: the winner of the
quotient from `canon v` wins from `v`, and the other player does not. -/
theorem Action.verdict {W : (Nat → Nat) → Prop} {p : Bool} {σ' : V → V} {v : V}
    (h : S.QWins W p σ' (S.canon v)) :
    Wins G W p (S.lift σ') v ∧ ∀ τ, ¬ Wins G W (!p) τ v := by
  refine ⟨S.lift_wins h, fun τ hτ => ?_⟩
  cases p
  · exact exclusive hτ (S.lift_wins h)
  · exact exclusive (S.lift_wins h) hτ

/-- Controller transport: an output on edges that the group preserves is the
same along the transported move as along the quotient move. -/
theorem Action.lift_output (out : V → V → O)
    (hout : ∀ g v w, out (S.act g v) (S.act g w) = out v w) (σ' : V → V) (v : V) :
    out v (S.lift σ' v) = out (S.canon v) (σ' (S.canon v)) := by
  have := hout (S.inv (S.transporter v)) (S.canon v) (σ' (S.canon v))
  rw [S.untransport] at this
  exact this

end Symmetry.Quotient

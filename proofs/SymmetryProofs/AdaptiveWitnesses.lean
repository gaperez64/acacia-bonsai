import SymmetryProofs.TableWitnesses
import SymmetryProofs.Witnesses

namespace Symmetry.AdaptiveWitnesses
open ActionTable

def letter : Letter 0 := fun p => Fin.elim0 p

/-- Two counting states and one rejecting step need K=2, rather than K=1. -/
def aut : Automaton 2 0 where
  init := 0
  rejecting := fun q => q = 1
  counting := fun _ => true
  isInput := fun p => Fin.elim0 p
  edges := [⟨0, .top, 1⟩]

def plan : SweepPlan aut false where
  generators := []
  orbits := [⟨letter, [Verified.identity aut]⟩]
  member_generated := by
    intro O ho s hs
    simp only [List.mem_singleton] at ho
    subst O
    simp only [List.mem_singleton] at hs
    subst s
    exact .identity
  orbit_closed := by simp
  covers := by
    intro i
    refine ⟨⟨letter, [Verified.identity aut]⟩, by simp, ?_⟩
    simp only [orbitLetters, List.map_cons, List.map_nil, List.mem_singleton]
    funext p
    exact Fin.elim0 p

def startMax : State 2 1 := fun _ => 1
def oldMax : State 2 1 := fun q => if q = 0 then 0 else 1

theorem start_region : downset [startMax] = safe aut 1 := by
  funext f
  simp only [downset, List.any_cons, List.any_nil, Bool.or_false, below, safe, aut, ↓reduceIte, startMax]
  rfl

theorem updated : downset [oldMax] = representativeUpdate 1 false plan.generators letter (downset [startMax]) := by
  unfold representativeUpdate close
  rw [iterate_of_fixed (closePass plan.generators) _ (by rfl)]
  funext f
  have h : ∀ x y : Fin 3, let f : State 2 1 := fun q => if q = 0 then x else y
      downset [oldMax] f =
        (downset [startMax] f && computeT aut 1 false (downset [startMax]) letter f) := by decide
  have he : f = (fun q => if q = 0 then f 0 else f 1) := by
    funext q
    have hq : q = 0 ∨ q = 1 := by omega
    rcases hq with rfl | rfl <;> rfl
  conv => lhs; rw [he]
  conv => rhs; rw [he]
  exact h (f 0) (f 1)

theorem old_contains : Subset (final aut 1 false) (downset [oldMax]) := by
  rw [updated]
  exact selected_preserves 1 false [] letter _ (by rw [start_region]; exact stage_safe 1 false _)

/-- Lowering, membership, symmetry and safety are all instantiated on a
    post-refutation region. The new initial vector is restored by widening. -/
theorem raised :
    Subset (final aut 2 false) (downset ([oldMax].map (bump aut 1 1))) ∧
    Subset (downset ([oldMax].map (bump aut 1 1))) (safe aut 2) ∧
    Symmetric (A := aut) [] (downset ([oldMax].map (bump aut 1 1))) :=
  bound_raise_invariant 1 1 (by decide) false [] [oldMax] old_contains (by decide) (by simp [Symmetric])

theorem restored : downset ([oldMax].map (bump aut 1 1)) (initial aut 2) = true :=
  raised_initial 1 1 (by decide) (by decide) false [oldMax] old_contains

theorem reached : AdaptiveReach aut false plan.generators 1 (downset [startMax]) := by
  rw [start_region]
  exact .start 1 (by decide)

theorem raised_reach : AdaptiveReach aut false plan.generators 2 (downset ([oldMax].map (bump aut 1 1))) :=
  .raise reached letter [oldMax] updated (by decide) 1 (by decide)

def table : List (Row 2 0) := precomputerTable (precomputerDiagram aut false) [letter]

theorem refines : Refines aut false (plan.orbits.map InputOrbit.representative) table :=
  precomputer_refinement aut false (precomputerDiagram aut false) (precomputer_denotes aut false) _

def selectedRow : Row 2 0 := table[0]'(by decide)

theorem physical_updated : downset [oldMax] = update aut 1 plan.generators [startMax] selectedRow := by
  obtain ⟨i, _, _, he⟩ := selected_refinement aut 1 (by decide) false plan.generators [startMax]
    (by decide) _ table refines selectedRow (by decide)
  have hi : i = letter := funext fun p => Fin.elim0 p
  rw [hi] at he
  exact updated.trans he.symm

/-- The source's shared k += kinc branch reuses the physical post-update
    region, and the initial-out branch at the limit is also instantiated. -/
theorem stopping_branches :
    (sharedNext 1 2 1 = some 2 ∧
      AdaptiveReach aut false plan.generators 2 (downset ([oldMax].map (bump aut 1 1)))) ∧
    winner aut 1 false = false := by
  refine ⟨shared_loop_raise false plan [startMax] reached table refines selectedRow (by decide)
    [oldMax] physical_updated (by decide) 2 1 (by decide) (by decide), ?_⟩
  apply adaptive_stopped false plan [startMax] reached table refines selectedRow (by decide)
    ?_ 1 (by decide) (by decide)
  rw [← physical_updated]
  decide

/-- The actual no-input branch after a warm start returns the new-bound
    winner, through the grouped and deduplicated input/output table. -/
theorem completed :
    downset ([oldMax].map (bump aut 1 1)) = final aut 2 false ∧ winner aut 2 false = true := by
  apply adaptive_completed (by intro q r v _ h; contradiction) false plan _ raised_reach table refines
  decide

theorem sweep_updated : downset [oldMax] = orbitUpdate aut 1 false
    ⟨letter, [Verified.identity aut]⟩ (downset [startMax]) := by
  rw [updated]
  unfold representativeUpdate close
  rw [iterate_of_fixed (closePass plan.generators) _ (by rfl)]
  rfl

theorem sweep_raised_reach : AdaptiveReach aut false plan.generators 2
    (downset ([oldMax].map (bump aut 1 1))) :=
  .orbit_raise reached plan rfl ⟨letter, [Verified.identity aut]⟩ (by simp [plan])
    [oldMax] sweep_updated (by decide) 1 (by decide)

/-- The sweep branch reuses a post-orbit region too; unchanged-round success
    and the intervening initial-out branch both instantiate its history proof. -/
theorem sweep_branches :
    (downset ([oldMax].map (bump aut 1 1)) = final aut 2 false ∧ winner aut 2 false = true) ∧
    winner aut 1 false = false := by
  refine ⟨adaptive_sweep_completed false plan _ sweep_raised_reach ?_, ?_⟩
  · funext f
    have he : ∀ f, sweep aut 2 false plan (downset ([oldMax].map (bump aut 1 1))) f =
        downset ([oldMax].map (bump aut 1 1)) f := by decide
    exact he f
  · apply adaptive_orbit_out false plan _ reached ⟨letter, [Verified.identity aut]⟩
    rw [← sweep_updated]
    decide

/-- The unchanged-rank mutant is not a sound warm start: it omits the new
    winning initial vector. Positive K alone does not justify region reuse. -/
theorem no_lift_mutant :
    winner aut 2 false = true ∧ downset ([oldMax].map (embed 1 1)) (initial aut 2) = false :=
  ⟨completed.2, by decide⟩

/-- A bound raise and its winner embeddings work on explicit rank vectors. -/
theorem simulations :
    lower 1 1 (initial aut 2) = absent 2 1 ∧
    below (succ aut 1 (lower 1 1 (initial aut 2)) letter)
      (lower 1 1 (succ aut 2 (initial aut 2) letter)) = true ∧
    winner aut 3 false = true :=
  ⟨by decide, lower_succ 1 1 _ _, winner_mono 2 1 (by decide) false completed.2⟩

/-- The shared schedule overshoots; the ordinary schedule clamps, and its
    actual delta is positive. In-range steps agree. -/
theorem schedules :
    sharedNext 1 2 2 = some 3 ∧ ordinaryNext 1 2 2 = some 2 ∧
    sharedNext 1 3 1 = ordinaryNext 1 3 1 ∧
    1 ≤ min 2 (1 + 2) - 1 := by decide
/-- A nonidentity state/AP swap survives a raise of mixed Boolean/counting
    maxima. Nonuniform coordinate widening would destroy that symmetry. -/
theorem symmetric_raise :
    let ms : List (State 3 1) := [fun _ => 1]
    Subset (final Witnesses.aut 2 false) (downset (ms.map (bump Witnesses.aut 1 1))) ∧
    Subset (downset (ms.map (bump Witnesses.aut 1 1))) (safe Witnesses.aut 2) ∧
    Symmetric [Witnesses.sym] (downset (ms.map (bump Witnesses.aut 1 1))) := by
  have he : downset ([fun _ => 1] : List (State 3 1)) = safe Witnesses.aut 1 := by
    funext f
    simp [downset, below, safe, Witnesses.aut]
  apply bound_raise_invariant 1 1 (by decide) false [Witnesses.sym] _
  · rw [he]; exact stage_safe 1 false _
  · decide
  · rw [he]
    intro s hs
    simp only [List.mem_singleton] at hs
    subst s
    exact safe_commutes Witnesses.sym 1

theorem nonuniform_mutant :
    ¬ Invariant Witnesses.sym.states (downset ([fun q => if q = 1 then 2 else 1] : List (State 3 2))) := by
  intro h
  have he := h (fun q => if q = 1 then 2 else 1)
  have hn : downset ([fun q => if q = 1 then 2 else 1] : List (State 3 2))
      (act Witnesses.sym.states (fun q => if q = 1 then 2 else 1)) ≠ true := by decide
  exact hn he

/-- Three acyclic states need K=3: two rejecting destinations increment a
    live initial rank twice. This exposes the schedule's verdict limitation. -/
def chain : Automaton 3 0 where
  init := 0
  rejecting := fun q => q != 0
  counting := fun _ => true
  isInput := fun p => Fin.elim0 p
  edges := [⟨0, .top, 1⟩, ⟨1, .top, 2⟩]

def chainMax : State 3 3 := fun q => ⟨q.val + 1, by have := q.isLt; omega⟩

theorem chain_wins : winner chain 3 false = true := by
  have hs : Subset (downset [chainMax]) (safe chain 3) := by
    apply downset_safe
    intro m hm
    simp only [List.mem_singleton] at hm
    subst m
    decide
  have hm : below (succ chain 3 chainMax letter) chainMax = true := by decide
  have hp : Subset (downset [chainMax]) (cpre chain 3 false (downset [chainMax])) := by
    intro f hf
    apply (cpre_iff _ _ _ _ _).mpr
    refine ⟨hf, fun i => ⟨letter, ?_⟩⟩
    have hi : merge chain false i letter = letter := funext fun p => Fin.elim0 p
    rw [hi]
    have hb : below f chainMax = true := by simpa [downset] using hf
    exact List.any_eq_true.mpr ⟨chainMax, by simp, below_trans (succ_mono 3 f chainMax hb letter) hm⟩
  exact final_greatest 3 false _ hs hp _ (by decide)

private theorem chain_next (f : State 3 2) (hf : final chain 2 false f = true) :
    final chain 2 false (succ chain 2 f letter) = true := by
  have hp : cpre chain 2 false (final chain 2 false) f = true := by rw [final_fixed]; exact hf
  obtain ⟨o, ho⟩ := ((cpre_iff _ _ _ _ _).mp hp).2 letter
  have he : merge chain false letter o = letter := funext fun p => Fin.elim0 p
  simpa only [he] using ho

theorem chain_loses : winner chain 2 false = false := by
  cases hw : winner chain 2 false with
  | false => rfl
  | true =>
    have hh := chain_next _ (chain_next _ hw)
    have hs := stage_safe (A := chain) 2 false _ _ hh
    have hn : safe chain 2 (succ chain 2 (succ chain 2 (initial chain 2) letter) letter) = false := by decide
    rw [hn] at hs
    contradiction

/-- Mutant of an equality-at-kmax claim: shared succeeds at overshot K=3,
    whereas the ordinary schedule stops losing at its clamped K=2. -/
theorem schedule_verdict_mutant :
    sharedNext 1 2 2 = some 3 ∧ ordinaryNext 1 2 2 = some 2 ∧
    winner chain 3 false = true ∧ winner chain 2 false = false :=
  ⟨rfl, rfl, chain_wins, chain_loses⟩

end Symmetry.AdaptiveWitnesses

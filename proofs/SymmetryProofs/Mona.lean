import SymmetryProofs.ActionTable

namespace Symmetry.ActionTable
-- Ordered BDD paths have satisfiable cubes; this is the abstract shape requirement.
def Diagram.Realizable (D : Diagram a α) : Prop := ∀ row ∈ D.rows, ∃ i, row.1 i = true
private theorem tabulate_paths [DecidableEq α] (F : Letter a → α) (ps : List (Fin a))
    (base : Letter a) (distinct : ps.Nodup) : ∀ row ∈ (tabulate F ps base).rows,
    ∃ i, row.1 i = true ∧ ∀ q, q ∉ ps → i q = base q := by
  induction ps generalizing base with
  | nil =>
    intro row hr; simp only [tabulate, Diagram.rows, List.mem_singleton] at hr; subst row
    exact ⟨base, rfl, fun _ _ => rfl⟩
  | cons p ps ih =>
    have hn := List.nodup_cons.mp distinct
    have reach : ∀ b, ∀ row ∈ (tabulate F ps (assign base p b)).rows,
        ∃ i, row.1 i = true ∧ i p = b ∧ ∀ q, q ∉ p :: ps → i q = base q := by
      intro b row hr; obtain ⟨i, hc, hi⟩ := ih _ hn.2 row hr
      refine ⟨i, hc, by simpa [assign] using hi p hn.1, ?_⟩
      intro q hq; simp only [List.mem_cons, not_or] at hq
      simpa [assign, hq.1] using hi q hq.2
    simp only [tabulate]; split
    · intro row hr; obtain ⟨i, hc, _, hi⟩ := reach false row hr; exact ⟨i, hc, hi⟩
    · intro row hr; rcases List.mem_append.mp hr with hr | hr
      · obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
        obtain ⟨i, hc, hp, hi⟩ := reach false r hm; exact ⟨i, by simp [hp, hc], hi⟩
      · obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
        obtain ⟨i, hc, hp, hi⟩ := reach true r hm; exact ⟨i, by simp [hp, hc], hi⟩
theorem tabulate_realizable [DecidableEq α] (F : Letter a → α) (ps : List (Fin a))
    (base : Letter a) (distinct : ps.Nodup) : (tabulate F ps base).Realizable := by
  intro row hr; obtain ⟨i, hi, _⟩ := tabulate_paths F ps base distinct row hr; exact ⟨i, hi⟩
-- High-first output walk emits every terminal, including the empty action; mona.hh:142–165.
def outputPaths : Diagram a α → List α
  | .leaf t => [t]
  | .branch _ l h => outputPaths h ++ outputPaths l
private theorem outputPaths_eq (D : Diagram a α) :
    outputPaths D = (D.rows.map Prod.snd).reverse := by
  induction D <;> simp_all [outputPaths, Diagram.rows, List.map_map, Function.comp_def]
theorem mem_outputPaths (D : Diagram a (Action n)) (valid : D.Realizable) (t : Action n) :
    t ∈ outputPaths D ↔ ∃ o, D.eval o = t := by
  rw [outputPaths_eq]; simp only [List.mem_reverse, List.mem_map]
  constructor
  · rintro ⟨row, hr, rfl⟩; obtain ⟨o, ho⟩ := valid row hr
    exact ⟨o, (diagram_rows D).2 row hr o ho⟩
  · rintro ⟨o, he⟩; obtain ⟨row, hr, hc⟩ := (diagram_rows D).1 o
    exact ⟨row, hr, ((diagram_rows D).2 row hr o hc).symm.trans he⟩
-- eraseDups keeps first occurrences; each input starts with a fresh set; mona.hh:146–184.
def monaOutputs (quotient : Bool) (D : Diagram a (Action n)) : List (Action n) :=
  if quotient then (outputPaths D).eraseDups else outputPaths D
theorem mem_monaOutputs (quotient : Bool) (D : Diagram a (Action n)) (valid : D.Realizable)
    (t : Action n) : t ∈ monaOutputs quotient D ↔ ∃ o, D.eval o = t := by
  cases quotient <;> simp [monaOutputs, mem_outputPaths D valid t]
-- Diagram.rows is the low-first input walk, including false residuals; mona.hh:167–190.
def monaRows (quotient : Bool) (D : Diagram a (Diagram a (Action n))) : List (Row n a) :=
  D.rows.map fun row => (row.1, monaOutputs quotient row.2)
def monaTable (quotient : Bool) (D : Diagram a (Diagram a (Action n)))
    (reps : List (Letter a)) : List (Row n a) := dedup (restrict reps (monaRows quotient D))
theorem mona_refinement (A : Automaton n a) (side : Bool) (quotient : Bool)
    (D : Diagram a (Diagram a (Action n)))
    (valid : ∀ row ∈ D.rows, row.2.Realizable)
    (denotes : ∀ i o, (D.eval i).eval o = action A (merge A side i o)) (reps : List (Letter a)) :
    Refines A side reps (monaTable quotient D reps) := by
  have complete : ∀ i, ∃ row ∈ monaRows quotient D, row.1 i = true := by
    intro i; obtain ⟨row, hr, hc⟩ := (diagram_rows D).1 i
    exact ⟨_, List.mem_map.mpr ⟨row, hr, rfl⟩, hc⟩
  have exactClass : ∀ row ∈ monaRows quotient D, ∀ i, row.1 i = true → Class A side row.2 i := by
    intro row hr i hi t; obtain ⟨r, hm, rfl⟩ := List.mem_map.mp hr
    rw [mem_monaOutputs quotient r.2 (valid r hm), ← (diagram_rows D).2 r hm i hi]
    simp only [denotes]
  constructor
  · intro i hi; obtain ⟨row, hr, hc⟩ := complete i
    have hm : row ∈ restrict reps (monaRows quotient D) :=
      List.mem_filter.mpr ⟨hr, List.any_eq_true.mpr ⟨i, hi, hc⟩⟩
    obtain ⟨kept, hk, he⟩ := (dedup_refines _).2 row hm
    exact ⟨kept, hk, he ▸ exactClass row hr i hc⟩
  · intro row hr; obtain ⟨hm, hc⟩ := List.mem_filter.mp ((dedup_refines _).1 row hr)
    obtain ⟨i, hi, hc⟩ := List.any_eq_true.mp hc; exact ⟨i, hi, hc, exactClass row hm i hc⟩
def Diagram.map (f : α → β) : Diagram a α → Diagram a β
  | .leaf t => .leaf (f t)
  | .branch p l h => .branch p (l.map f) (h.map f)
private theorem outputPaths_map (D : Diagram a α) (f : α → β) :
    outputPaths (D.map f) = (outputPaths D).map f := by
  induction D <;> simp_all [Diagram.map, outputPaths]
-- None is an unencoded false node; some t is an injective terminal ID, even for empty t.
-- Skip false nodes in both explicit visitors; mona.hh:406–437.
def explicitRows : Diagram a (Diagram a (Option (Action n))) → List (Row n a)
  | .leaf (.leaf none) => []
  | .leaf residual => [(fun _ => true, ((outputPaths residual).filterMap id).eraseDups)]
  | .branch p l h =>
      ((explicitRows l).map fun row => (fun i => !i p && row.1 i, row.2)) ++
      ((explicitRows h).map fun row => (fun i => i p && row.1 i, row.2))
-- Fixed interning encodes every letter's endpoint row; mona.hh:384–404.
theorem explicit_rows (D : Diagram a (Diagram a (Action n))) :
    explicitRows (D.map (Diagram.map some)) = monaRows true D := by
  induction D with
  | leaf residual =>
    cases residual <;> simp [Diagram.map, explicitRows, monaRows, monaOutputs,
      Diagram.rows, outputPaths_map, outputPaths]
  | branch p l h ihl ihh =>
    simp_all [Diagram.map, explicitRows, monaRows, Diagram.rows, List.map_map, Function.comp_def]
-- tabulate reduces equal cofactors; encoding retains empty terminals; mona.hh:384–437.
def explicitTable (A : Automaton n a) (side : Bool) (reps : List (Letter a)) : List (Row n a) :=
  dedup (restrict reps (explicitRows ((precomputerDiagram A side).map (Diagram.map some))))
theorem precomputer_realizable (A : Automaton n a) (side : Bool) :
    ∀ row ∈ (precomputerDiagram A side).rows, row.2.Realizable := by
  intro row hr
  have hin : (precomputerDiagram A side).Realizable :=
    tabulate_realizable _ _ _ (List.nodup_finRange a)
  obtain ⟨i, hi⟩ := hin row hr
  rw [← (diagram_rows _).2 row hr i hi]
  unfold precomputerDiagram; rw [tabulate_eval _ _ _ i (by simp)]
  exact tabulate_realizable _ _ _ (List.nodup_finRange a)
theorem explicit_refinement (A : Automaton n a) (side : Bool) (reps : List (Letter a)) :
    Refines A side reps (explicitTable A side reps) := by
  simpa only [explicitTable, explicit_rows, monaTable] using
    mona_refinement A side true _ (precomputer_realizable A side) (precomputer_denotes A side) reps
-- Both symbolic modes and the explicit path supply the existing completion contract.
theorem mona_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K)) (reached : PickerReach A K side P.generators (downset maxima))
    (quotient : Bool) (D : Diagram a (Diagram a (Action n)))
    (valid : ∀ row ∈ D.rows, row.2.Realizable)
    (denotes : ∀ i o, (D.eval i).eval o = action A (merge A side i o))
    (exhausted : (criticalCached (applyAction A K) maxima
      (monaTable quotient D (P.orbits.map InputOrbit.representative))).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true :=
  refined_completed A closed K positive side P maxima (picker_reach_invariants reached) _
    (mona_refinement A side quotient D valid denotes _) exhausted
theorem explicit_completed (A : Automaton n a) (closed : BooleanClosed A)
    (K : Nat) (positive : 1 ≤ K) (side : Bool) (P : SweepPlan A side)
    (maxima : List (State n K)) (reached : PickerReach A K side P.generators (downset maxima))
    (exhausted : (criticalCached (applyAction A K) maxima
      (explicitTable A side (P.orbits.map InputOrbit.representative))).1 = none) :
    downset maxima = final A K side ∧ winner A K side = true := by
  apply mona_completed A closed K positive side P maxima reached true _
    (precomputer_realizable A side) (precomputer_denotes A side)
  simpa only [explicitTable, explicit_rows, monaTable] using exhausted

end Symmetry.ActionTable

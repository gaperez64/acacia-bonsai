import SymmetryProofs.NextPermutation
import SymmetryProofs.LayoutBuilder
import SymmetryProofs.Solver

namespace Symmetry.Builders
variable {A : Automaton n a}

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:81–121. Indexed AP
    families share the recovered client slots; #330 checks each family's side. -/
structure FamilyLayout (A : Automaton n a) (clients nf : Nat) where
  layout : BlockLayout a clients nf
  owner : Fin nf → Bool
  pure_family : ∀ b j, A.isInput (layout.state b j) = owner b

/-- C++: solver/symmetry.hh:310–335; equivariant:81–105. The AP dictionary's
    distinct family/index entries, before the whole-family partition check.
    Distinct AP identifiers are the dictionary/container boundary. -/
structure FamilyTable (a clients : Nat) where
  rows : List (Bool × (Fin clients → Fin a))
  unique : ((rows.map Prod.snd).flatMap List.ofFn).Nodup

/-- C++: solver/symmetry.hh:326–335; equivariant:81–105. Check every family
    member's side, and derive both AP lookup directions from the raw table. -/
def buildFamilies (A : Automaton n a) (raw : FamilyTable a clients) :
    Option (Sigma fun nf => FamilyLayout A clients nf) :=
  if h : raw.rows.all (fun row => (List.finRange clients).all fun j => A.isInput (row.2 j) == row.1) = true
    then some ⟨(raw.rows.map Prod.snd).length,
      layoutOfRows (raw.rows.map Prod.snd) raw.unique,
      (fun b => (raw.rows[b.val]'(by simpa using b.isLt)).1), by
        intro b j
        have hb := List.all_eq_true.mp h (raw.rows[b.val]'(by simpa using b.isLt))
          (List.getElem_mem ..)
        have hj := beq_iff_eq.mp (List.all_eq_true.mp hb j (List.mem_finRange _))
        simpa only [layoutOfRows, List.getElem_map] using hj⟩
    else none

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:85–105. Keep precisely
    the indexed families on the current engine's opponent side. -/
def FamilyLayout.active (F : FamilyLayout A clients nf) (side : Bool) : List (Fin nf) :=
  (List.finRange nf).filter fun b => F.owner b != side

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:108–121. Shared opponent
    variables remain explicit; indexed and protagonist variables are excluded. -/
def FamilyLayout.shared (F : FamilyLayout A clients nf) (side : Bool) : List (Fin a) :=
  (List.finRange a).filter fun p => A.isInput p != side && (F.layout.locate p).isNone

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:151–193.
    Read family bits from each slot type, then the explicit shared mask. -/
def FamilyLayout.letter (F : FamilyLayout A clients nf) (side : Bool)
    (types : Fin clients → Nat) (sharedMask : Nat) : Fin a → Bool := fun p =>
  if A.isInput p = side then false else match F.layout.locate p with
    | none => sharedMask.testBit ((F.shared side).idxOf p)
    | some (b, j) => (types j).testBit ((F.active side).idxOf b)

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:151–172. Pack the
    Boolean family bits into the unsigned type's little-endian positions. -/
def packBits (bits : List Bool) : Nat := (BitVec.ofBoolListLE bits).toNat

theorem packBits_bound (bits : List Bool) : packBits bits < 2 ^ bits.length :=
  (BitVec.ofBoolListLE bits).isLt

theorem packBits_bit (bits : List Bool) (i : Nat) (h : i < bits.length) :
    (packBits bits).testBit i = bits[i] := by
  change (BitVec.ofBoolListLE bits).getLsbD i = _
  rw [BitVec.getLsbD_ofBoolListLE]
  simp [List.getD, h]

/-- Every full input valuation is encoded by the bounded slot types and shared
    mask that the C++ loops enumerate. Protagonist bits are projected away. -/
theorem letter_covers (F : FamilyLayout A clients nf) (side : Bool) (i : Fin a → Bool) :
    F.letter side
      (fun j => packBits ((F.active side).map fun b => i (F.layout.state b j)))
      (packBits ((F.shared side).map i)) = opponentProjection A side i := by
  funext p
  by_cases hp : A.isInput p = side
  · simp [FamilyLayout.letter, opponentProjection, hp]
  · simp only [FamilyLayout.letter, opponentProjection, hp, ↓reduceIte]
    cases hl : F.layout.locate p with
    | none =>
      have hm : p ∈ F.shared side := by simp [FamilyLayout.shared, hp, hl]
      have hi := List.idxOf_lt_length_of_mem hm
      rw [packBits_bit _ _ (by simpa using hi), List.getElem_map, List.getElem_idxOf hi]
    | some bj =>
      rcases bj with ⟨b, j⟩
      have he := F.layout.state_locate p b j hl
      have hm : b ∈ F.active side := by
        have howner := F.pure_family b j
        rw [he] at howner
        simp [FamilyLayout.active, ← howner, hp]
      have hi := List.idxOf_lt_length_of_mem hm
      dsimp only
      rw [packBits_bit _ _ (by simpa using hi), List.getElem_map, List.getElem_idxOf hi, he]

/-- Layout-induced transport acts only on client positions in the encoded
    input. Shared bits and family identities stay fixed in either engine frame. -/
theorem letter_transport (F : FamilyLayout A clients nf) (side : Bool)
    (sigma : Perm (Fin clients)) (types : Fin clients → Nat) (mask : Nat) :
    act (F.layout.phiFromSigma sigma) (F.letter side types mask) =
      F.letter side (act sigma types) mask := by
  funext p
  cases hl : F.layout.locate p with
  | none => simp [act, BlockLayout.phiFromSigma, FamilyLayout.letter, hl]
  | some bj =>
    rcases bj with ⟨b, j⟩
    have he := F.layout.state_locate p b j hl
    have hs := F.pure_family b (sigma.invFun j)
    have hp := F.pure_family b j
    simp only [act, BlockLayout.phiFromSigma, hl]
    simp only [FamilyLayout.letter, F.layout.locate_state, hs, ← he, hp]
    rfl

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:734–736.
    Obtain the total stable matcher from its proved equal-multiset contract. -/
def matchOfPerm (canonical member : Fin clients → Nat)
    (h : (List.ofFn canonical).Perm (List.ofFn member)) : Perm (Fin clients) :=
  match he : matchSlots canonical member with
  | some sigma => sigma
  | none => False.elim (by
      obtain ⟨sigma, hs⟩ := matchSlots_complete canonical member h
      rw [he] at hs
      contradiction)

theorem matchOfPerm_types (canonical member : Fin clients → Nat)
    (h : (List.ofFn canonical).Perm (List.ofFn member)) :
    act (matchOfPerm canonical member h) canonical = member := by
  have hs : matchSlots canonical member = some (matchOfPerm canonical member h) := by
    unfold matchOfPerm
    split
    · assumption
    · next he =>
        obtain ⟨sigma, hs⟩ := matchSlots_complete canonical member h
        rw [he] at hs
        contradiction
  funext j
  have ht := matchSlots_types canonical member hs ((matchOfPerm canonical member h).invFun j)
  simpa [act] using ht.symm

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:374–383.
    The raw maps are executable. Existing star-generation proofs supply only
    erased automorphism evidence, without searching for a certificate. -/
def layoutTransport (S : BlockLayout n clients bs) (I : BlockLayout a clients bi)
    (G : List (Verified A)) (pivot : Fin clients)
    (stars : ∀ j, j ≠ pivot → ∃ s ∈ G,
      s.states = S.phiFromSigma (transposition pivot j) ∧
      s.aps = I.phiFromSigma (transposition pivot j)) (sigma : Perm (Fin clients)) : Verified A where
  states := S.phiFromSigma sigma
  aps := I.phiFromSigma sigma
  init_fixed := by
    obtain ⟨s, hs, _⟩ := generated_verified (phiFromSigma_generated S I G pivot stars sigma)
    rw [← hs]; exact s.init_fixed
  acceptance := by
    obtain ⟨s, hs, _⟩ := generated_verified (phiFromSigma_generated S I G pivot stars sigma)
    rw [← hs]; exact s.acceptance
  partition := by
    obtain ⟨s, _, hi⟩ := generated_verified (phiFromSigma_generated S I G pivot stars sigma)
    rw [← hi]; exact s.partition
  caps := by
    obtain ⟨s, hs, _⟩ := generated_verified (phiFromSigma_generated S I G pivot stars sigma)
    rw [← hs]; exact s.caps
  forward := by
    obtain ⟨s, hs, hi⟩ := generated_verified (phiFromSigma_generated S I G pivot stars sigma)
    rw [← hs, ← hi]; exact s.forward

/-- C++: solver/symmetry.hh:552–589; symmetry_blocks.hh:274–302.
    Successful detector stars, in the recovered slot frame. AP maps come from
    the detector's within-family transposition construction; states are checked
    by generators_match_layout before a plan is built. -/
structure LayoutGroup (A : Automaton n a) (clients bs nf : Nat) where
  states : BlockLayout n clients bs
  families : FamilyLayout A clients nf
  pivot : Fin clients
  star : Fin clients → Verified A
  ap_star : ∀ j, j ≠ pivot → (star j).aps = families.layout.phiFromSigma (transposition pivot j)
  state_star : ∀ j, j ≠ pivot → (star j).states = states.phiFromSigma (transposition pivot j)
  slotOrder : Perm (Fin clients) := Perm.id

/-- C++: solver/symmetry.hh:568–569. Retain the original stars only. -/
def LayoutGroup.generators (D : LayoutGroup A clients bs nf) : List (Verified A) :=
  (((List.finRange clients).map D.slotOrder).filter fun j => j != D.pivot).map D.star

theorem LayoutGroup.stars (D : LayoutGroup A clients bs nf) :
    ∀ j, j ≠ D.pivot → ∃ s ∈ D.generators,
      s.states = D.states.phiFromSigma (transposition D.pivot j) ∧
      s.aps = D.families.layout.phiFromSigma (transposition D.pivot j) := by
  intro j hj
  have hm : j ∈ (List.finRange clients).map D.slotOrder :=
    List.mem_map.mpr ⟨D.slotOrder.invFun j, List.mem_finRange _, D.slotOrder.right_inv j⟩
  exact ⟨D.star j, List.mem_map.mpr ⟨j, by simp [hj, hm], rfl⟩,
    D.state_star j hj, D.ap_star j hj⟩

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:253–256.
    A canonical count distribution with its proved client count. -/
def canonicalTypes (types clients : Nat) (counts : List Nat)
    (h : counts ∈ typeCounts types clients) : Fin clients → Nat := fun j =>
  (expandCounts (List.range types) counts)[j.val]'(by
    have hc := (mem_typeCounts _ _ _).mp h
    rw [expandCounts_length _ _ (by simp [hc.1]), hc.2]
    exact j.isLt)

theorem canonicalTypes_list (types clients : Nat) (counts : List Nat)
    (h : counts ∈ typeCounts types clients) :
    List.ofFn (canonicalTypes types clients counts h) = expandCounts (List.range types) counts := by
  apply List.ext_getElem
  · have hc := (mem_typeCounts _ _ _).mp h
    rw [List.length_ofFn, expandCounts_length (List.range types) counts (by simpa using hc.1.symm), hc.2]
  · intro i hi hj; simp [canonicalTypes]

theorem canonicalTypes_sorted (types clients : Nat) (counts : List Nat)
    (h : counts ∈ typeCounts types clients) :
    (List.ofFn (canonicalTypes types clients counts h)).Pairwise (· ≤ ·) := by
  rw [canonicalTypes_list]
  apply expandCounts_sorted
  apply List.pairwise_iff_getElem.mpr
  intro i j hi hj hij
  simpa using Nat.le_of_lt hij

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:731–734. A traversal
    member has the same length as its canonical sequence. -/
def memberTypes (canonical : Fin clients → Nat) (word : List Nat)
    (h : word ∈ permutations (List.ofFn canonical)) : Fin clients → Nat := fun j =>
  word[j.val]'(by
    have := ((permutationWalk_member _ _ _ h).1).length_eq
    simp only [List.length_ofFn] at this
    omega)

theorem memberTypes_list (canonical : Fin clients → Nat) (word : List Nat)
    (h : word ∈ permutations (List.ofFn canonical)) :
    List.ofFn (memberTypes canonical word h) = word := by
  apply List.ext_getElem
  · have := ((permutationWalk_member _ _ _ h).1).length_eq
    simpa using this
  · intro i hi hj; simp [memberTypes]

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:734–736.
    Stable member matching followed by phi_from_sigma on states and APs. -/
def builtMember (D : LayoutGroup A clients bs nf) (canonical : Fin clients → Nat)
    (word : List Nat) (h : word ∈ permutations (List.ofFn canonical)) : Verified A :=
  layoutTransport D.states D.families.layout D.generators D.pivot D.stars
    (matchOfPerm canonical (memberTypes canonical word h) (by
      rw [memberTypes_list]
      exact (permutationWalk_member _ _ _ h).1))

theorem builtMember_letter (D : LayoutGroup A clients bs nf) (side : Bool)
    (canonical : Fin clients → Nat) (mask : Nat) (word : List Nat)
    (h : word ∈ permutations (List.ofFn canonical)) :
    act (builtMember D canonical word h).aps (D.families.letter side canonical mask) =
      D.families.letter side (memberTypes canonical word h) mask := by
  change act (D.families.layout.phiFromSigma _) _ = _
  rw [letter_transport, matchOfPerm_types]

theorem letter_projected (F : FamilyLayout A clients nf) (side : Bool)
    (types : Fin clients → Nat) (mask : Nat) :
    opponentProjection A side (F.letter side types mask) = F.letter side types mask := by
  funext p
  by_cases hp : A.isInput p = side <;> simp [opponentProjection, FamilyLayout.letter, hp]

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:249–270,727–744.
    Build one orbit from a canonical type distribution and shared mask. -/
def builtOrbit (D : LayoutGroup A clients bs nf) (side : Bool) (mask : Nat)
    (counts : List Nat) (h : counts ∈ typeCounts (2 ^ (D.families.active side).length) clients) :
    InputOrbit A :=
  let canonical := canonicalTypes _ _ counts h
  ⟨D.families.letter side canonical mask,
    (permutations (List.ofFn canonical)).attach.map fun w => builtMember D canonical w.val w.property⟩

theorem builtOrbit_letters (D : LayoutGroup A clients bs nf) (side : Bool) (mask : Nat)
    (counts : List Nat) (h : counts ∈ typeCounts (2 ^ (D.families.active side).length) clients)
    (i : Fin a → Bool) :
    i ∈ orbitLetters side (builtOrbit D side mask counts h) ↔
      ∃ word, ∃ hw : word ∈ permutations (List.ofFn (canonicalTypes _ _ counts h)),
        D.families.letter side (memberTypes (canonicalTypes _ _ counts h) word hw) mask = i := by
  simp only [orbitLetters, builtOrbit, List.mem_map, List.mem_attach, true_and]
  constructor
  · rintro ⟨s, ⟨w, rfl⟩, he⟩
    exact ⟨w.val, w.property, by simpa [builtMember_letter, letter_projected] using he⟩
  · rintro ⟨w, hw, he⟩
    refine ⟨builtMember D _ w hw, ⟨⟨w, hw⟩, rfl⟩, ?_⟩
    simpa [builtMember_letter, letter_projected] using he

theorem ofFn_act_perm (p : Perm (Fin clients)) (f : Fin clients → Nat) :
    (List.ofFn (act p f)).Perm (List.ofFn f) := by
  have hp : ((List.finRange clients).map p.invFun).Perm (List.finRange clients) := by
    apply (List.perm_ext_iff_of_nodup
      ((List.nodup_finRange _).map _ (fun _ _ hne he => hne (p.symm.injective he)))
      (List.nodup_finRange _)).mpr
    intro j
    simp only [List.mem_finRange, iff_true, List.mem_map]
    exact ⟨p j, trivial, p.left_inv j⟩
  have hm := hp.map f
  change (List.ofFn fun x => f (p.invFun x)).Perm (List.ofFn fun x => f x)
  simpa [List.finRange, List.map_map, Function.comp_def] using hm

theorem builtOrbit_closed (D : LayoutGroup A clients bs nf) (side : Bool) (mask : Nat)
    (counts : List Nat) (h : counts ∈ typeCounts (2 ^ (D.families.active side).length) clients)
    (s : Verified A) (hs : s ∈ D.generators)
    (i : Fin a → Bool) (hi : i ∈ orbitLetters side (builtOrbit D side mask counts h)) :
    act s.aps i ∈ orbitLetters side (builtOrbit D side mask counts h) := by
  obtain ⟨j, hj, rfl⟩ := List.mem_map.mp hs
  have hj' : j ≠ D.pivot := by simpa using (List.mem_filter.mp hj).2
  rw [D.ap_star j hj']
  obtain ⟨word, hw, rfl⟩ := (builtOrbit_letters D side mask counts h i).mp hi
  let canonical := canonicalTypes _ _ counts h
  let member := memberTypes canonical word hw
  let tau := transposition D.pivot j
  have hp : (List.ofFn canonical).Perm (List.ofFn (act tau member)) :=
    ((permutationWalk_member _ _ _ hw).1).trans
      ((memberTypes_list canonical word hw) ▸ (ofFn_act_perm tau member).symm)
  have hm : List.ofFn (act tau member) ∈ permutations (List.ofFn canonical) :=
    (traversal_complete _ _ (canonicalTypes_sorted _ _ counts h)).mpr hp
  apply (builtOrbit_letters D side mask counts h _).mpr
  refine ⟨List.ofFn (act tau member), hm, ?_⟩
  have he : memberTypes canonical (List.ofFn (act tau member)) hm = act tau member := by
    funext q; simp [memberTypes]
  rw [he, letter_transport]

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:249–270,318–331.
    Shared masks are the outer loop; ascending count distributions are inner. -/
def builtOrbits (D : LayoutGroup A clients bs nf) (side : Bool) : List (InputOrbit A) :=
  (List.range (2 ^ (D.families.shared side).length)).flatMap fun mask =>
    (typeCounts (2 ^ (D.families.active side).length) clients).attach.map fun cs =>
      builtOrbit D side mask cs.val cs.property

theorem builtOrbits_member (D : LayoutGroup A clients bs nf) (side : Bool) (O : InputOrbit A)
    (h : O ∈ builtOrbits D side) :
    ∃ mask, mask < 2 ^ (D.families.shared side).length ∧
      ∃ counts, ∃ hc : counts ∈ typeCounts (2 ^ (D.families.active side).length) clients,
        O = builtOrbit D side mask counts hc := by
  obtain ⟨mask, hm, ho⟩ := List.mem_flatMap.mp h
  obtain ⟨cs, _, rfl⟩ := List.mem_map.mp ho
  exact ⟨mask, List.mem_range.mp hm, cs.val, cs.property, rfl⟩

/-- The count/shared-bit builder covers every projected opponent input, in
    either protagonist frame. Its stable transports, rather than hand-written
    witnesses, supply the member containing that input. -/
theorem builtOrbits_cover (D : LayoutGroup A clients bs nf) (side : Bool) (i : Fin a → Bool) :
    ∃ O ∈ builtOrbits D side, opponentProjection A side i ∈ orbitLetters side O := by
  let types := fun j => packBits ((D.families.active side).map fun b => i (D.families.layout.state b j))
  let mask := packBits ((D.families.shared side).map i)
  obtain ⟨counts, hc, hp⟩ := counts_cover (2 ^ (D.families.active side).length) clients
    (List.ofFn types) (by simp) (by
      intro x hx
      obtain ⟨j, rfl⟩ := List.mem_ofFn.mp hx
      simpa using packBits_bound ((D.families.active side).map fun b => i (D.families.layout.state b j)))
  let canonical := canonicalTypes _ _ counts hc
  have hp' : (List.ofFn canonical).Perm (List.ofFn types) := by
    rw [canonicalTypes_list]; exact hp
  have hw : List.ofFn types ∈ permutations (List.ofFn canonical) :=
    (traversal_complete _ _ (canonicalTypes_sorted _ _ counts hc)).mpr hp'
  refine ⟨builtOrbit D side mask counts hc, ?_, ?_⟩
  · apply List.mem_flatMap.mpr
    refine ⟨mask, List.mem_range.mpr ?_, List.mem_map.mpr ⟨⟨counts, hc⟩, by simp, rfl⟩⟩
    simpa using packBits_bound ((D.families.shared side).map i)
  · apply (builtOrbit_letters D side mask counts hc _).mpr
    refine ⟨List.ofFn types, hw, ?_⟩
    have he : memberTypes canonical (List.ofFn types) hw = types := by
      funext q; simp [memberTypes]
    rw [he]
    exact letter_covers D.families side i

/-- All SweepPlan premises follow from the executable enumeration and the
    recognized layout/star checks. Coverage and orbit closure are proved here,
    rather than required from the caller. -/
def builtPlan (D : LayoutGroup A clients bs nf) (side : Bool) : SweepPlan A side where
  generators := D.generators
  orbits := builtOrbits D side
  member_generated := by
    intro O ho s hs
    obtain ⟨mask, _, counts, hc, rfl⟩ := builtOrbits_member D side O ho
    obtain ⟨word, _, rfl⟩ := List.mem_map.mp hs
    exact phiFromSigma_generated D.states D.families.layout D.generators D.pivot D.stars _
  orbit_closed := by
    intro O ho s hs i hi
    obtain ⟨mask, _, counts, hc, rfl⟩ := builtOrbits_member D side O ho
    exact builtOrbit_closed D side mask counts hc s hs i hi
  covers := builtOrbits_cover D side

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:225–247,300–316.
    The extra count is a sentinel: hitting it declines, never truncates a
    successful plan. BDD response support size is supplied at that boundary. -/
def buildPlan (D : LayoutGroup A clients bs nf) (side : Bool) (responseBits : Nat) :
    Option (SweepPlan A side) :=
  let familyBits := (D.families.active side).length
  let sharedBits := (D.families.shared side).length
  let counts := (typeCounts (2 ^ familyBits) clients).take 4097
  if clients < 3 || bs < 2 || familyBits = 0 || familyBits ≥ 32 || sharedBits ≥ 20 ||
      counts.length > 4096 || 2 ^ sharedBits * counts.length > 4096 ||
      (clients ≤ 4 && 2 ^ responseBits > 4096)
    then none else some (builtPlan D side)

/-- C++: solver/symmetry_blocks.hh:274–302; equivariant:846–848.
    Check every state coordinate of each stored star against the expected
    block transposition. AP agreement is from source family construction. -/
def checkLayoutGroup (S : BlockLayout n clients bs) (F : FamilyLayout A clients nf)
    (pivot : Fin clients) (star : Fin clients → Verified A)
    (apStar : ∀ j, j ≠ pivot → (star j).aps = F.layout.phiFromSigma (transposition pivot j))
    (order : Perm (Fin clients) := Perm.id) :
    Option (LayoutGroup A clients bs nf) :=
  if h : ∀ j, j ≠ pivot → ∀ q, (star j).states q = S.phiFromSigma (transposition pivot j) q
    then some ⟨S, F, pivot, star, apStar, fun j hj => Perm.ext (h j hj), order⟩ else none

/-- C++: solver/symmetry.hh:552–589,260–264. Successful star detections in
    the original AP-index frame, before recovering state slots. The AP map is
    the one constructed from these families, rather than a nominated map. -/
structure SourceStars (A : Automaton n a) (clients nf : Nat) where
  families : FamilyLayout A clients nf
  pivot : Fin clients
  star : Fin clients → Verified A
  ap_star : ∀ j, j ≠ pivot → (star j).aps = families.layout.phiFromSigma (transposition pivot j)

/-- C++: solver/symmetry.hh:252–277,564–569. Source AP nomination swaps the
    same two client indices in every family and fixes other APs. -/
def sourceCandidate (F : FamilyLayout A clients nf) (pivot j : Fin clients)
    (raw : Candidate n a) : Candidate n a :=
  let pi := F.layout.phiFromSigma (transposition pivot j)
  { raw with
    pi := pi
    piInv := pi.invFun
    families := (List.finRange nf).map fun b => (F.owner b, List.ofFn (F.layout.state b)) }

/-- C++: solver/symmetry.hh:564–569. Port the completed successful detector
    stars; only checked candidates become verified generators. The chosen
    client domain contains the root and its successful leaves. -/
def buildSourceStars (F : FamilyLayout A clients nf) (pivot : Fin clients)
    (raw : Fin clients → Candidate n a) : Option (SourceStars A clients nf) :=
  if h : ∀ j, j ≠ pivot → check A (sourceCandidate F pivot j (raw j)) = true then
    some ⟨F, pivot,
      (fun j => if hj : j ≠ pivot then
        accepted_sound A (sourceCandidate F pivot j (raw j)) (h j hj) else Verified.identity A), by
        intro j hj
        rw [dite_eq_left hj]
        apply Perm.ext
        intro p
        rfl⟩
  else none

/-- C++: solver/symmetry.hh:560–569. Preserve successful stars in their
    original client-index order. The root's dummy entry is never stored. -/
def SourceStars.generators (source : SourceStars A clients nf) : List (Verified A) :=
  ((List.finRange clients).filter fun j => j != source.pivot).map source.star

/-- C++: solver/symmetry_blocks.hh:146–152. AP-index stabilizer signatures
    use precisely the verified generator pairs, in generator order. -/
def SourceStars.indexSignature (source : SourceStars A clients nf) (k : Fin clients) : Nat :=
  packBits (((List.finRange clients).filter fun j => j != source.pivot).map fun j =>
    decide (k ≠ source.pivot ∧ k ≠ j))

/-- C++: solver/symmetry_blocks.hh:148–185,274–302. Reconcile AP-index and
    state-slot frames, then perform the source's generator/layout check. -/
def alignGroup (source : SourceStars A clients nf) (L : AlignedLayout n clients) :
    Option (LayoutGroup A clients L.blocks nf) :=
  let F : FamilyLayout A clients nf :=
    ⟨reindexLayout source.families.layout L.slotOfIndex, source.families.owner,
      fun b j => source.families.pure_family b (L.slotOfIndex.invFun j)⟩
  checkLayoutGroup L.states F (L.slotOfIndex source.pivot)
    (fun j => source.star (L.slotOfIndex.invFun j)) (by
      intro j hj
      have hi : L.slotOfIndex.invFun j ≠ source.pivot := by
        intro he
        apply hj
        rw [← L.slotOfIndex.right_inv j, he]
      rw [source.ap_star _ hi]
      have he := reindexLayout_swap source.families.layout L.slotOfIndex
        source.pivot (L.slotOfIndex.invFun j)
      simpa only [L.slotOfIndex.right_inv] using he.symm) L.slotOfIndex

/-- Slot alignment preserves the literal stored generator list and its order.
    Closure still uses exactly the generators supplied by the C++ detector. -/
theorem alignGroup_generators (source : SourceStars A clients nf) (L : AlignedLayout n clients)
    {D : LayoutGroup A clients L.blocks nf}
    (h : alignGroup source L = some D) : D.generators = source.generators := by
  unfold alignGroup checkLayoutGroup at h
  dsimp only at h
  split at h
  · simp only [Option.some.injEq] at h
    subst D
    simp only [LayoutGroup.generators, SourceStars.generators, List.filter_map, List.map_map,
      Function.comp_def, L.slotOfIndex.left_inv]
    congr 1
    apply List.filter_congr
    intro j _
    have he : L.slotOfIndex j = L.slotOfIndex source.pivot ↔ j = source.pivot :=
      ⟨L.slotOfIndex.injective, congrArg L.slotOfIndex⟩
    apply Bool.eq_iff_iff.mpr
    simpa only [bne_iff_ne] using not_congr he
  · contradiction

/-- Alignment changes client slot names only; family owners and the
    reindexed AP table are exactly the fields used by the enumeration caps. -/
theorem alignGroup_families (source : SourceStars A clients nf) (L : AlignedLayout n clients)
    {D : LayoutGroup A clients L.blocks nf} (h : alignGroup source L = some D) :
    D.families.layout = reindexLayout source.families.layout L.slotOfIndex ∧
      D.families.owner = source.families.owner := by
  unfold alignGroup checkLayoutGroup at h
  dsimp only at h
  split at h
  · simp only [Option.some.injEq] at h
    subst D
    exact ⟨rfl, rfl⟩
  · contradiction

/-- A successful capped count enumeration contains the entire enumeration.
    The sentinel cannot conceal a missing distribution. -/
theorem count_cap_complete (xs : List α) (h : (xs.take 4097).length ≤ 4096) :
    xs.take 4097 = xs := by
  have hl : xs.length ≤ 4096 := by
    simp only [List.length_take] at h
    omega
  exact List.take_of_length_le (by omega)

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:71–79,190–201.
    The destination row records enabled source states and acceptance increments. -/
def responseAction (A : Automaton n a) (v : Fin a → Bool) : List (List (Fin n × Bool)) :=
  (List.finRange n).map fun r => (List.finRange n).filterMap fun q =>
    if A.enabled q r v then some (q, A.rejecting r) else none

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:258–266.
    Enumerate every response and deduplicate identical action vectors. -/
def responseActions (A : Automaton n a) (side : Bool) (i : Fin a → Bool) :
    List (List (List (Fin n × Bool))) :=
  unique ((letters a).map fun o => responseAction A (merge A side i o))

/-- Deduplication drops no response and adds no incompatible action. This is
    the complete-action premise used when relating forward picker membership
    to the representative backward union. -/
theorem responseActions_complete (A : Automaton n a) (side : Bool) (i : Fin a → Bool) (action) :
    action ∈ responseActions A side i ↔ ∃ o, responseAction A (merge A side i o) = action := by
  simp only [responseActions, mem_unique, List.mem_map]
  exact ⟨fun ⟨o, _, he⟩ => ⟨o, he⟩, fun ⟨o, he⟩ => ⟨o, mem_letters o, he⟩⟩

theorem responseAction_edge (A : Automaton n a) (v : Fin a → Bool) (q r : Fin n) :
    (q, A.rejecting r) ∈ (responseAction A v)[r.val]'(by simp [responseAction]) ↔
      A.enabled q r v = true := by
  simp only [responseAction, List.getElem_map, List.getElem_finRange]
  change (q, A.rejecting r) ∈ ((List.finRange n).filterMap fun p =>
    if A.enabled p r v then some (p, A.rejecting r) else none) ↔ A.enabled q r v = true
  simp only [List.mem_filterMap, Option.ite_none_right_eq_some, Option.some.injEq,
    Prod.mk.injEq, and_true]
  constructor
  · rintro ⟨p, _, hp, hq⟩
    subst p
    exact hp
  · intro h
    exact ⟨q, List.mem_finRange _, h, rfl⟩

/-- Equal deduplicated action rows have equal scalar successors. Combined
    with backward_preimage, replacing response letters by action vectors loses
    neither the picker's test nor any backward predecessor. -/
theorem responseAction_succ (A : Automaton n a) (K : Nat) (f : State n K) (v w : Fin a → Bool)
    (same : responseAction A v = responseAction A w) : succ A K f v = succ A K f w := by
  have enabled : ∀ q r, A.enabled q r v = A.enabled q r w := by
    intro q r
    apply Bool.eq_iff_iff.mpr
    rw [← responseAction_edge A v q r, ← responseAction_edge A w q r]
    simp only [same]
  funext r
  apply Fin.ext
  simp only [succ, contribution, enabled]

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:831–869.
    The shared builder starts with source stars, recognizes and aligns their
    layout, checks the generator maps, then builds the count/shared-bit plan. -/
def buildSharedPlan (source : SourceStars A clients nf) (side : Bool) (responseBits : Nat) :
    Option (SweepPlan A side) := do
  let layout ← computeBlockLayout (source.generators.map Verified.states) source.indexSignature
  let group ← alignGroup source layout
  buildPlan group side responseBits

/-- C++: solver/equivariant_k_bounded_safety_aut.hh:800–869.
    Start from dictionary tables and completed detector candidates, so no
    inverse-layout or generated-transport proof is required as an input. -/
def buildReduction (A : Automaton n a) (families : FamilyTable a clients)
    (pivot : Fin clients) (candidates : Fin clients → Candidate n a) (side : Bool) (responseBits : Nat) :
    Option (SweepPlan A side) := do
  let F ← buildFamilies A families
  let source ← buildSourceStars F.2 pivot candidates
  buildSharedPlan source side responseBits

/-- Successful stages compose into the actual builder result. This lets
    concrete checks reuse their proved tables without re-running earlier stages. -/
theorem reduction_of_stages (A : Automaton n a) (families : FamilyTable a clients)
    (pivot : Fin clients) (candidates : Fin clients → Candidate n a) (side : Bool) (responseBits : Nat)
    (F : Sigma fun nf => FamilyLayout A clients nf) (source : SourceStars A clients F.1)
    (L : AlignedLayout n clients) (D : LayoutGroup A clients L.blocks F.1)
    (hf : buildFamilies A families = some F)
    (hs : buildSourceStars F.2 pivot candidates = some source)
    (hl : computeBlockLayout (source.generators.map Verified.states) source.indexSignature = some L)
    (hd : alignGroup source L = some D) (hc : (buildPlan D side responseBits).isSome = true) :
    buildReduction A families pivot candidates side responseBits = some (builtPlan D side) := by
  unfold buildReduction
  rw [hf]
  change (do let source ← buildSourceStars F.2 pivot candidates
             buildSharedPlan source side responseBits) = _
  rw [hs]
  change buildSharedPlan source side responseBits = _
  unfold buildSharedPlan
  rw [hl]
  change (do let group ← alignGroup source L
             buildPlan group side responseBits) = _
  rw [hd]
  change buildPlan D side responseBits = _
  unfold buildPlan at hc ⊢
  dsimp only at hc ⊢
  split <;> simp_all <;> omega

/-- Every successful builder result satisfies the final-set and verdict
    theorems with no extra SweepPlan premises. -/
theorem builder_final_eq (D : LayoutGroup A clients bs nf) (side : Bool) (responseBits K : Nat)
    {P : SweepPlan A side} (h : buildPlan D side responseBits = some P) :
    sweepFinal A K side P = final A K side ∧
      representativeFinal K side P = final A K side := by
  unfold buildPlan at h
  dsimp only at h
  split at h
  · contradiction
  · simp only [Option.some.injEq] at h
    subst P
    exact ⟨sweepFinal_eq K side _, representativeFinal_eq K side _⟩

/-- Successful recognition and enumeration supply every plan premise. These
    final-set equalities therefore need no caller-provided SweepPlan evidence. -/
theorem sharedBuilder_final_eq (source : SourceStars A clients nf) (side : Bool) (responseBits K : Nat)
    {P : SweepPlan A side} (h : buildSharedPlan source side responseBits = some P) :
    sweepFinal A K side P = final A K side ∧
      representativeFinal K side P = final A K side := by
  have hs : ∃ D : Sigma fun bs => LayoutGroup A clients bs nf,
      buildPlan D.2 side responseBits = some P := by
    unfold buildSharedPlan at h
    cases hl : computeBlockLayout (source.generators.map Verified.states) source.indexSignature with
    | none => simp [hl] at h
    | some layout =>
      cases hg : alignGroup source layout with
      | none => simp [hl, hg] at h
      | some group => exact ⟨⟨layout.blocks, group⟩, by simpa [hl, hg] using h⟩
  obtain ⟨D, hd⟩ := hs
  exact builder_final_eq D.2 side responseBits K hd

/-- A successful shared builder preserves the detector's generator list,
    including its order. Recovered slot names do not change closure execution. -/
theorem sharedBuilder_generators (source : SourceStars A clients nf) (side : Bool) (responseBits : Nat)
    {P : SweepPlan A side} (h : buildSharedPlan source side responseBits = some P) :
    P.generators = source.generators := by
  unfold buildSharedPlan at h
  cases hl : computeBlockLayout (source.generators.map Verified.states) source.indexSignature with
  | none => simp [hl] at h
  | some L =>
    cases hg : alignGroup source L with
    | none => simp [hl, hg] at h
    | some D =>
      have hp : buildPlan D side responseBits = some P := by simpa [hl, hg] using h
      unfold buildPlan at hp
      dsimp only at hp
      split at hp
      · contradiction
      · simp only [Option.some.injEq] at hp
        subst P
        exact alignGroup_generators source L hg

/-- Every successfully built reduction has the plain final region and both
    verdicts, starting from raw tables rather than assumed SweepPlan premises. -/
theorem reduction_final_eq (A : Automaton n a) (families : FamilyTable a clients)
    (pivot : Fin clients) (candidates : Fin clients → Candidate n a) (side : Bool) (responseBits K : Nat)
    {P : SweepPlan A side} (h : buildReduction A families pivot candidates side responseBits = some P) :
    sweepFinal A K side P = final A K side ∧ representativeFinal K side P = final A K side ∧
      sweepFinal A K side P (initial A K) = winner A K side ∧
      representativeFinal K side P (initial A K) = winner A K side := by
  have hp : ∃ source : Sigma fun nf => SourceStars A clients nf,
      buildSharedPlan source.2 side responseBits = some P := by
    unfold buildReduction at h
    cases hf : buildFamilies A families with
    | none => simp [hf] at h
    | some F =>
      cases hs : buildSourceStars F.2 pivot candidates with
      | none => simp [hf, hs] at h
      | some source => exact ⟨⟨F.1, source⟩, by simpa [hf, hs] using h⟩
  obtain ⟨source, hs⟩ := hp
  obtain ⟨hsweep, hrep⟩ := sharedBuilder_final_eq source.2 side responseBits K hs
  exact ⟨hsweep, hrep, (verdicts_equal K side P).1, (verdicts_equal K side P).2⟩


end Symmetry.Builders

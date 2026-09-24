"""Semantic BDD coordinate layout and owner indexing."""
from __future__ import annotations

import dataclasses
import hashlib
import itertools
import json
from collections import defaultdict
from collections.abc import Iterable

from .buddy_veccompose import BUDDY_MAX_VARIABLE_COUNT

BDD_COORDINATE_LIMIT = BUDDY_MAX_VARIABLE_COUNT

@dataclasses.dataclass
class VarInfo:
    index: int
    key: tuple
    owners: frozenset[int]

    @property
    def canonical_owners(self) -> tuple[int, ...]:
        """Return provenance client IDs in the owner-index key format."""
        return tuple(sorted(self.owners))


class OwnerIndex:
    """Attempt-local owner tuple to sorted public-variable index."""

    def __init__(self, variables: Iterable[VarInfo]):
        grouped: dict[tuple[int, ...], list[int]] = defaultdict(list)
        self.variables_by_id: dict[int, VarInfo] = {}
        for item in variables:
            if item.index < 0:
                raise ValueError("public variable IDs must be non-negative")
            if item.index in self.variables_by_id:
                raise ValueError(f"duplicate public variable ID {item.index}")
            self.variables_by_id[item.index] = item
            grouped[item.canonical_owners].append(item.index)
        self.owner_groups = {
            owners: tuple(sorted(public_ids))
            for owners, public_ids in grouped.items()
        }
        self.shared_variables = self.owner_groups.get((), ())

    def keep_ids(self, subset: Iterable[int]) -> tuple[int, ...]:
        """Assemble keep(S) with 2^|S| indexed lookups, in public-ID order."""
        selected = tuple(sorted(set(subset)))
        public_ids = []
        for arity in range(len(selected) + 1):
            for owners in itertools.combinations(selected, arity):
                public_ids.extend(self.owner_groups.get(owners, ()))
        return tuple(sorted(public_ids))

    def keep_items(self, subset: Iterable[int]) -> tuple[VarInfo, ...]:
        return tuple(
            self.variables_by_id[public_id]
            for public_id in self.keep_ids(subset)
        )


@dataclasses.dataclass(frozen=True)
class VariableBlock:
    """A checked half-open interval of semantic BDD coordinates."""

    name: str
    start: int
    size: int

    def __post_init__(self) -> None:
        if self.start < 0 or self.size < 0:
            raise ValueError(f"negative BDD variable block {self.name}")
        if self.end > BDD_COORDINATE_LIMIT:
            raise OverflowError(f"BDD variable block {self.name} overflows")

    @property
    def end(self) -> int:
        return self.start + self.size

    def coordinate(self, offset: int) -> int:
        if not 0 <= offset < self.size:
            raise IndexError(f"{self.name} offset {offset} is outside [0, {self.size})")
        return self.start + offset


@dataclasses.dataclass(frozen=True)
class VariableLayout:
    """All manager coordinates owned by one generalization attempt."""

    public: VariableBlock
    composition: VariableBlock
    policy_counter: VariableBlock
    policy_game: VariableBlock
    canonical_templates: VariableBlock
    public_state_count: int
    public_letter_count: int
    composition_state_count: int
    composition_letter_count: int
    variable_limit: int | None = None

    def __post_init__(self) -> None:
        blocks = (
            self.public,
            self.composition,
            self.policy_counter,
            self.policy_game,
            self.canonical_templates,
        )
        for previous, current in itertools.pairwise(sorted(
                blocks, key=lambda block: (block.start, block.end, block.name))):
            if current.start < previous.end:
                raise ValueError(
                    f"BDD variable blocks overlap: {previous.name} and {current.name}"
                )
        if self.public_state_count < 0 or self.public_letter_count < 0:
            raise ValueError("public state/letter counts must be non-negative")
        if self.public_state_count + self.public_letter_count > self.public.size:
            raise ValueError("public state/letter coordinates exceed their block")
        if self.composition_state_count < 0 or self.composition_letter_count < 0:
            raise ValueError("composition counts must be non-negative")
        expected_composition = (
            2 * self.composition_state_count + self.composition_letter_count
        )
        if expected_composition != self.composition.size:
            raise ValueError("composition block does not match state/letter counts")
        if self.policy_game.size != self.public_state_count + self.public_letter_count:
            raise ValueError("policy game block does not match the public target ABI")
        if self.variable_limit is not None:
            if self.variable_limit < 0:
                raise ValueError("BDD variable limit must be non-negative")
            if self.required_variables > self.variable_limit:
                raise OverflowError(
                    "attempt variable layout requires "
                    f"{self.required_variables} variables, limit is {self.variable_limit}"
                )

    @classmethod
    def plan(cls, *, public_variables: int, public_states: int,
             public_letters: int, composition_states: int,
             composition_letters: int, policy_counters: int,
             canonical_templates: int,
             variable_limit: int | None = None) -> "VariableLayout":
        counts = {
            "public_variables": public_variables,
            "public_states": public_states,
            "public_letters": public_letters,
            "composition_states": composition_states,
            "composition_letters": composition_letters,
            "policy_counters": policy_counters,
            "canonical_templates": canonical_templates,
        }
        if any(value < 0 for value in counts.values()):
            raise ValueError(f"negative variable-layout count: {counts}")
        composition_size = cls._checked_add(
            2 * composition_states, composition_letters)
        policy_game_size = cls._checked_add(public_states, public_letters)
        cursor = 0

        def allocate(name: str, size: int) -> VariableBlock:
            nonlocal cursor
            block = VariableBlock(name, cursor, size)
            cursor = cls._checked_add(cursor, size)
            return block

        return cls(
            public=allocate("public", public_variables),
            composition=allocate("composition", composition_size),
            # Counter variables deliberately precede the policy game variables:
            # the goal mux should branch before the larger game relation.
            policy_counter=allocate("policy_counter", policy_counters),
            policy_game=allocate("policy_game", policy_game_size),
            canonical_templates=allocate(
                "canonical_templates", canonical_templates),
            public_state_count=public_states,
            public_letter_count=public_letters,
            composition_state_count=composition_states,
            composition_letter_count=composition_letters,
            variable_limit=variable_limit,
        )

    @staticmethod
    def _checked_add(left: int, right: int) -> int:
        result = left + right
        if result > BDD_COORDINATE_LIMIT:
            raise OverflowError("BDD variable coordinate arithmetic overflow")
        return result

    @property
    def required_variables(self) -> int:
        return max(
            block.end for block in (
                self.public,
                self.composition,
                self.policy_counter,
                self.policy_game,
                self.canonical_templates,
            )
        )

    @property
    def identity(self) -> tuple:
        return (
            tuple((block.name, block.start, block.size) for block in (
                self.public,
                self.composition,
                self.policy_counter,
                self.policy_game,
                self.canonical_templates,
            )),
            self.public_state_count,
            self.public_letter_count,
            self.composition_state_count,
            self.composition_letter_count,
        )

    @property
    def digest(self) -> str:
        payload = json.dumps(self.identity, separators=(",", ":"))
        return hashlib.sha256(payload.encode("utf-8")).hexdigest()

    def description(self) -> dict[str, object]:
        return {
            "sha256": self.digest,
            "required_variables": self.required_variables,
            "variable_limit": self.variable_limit,
            "blocks": {
                block.name: {"start": block.start, "size": block.size}
                for block in (
                    self.public,
                    self.composition,
                    self.policy_counter,
                    self.policy_game,
                    self.canonical_templates,
                )
            },
            "public_state_count": self.public_state_count,
            "public_letter_count": self.public_letter_count,
        }

    def composition_current(self, state: int) -> int:
        if not 0 <= state < self.composition_state_count:
            raise IndexError(state)
        return self.composition.coordinate(2 * state)

    def composition_temporary(self, state: int) -> int:
        if not 0 <= state < self.composition_state_count:
            raise IndexError(state)
        return self.composition.coordinate(2 * state + 1)

    def composition_letter(self, letter: int) -> int:
        if not 0 <= letter < self.composition_letter_count:
            raise IndexError(letter)
        return self.composition.coordinate(
            2 * self.composition_state_count + letter)

    def public_state(self, state: int) -> int:
        if not 0 <= state < self.public_state_count:
            raise IndexError(state)
        return self.public.coordinate(state)

    def public_letter(self, letter: int) -> int:
        if not 0 <= letter < self.public_letter_count:
            raise IndexError(letter)
        return self.public.coordinate(self.public_state_count + letter)


"""Build-free regression lint for accidental or casual solver hardcoding.

Any corpus basename, instance or family stem, or digest in a code literal or
solver-path text data file is rejected regardless of its C/C++ context. Generic
family words are exempt only as identifiers and comments. Secondary checks look
for literal source-name comparisons and simple environment/hash verdict gates.
This is not a defense against deliberately disguised constants (characters
assembled one by one, encodings, computed hashes); the renamed-copy metamorphic
audit (hc-meta) tests behavior for those. It does not expand function macros,
follow calls or pointers, or prove arbitrary control flow. Generated headers are
covered through their scanned ``.in`` templates.
"""

from __future__ import annotations

import ast
from collections import defaultdict
from dataclasses import dataclass
from functools import lru_cache
import hashlib
import io
import json
from pathlib import Path
import re
import tokenize

import pytest

from test_acacia_lift_generic_guard import FAMILY_LABELS, folded_string


ROOT = Path(__file__).resolve().parents[2]
BENCHMARKS = ROOT / "tests/suites/benchmarks"
CORPUS = ROOT / "tlsf-corpus"
CORPUS_DIRS = (CORPUS, ROOT / "tests/ltl", ROOT / "tests/syntcomp-benchmarks/tlsf")
INCLUDE_DIRS = (
    "src", "subprojects/tlsf-tools/src/lib", "subprojects/tlsf-tools/include",
    "subprojects/posets/include", "subprojects/posets/lib",
)
SOURCE_DIRS = INCLUDE_DIRS
CODE_TREES = ("src", "subprojects/tlsf-tools/src", "subprojects/posets/lib")
CODE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inc", ".in"})
# Exact files belonging to other targets. New files enter the scan by default.
EXCLUDED = {
    **{name: "Python interface module, not linked into acacia-bonsai" for name in (
        "src/python/python_interface.cc", "src/python/python_interface.hh")},
    "src/native_test_hooks.cc": "native-test executable only",
    **{name: "research executable or its private header, not linked into acacia-bonsai"
       for name in (
           "src/research/all_input_actions.hh", "src/research/antichain_replay.cc",
           "src/research/automata_study.cc", "src/research/bdd_cpre_replay.cc",
           "src/research/cpre_event.hh", "src/research/cpre_replay.cc",
           "src/research/explicit_forward_game.hh",
           "src/research/forced_contradiction_scan.cc",
           "src/research/forward_game_replay.cc", "src/research/hoa_replay.cc",
           "src/research/mp_census.cc", "src/research/rank_action_replay.hh",
           "src/research/small_invariant_replay.cc",
           "src/research/spot_letter_oracle_replay.cc",
           "src/research/spot_otf_probe.cc", "src/research/spot_provider_replay.cc",
           "src/research/spot_rows_replay.cc")},
    **{name: "standalone TLSF tool, not linked into acacia-bonsai" for name in (
        "subprojects/tlsf-tools/src/tools/common/cli.c",
        "subprojects/tlsf-tools/src/tools/common/cli.h",
        "subprojects/tlsf-tools/src/tools/mealy2moore/main.c",
        "subprojects/tlsf-tools/src/tools/tlsf2ltl/main.c",
        "subprojects/tlsf-tools/src/tools/tlsf2tlsf/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfbenchgraph/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfcertcheck/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/compose_analysis.c",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/compose_games.c",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/compose_internal.h",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/compose_oxidd.c",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/compose_route.c",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/compose_route.h",
        "subprojects/tlsf-tools/src/tools/tlsfcompose/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfinfo/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfnorm/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfresidual/main.c",
        "subprojects/tlsf-tools/src/tools/tlsfsolve/main.c",
        "subprojects/tlsf-tools/src/tools/tlsftemplates/main.c")},
}
MESON_TARGETS = ("src/meson.build", "subprojects/tlsf-tools/src/lib/meson.build",
                 "subprojects/posets/meson.build")
COMPILED = re.compile(r"['\"]([^'\"]+\.(?:cc|c))['\"]")
HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".in", ".l", ".y"})
IDENTIFIER = re.compile(r"\b[A-Za-z_]\w{7,}\b")
DECLARATION = re.compile(r"^\s*([A-Za-z_]\w{7,})\s*(?:\[[^\]]*\])?\s*[;=,]", re.M)
FILE_NAME = re.compile(r"(?<![\w.-])[\w.+-]+\.(?:tlsf|ltl)\b")
HEX_DIGEST = re.compile(r"\b[0-9a-f]{64}\b", re.I)
BOOL_VERDICT_ROW = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(?:true|false|(?:[A-Za-z_]\w*::)?'
    r'(?:REALIZABLE|UNREALIZABLE|UNKNOWN))\s*\}', re.I)
TLSF_SECTION = re.compile(r"\b(?:INPUTS|OUTPUTS|PARAMETERS)\s*\{([^{}]*)\}", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"', re.S)
FAMILY_TOKEN = re.compile(r"(?<![A-Za-z0-9_])[A-Za-z0-9_]+(?![A-Za-z0-9_])")
LITERAL_SEGMENT = re.compile(r"(?<![A-Za-z0-9])[A-Za-z0-9]+(?![A-Za-z0-9])")
INSTANCE_TOKEN = re.compile(r"(?<![A-Za-z0-9_])[A-Za-z0-9_+.-]{8,}(?![A-Za-z0-9_])")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
COMMENT_OR_STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.S)
BRANCH = re.compile(r"(?<!#)\b(?:if|switch)\s*\(")
VERDICT = re.compile(r"\b(?:REALIZABLE|UNREALIZABLE|UNKNOWN)\b")
VERDICT_ACTION = re.compile(r"\b(?:return|exit|_Exit|EXIT_CODE_\w*|REALIZABLE|UNREALIZABLE|UNKNOWN)\b")
VERDICT_RETURN = re.compile(
    r"\breturn\s+(?:EXIT_CODE_\w*|EXIT_SUCCESS|EXIT_FAILURE|"
    r"\w*(?:verdict|outcome|exit_code)\w*|"
    r"[A-Z_]*(?:REALIZABLE|UNREALIZABLE|UNKNOWN)[A-Z_]*)\b|"
    r"\b(?:exit|_Exit)\s*\(", re.I)
COMPARISON = re.compile(r"(?:==|!=|\bstrcmp\b|\bstrncmp\b|\bcompare\s*\(|\b(?:in|contains)\b)")
ASSIGNMENT = re.compile(r"(?<![=!<>])=(?!=)\s*(?P<rhs>[^;]+)")
SOURCE_IDENTITY = re.compile(r"\b(?:source(?:_name|_path|_file)?|filename|file_name|"
                             r"file_path|filepath|basename|input_name|input_path|"
                             r"instance_name|instance_path|path)\b", re.I)
STRING_LITERAL = re.compile(r'''(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')''')

# These corpus stems/declared names also have an ordinary solver meaning.
# Generic family words are exempt only as identifiers, never inside literals.
GENERIC_FAMILY_WORDS = frozenset({
    "alarm", "arbiter", "assumptions", "automata", "chain", "core", "evasion",
    "example", "follow", "full", "increment", "lift", "room", "shift",
    "sort", "system", "test", "time", "timer", "window",
})
GENERIC_DECLARED_NAMES = frozenset({
    "available", "coordinate", "coordinates", "counting", "finished", "location",
    "otherwise", "possible", "proposition", "realizable", "unrealizable",
})
# Exact (path, literal) exceptions for audited non-identity uses.
SAFE_IDENTITY_LITERALS: dict[tuple[str, str], str] = {
    ('src/acacia-bonsai.cc', 'ACACIA_TEST_CHILD_MODES'):
        'included header, test control, or diagnostic record',
    ('src/acacia-bonsai.cc', 'ACACIA_TEST_DELAY_AFTER_REAP'):
        'included header, test control, or diagnostic record',
    ('src/acacia-bonsai.cc', 'native_param_lift_arm.hh'):
        'included header, test control, or diagnostic record',
    ('src/acacia-bonsai.cc', '{\\"stage\\":\\"test_reaped\\",\\"before_deadline\\":'):
        'included header, test control, or diagnostic record',
    ('src/arg_parser.cc', '                    real:param-lift:oxidd (realizability only);\\n'):
        'user-facing option syntax or error help',
    ('src/arg_parser.cc', '  -I VAL            increment value for K, used when M < K\\n'):
        'user-facing option syntax or error help',
    ('src/arg_parser.cc', 'Error: invalid field count in --arms spec %s; expected polarity:transform:backend[:provider]; native forms are both:gr1:oxidd, real:gr1:oxidd, unreal:gr1:oxidd, and real:param-lift:oxidd without a provider.\\n'):
        'user-facing option syntax or error help',
    ('src/arg_parser.cc', 'Error: invalid transform %s in --arms spec %s; real arms accept small or any (or gr1 or param-lift with oxidd).\\n'):
        'user-facing option syntax or error help',
    ('src/boolean_states/transition_core.hh', 'solver/acceptance_core.hh'):
        'included solver header',
    ('src/native_gr1_arm.cc', 'native_test_hooks.hh'):
        'included test-hook header, only used under test define',
    ('src/native_param_lift_arm.cc', 'incomplete or unverified system proof'):
        'native arm label or proof diagnostic',
    ('src/native_param_lift_arm.cc', 'invalid lift evidence JSON'):
        'native arm label or proof diagnostic',
    ('src/native_param_lift_arm.cc', 'lift_call'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_candidate_instantiation'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_decline_'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_internal_check'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_method_certificate'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_method_region'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_policy_export'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_publish'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_schema_learning'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_seed_solve'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_seed_window'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_source'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'lift_target_reduce'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'native_param_lift_arm.hh'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'native_test_hooks.hh'):
        'included header or generic lift stage label',
    ('src/native_param_lift_arm.cc', 'real:param-lift:oxidd'):
        'native arm label or proof diagnostic',
    ('src/native_proof_binding.cc', 'system'):
        'proof-side schema value',
    ('src/phase_records.hh', 'ACACIA_TEST_RECORD_WRITER_STALL'):
        'test record writer option name',
    ('src/portfolio_arm.cc', 'param-lift'):
        'native transformation name in CLI parsing',
    ('src/portfolio_arm.hh', 'real:param-lift:oxidd'):
        'native arm label',
    ('src/solver/configured_components.hh', 'boolean_states/transition_core.hh'):
        'included solver header',
    ('src/solver/equivariant_k_bounded_safety_aut.hh', 'not a verified full symmetric group ('):
        'symmetry validation diagnostic',
    ('src/solver/solver_invoker.cc', ' full_symmetric='):
        'included header or symmetry diagnostic label',
    ('src/solver/solver_invoker.cc', 'solver/unreal_safety_core_witnesses.hh'):
        'included header or symmetry diagnostic label',
    ('src/solver/solver_invoker.cc', 'unreal-safety-core-witness'):
        'diagnostic reason for a generic proof method',
    ('src/solver/symmetry.hh', ' clients (full_symmetric='):
        'symmetry diagnostic label',
    ('src/solver/symmetry.hh', ' exhaustive_full='):
        'symmetry diagnostic label',
    ('src/solver/symmetry.hh', ' fast_full='):
        'symmetry diagnostic label',
    ('src/solver/symmetry_profile.hh', 'k_increment_union'):
        'generic symmetry profile option name',
    ('subprojects/posets/lib/boost/config/assert_cxx03.hpp', 'Your compiler appears not to be fully C++03 compliant.  Detected via defect macro BOOST_NO_LIMITS_COMPILE_TIME_CONSTANTS.'):
        'vendored Boost compiler diagnostic',
    ('subprojects/posets/lib/boost/config/assert_cxx11.hpp', 'Your compiler appears not to be fully C++11 compliant.  Detected via defect macro BOOST_NO_CXX11_HDR_SYSTEM_ERROR.'):
        'vendored Boost compiler diagnostic',
    ('subprojects/tlsf-tools/include/tlsf/gr1_lift.h', 'tlsf-gr1-lift-evidence-v1.1'):
        'versioned lift evidence format',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'METHOD certificate %s time=%.6f\\n'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'METHOD closed-loop %s time=%.6f\\n'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'METHOD region %s version=gr1-region-v1 time=%.6f\\n'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'TLSFCERTCHECK_STATS node_cap=%zu cache_cap=%zu aig_gates_visited=%zu requested_roots=%zu setup_seconds=%.9f proof_seconds=%.9f retry_seconds=%.9f legacy_order=%d peak_live_nodes_sample=%zu policy_mode_builds=%zu policy_counter_constants=%zu policy_specialized_gates=%zu policy_unspecialized_gates=%zu policy_independent_gates=%zu policy_independent_roots=%zu policy_cross_mode_root_reuses=%zu policy_dependent_gates_per_mode=%zu policy_full_cache_modes=%zu successor_substitutions=%zu successor_applications=%zu'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'fairness_assumptions'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'region-v1 requires a realizable system certificate matching this game'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'system'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'system_winning'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_check.c', 'time_seconds'):
        'proof-side schema or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_lift.cc', 'fairness_assumptions'):
        'included header, proof-side schema, or lift stage name',
    ('subprojects/tlsf-tools/src/lib/gr1_lift.cc', 'no system certificate'):
        'proof-side schema or proof diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_lift.cc', 'seed_window'):
        'included header, proof-side schema, or lift stage name',
    ('subprojects/tlsf-tools/src/lib/gr1_lift.cc', 'system'):
        'proof-side schema or proof diagnostic',
    ('subprojects/tlsf-tools/src/lib/gr1_lift.cc', 'tlsf/gr1_lift.h'):
        'included header, proof-side schema, or lift stage name',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'The all-zero state means fairness counter 0. At a state where fair_i holds the counter advances to (i+1) modulo the number of fairness assumptions. The uncontrollable outputs do not read the current controllable letter.'):
        'proof-side schema or generated-circuit explanation',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'fairness_assumptions'):
        'proof-side schema or generated-circuit explanation',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'system'):
        'proof-side schema or generated-circuit explanation',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'system_strategy_semantics'):
        'proof-side schema or generated-circuit explanation',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'system_winning'):
        'proof-side schema or generated-circuit explanation',
    ('subprojects/tlsf-tools/src/lib/normalize.c', 'bool-sort-and'):
        'rewrite rule name or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/normalize.c', 'bool-sort-or'):
        'rewrite rule name or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/normalize.c', 'norm phase=%s schedule=%s formulas=%u changed=%u rejected=%u nodes=%llu->%llu iters=%u time=%.2fms\\n'):
        'rewrite rule name or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/normalize.c', 'sickert-limit-lift'):
        'rewrite rule name or timing diagnostic',
    ('subprojects/tlsf-tools/src/lib/obligation_census.c', 'arbiter'):
        'generic recognized template label for structural census',
    ('subprojects/tlsf-tools/src/lib/pipeline_source.c', 'tlsf-lift'):
        'input parser diagnostic label',
    ('subprojects/tlsf-tools/src/lib/recognize.c', 'arbiter_candidate'):
        'generic recognized template candidate',
    ('subprojects/tlsf-tools/src/lib/templates.c', 'arbiter'):
        'generic recognized template name',
    ('subprojects/tlsf-tools/src/lib/templates_certify.c', 'arbiter'):
        'generic recognized template name',
    ('subprojects/tlsf-tools/src/lib/templates_certify.c', 'arbiter_candidate'):
        'generic recognized template candidate',
    ('subprojects/tlsf-tools/src/lib/templates_certify.c', 'fair_arbiter'):
        'generic recognized template candidate',
    ('src/arg_parser.cc', 'Check realizability for LTL specifications.\\n\\n'):
        'CLI help names the LTL input language',
    ('src/solver/diagnostics.hh', 'ltl'):
        'source-format diagnostic label',
    ('src/solver/diagnostics.hh', ' tlsf_gr_level='):
        'diagnostic field for GR level',
    ('src/solver/solver_invoker.cc', 'Error parsing LTL formula'):
        'generic input parser error',
    ('src/solver/solver_invoker.cc', 'no-input-ltl-synthesis'):
        'diagnostic reason for the no-input synthesis path',
    ('src/solver/solver_invoker.hh', 'ltl'):
        'default input-format metadata',
    ('src/solver/spot_lazy_buchi_view.hh', 'TAA route requires LTL'):
        'generic formula-format diagnostic',
    ('src/solver/spot_lazy_worker.hh',
     'ltl_to_taa,refined_rules=false,cursor=P5,initial_rank=0'):
        'construction diagnostic label',
    ('src/solver/spot_lazy_worker.hh', 'TAA route requires LTL'):
        'generic formula-format diagnostic',
    ('src/solver/spot_worker_record.hh', '.tmp'):
        'temporary file suffix for atomic diagnostic writes',
    ('subprojects/tlsf-tools/src/lib/decompose.c', 'gr.h'):
        'included GR solver header',
    ('subprojects/tlsf-tools/src/lib/gr.c', 'gr.h'):
        'included GR solver header',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'cannot allocate GR(1) certificate'):
        'GR proof allocation error',
    ('subprojects/tlsf-tools/src/lib/gr1_oxidd.c', 'cannot allocate GR(1) policy'):
        'GR proof allocation error',
    ('subprojects/tlsf-tools/src/lib/gr1_service.c',
     'GR(1) requires justice and no invariant constraints'):
        'generic GR input validation error',
    ('subprojects/tlsf-tools/src/lib/section_pattern.c', 'gr.h'):
        'included GR solver header',
    ('subprojects/tlsf-tools/src/lib/spec.c',
     '%s: ltlxba cannot express atom \\"%s\\": ltl2ba and ltl3ba silently truncate formulas at \\"%c\\"; use the ltl format instead\\n'):
        'generic LTL syntax diagnostic',
    ('subprojects/tlsf-tools/src/lib/tlsf.l',
     '             { return TOK_MOORE; }\n\n  /* --- Boolean / LTL atoms --- */\n'):
        'Flex rule separator and LTL grammar comment, not a code literal',
    ('subprojects/tlsf-tools/src/lib/tlsf.l',
     '         { return TOK_OTHERWISE; }\n\n  /* --- LTL operators (single-char temporal first to avoid prefix match) ---\n     X[!] must be matched before X. */\n'):
        'Flex rule separator and LTL grammar comment, not a code literal',
}
# Each key is a specific path and symbol or complete line pattern. A new file,
# option, or branch shape requires a new review, even when it shares a word.
SAFE_ENV_READS = {
    ("src/acacia-bonsai.cc", "ACACIA_OUTER_DEADLINE_MONOTONIC"): "process deadline control",
    ("src/acacia-bonsai.cc", "ACACIA_TEST_CHILD_MODES"): "test child mode control",
    ("src/acacia-bonsai.cc", "ACACIA_TEST_DELAY_AFTER_REAP"): "test reaping delay",
    ("src/ios_precomputers/semantic_action_census.hh", "ACACIA_DIAG_SEMANTIC_DOMINANCE_TESTS"): "diagnostic test count",
    ("src/ios_precomputers/semantic_action_census.hh", "ACACIA_DIAG_SEMANTIC_DOMINANCE_MS"): "diagnostic time budget",
    ("src/phase_records.hh", "ACACIA_TEST_RECORD_WRITER_STALL"): "test writer stall",
    ("src/phase_records.hh", "ACACIA_PHASE_RECORDS"): "record output directory",
    ("src/solver/antichain_snapshot.hh", "name"): "helper receives fixed snapshot option names",
    ("src/solver/antichain_snapshot.hh", "ACACIA_ANTICHAIN_SNAPSHOT_DIR"): "snapshot output directory",
    ("src/solver/antichain_snapshot.hh", "ACACIA_ANTICHAIN_SNAPSHOT_CPRE"): "snapshot diagnostic selection",
    ("src/solver/antichain_snapshot.hh", "ACACIA_ANTICHAIN_SNAPSHOT_ALL_ACTIONS"): "snapshot diagnostic scope",
    ("src/solver/diagnostics.hh", "name"): "helper receives fixed diagnostic option names",
    ("src/solver/diagnostics.hh", "ACACIA_DIAG_PREPROCESSING_CENSUS"): "diagnostic census",
    ("src/solver/diagnostics.hh", "ACACIA_DIAG_PROGRESS_EVERY"): "diagnostic output frequency",
    ("src/solver/diagnostics.hh", "ACACIA_LOCAL_CERTIFICATE_TRACE"): "certificate tracing",
    ("src/solver/losing_proof_replay.hh", "ACACIA_REPLAY_TRACE"): "replay tracing",
    ("src/solver/spot_worker_record.hh", "ACACIA_SPOT_CAPTURE_DIR"): "capture output directory",
    ("src/solver/spot_worker_record.hh", "ACACIA_SPOT_CAPTURE_HISTORY"): "capture history setting",
    ("src/solver/spot_worker_record.hh", "ACACIA_DIAG_INSTANCE"): "capture label only",
}
SAFE_HASH_BRANCHES = {
    ("src/arg_parser.cc", "if (tlsf_pipeline_source_sha256 (result.tlsf_source.data (), result.tlsf_source.size (), sha256))"): "compute source provenance",
    ("src/native_gr1_arm.cc", "if (args.tlsf_sha256 != pipeline->source_sha256) {"): "check source provenance",
    ("src/native_param_lift_arm.cc", "if (args.tlsf_sha256.size () != 64 ||"): "validate digest format",
    ("src/native_param_lift_arm.cc", 'if (!native_sha256_matches (evidence, "game_sha256", result.game_aag, result.game_size)) {'): "bind game proof",
    ("src/native_param_lift_arm.cc", 'if (!native_sha256_matches (evidence, "certificate_sha256", result.certificate_aag,'): "bind certificate proof",
    ("src/native_param_lift_arm.cc", "if (!native_lift_policy_hash_matches (evidence, certificate_method, result.policy_aag,"): "bind policy proof",
    ("src/native_param_lift_arm.cc", "if (!pipeline || args.tlsf_sha256 != pipeline->source_sha256) {"): "check source provenance",
    ("subprojects/tlsf-tools/src/lib/gr1_lift.cc", "if (!tlsf_pipeline_source_sha256(source, size, source_hash))"): "compute source provenance",
    ("subprojects/tlsf-tools/src/lib/gr1_lift.cc", "if (!tlsf_pipeline_source_sha256(candidate.game.data(), candidate.game.size(),"): "compute game proof digest",
    ("subprojects/tlsf-tools/src/lib/gr1_reduction.cc", "if (memcmp(snapshot_sha256, pipeline->source_sha256,"): "validate source snapshot",
    ("subprojects/tlsf-tools/src/lib/pipeline.c", "if (opts && opts->source_sha256 && !provenance_stream) {"): "require provenance stream",
    ("subprojects/tlsf-tools/src/lib/pipeline_source.c", "if (!tlsf_pipeline_source_sha256(source, size, hash)) {"): "compute source provenance",
}
# A pure extension is safe unless its suffix is also a corpus identity.
EXTENSION_LITERAL = re.compile(r"\.[A-Za-z0-9]{1,6}\Z")


@dataclass(frozen=True)
class Identities:
    basenames: frozenset[str]
    instance_stems: frozenset[str]
    families: frozenset[str]
    digests: frozenset[str]
    identifiers: frozenset[str]
    corpus_files: int
    manifest_files: int


@dataclass(frozen=True, order=True)
class Hit:
    path: str
    line: int
    kind: str
    value: str


def family_stem(filename: str) -> str:
    """Drop generated suffixes and numeric parameters, as in U1b/P4."""
    stem = Path(filename).stem
    stem = re.sub(r"_[0-9a-f]{8}$", "", stem, flags=re.I)
    stem = re.sub(r"_pb_\d+(?:_\d+)*_pe_$", "", stem)
    stem = re.sub(r"(?:[_-]?\d+)+$", "", stem)
    stem = re.sub(r"\d+", "#", stem)
    return re.sub(r"[#_-]+", "_", stem).strip("_").lower()


@lru_cache(maxsize=1)
def corpus_identities() -> Identities:
    manifests = sorted(path for path in BENCHMARKS.rglob("*")
                       if path.is_file() and path.suffix in {".list", ".tsv"})
    all_lists = sorted(BENCHMARKS.rglob("all.list"))
    assert all_lists and all(directory.is_dir() for directory in CORPUS_DIRS)
    names = {Path(line.strip()).name for path in all_lists
             for line in path.read_text(encoding="utf-8").splitlines()
             if line.strip() and not line.lstrip().startswith(("#", "@"))
             and line.strip().endswith((".tlsf", ".ltl"))}
    corpus_files = sorted(path for directory in CORPUS_DIRS
                          for path in directory.rglob("*")
                          if path.is_file() and path.suffix in {".tlsf", ".ltl"})
    names.update(path.name for path in corpus_files)
    families = {family_stem(name) for name in names} | set(FAMILY_LABELS)
    families.discard("")
    digests = {hashlib.sha256(path.read_bytes()).hexdigest()
               for path in [*corpus_files, *manifests]}
    owners: dict[str, set[str]] = defaultdict(set)
    for path in (path for path in corpus_files if path.suffix == ".tlsf"):
        source = COMMENT.sub("", path.read_text(encoding="utf-8", errors="replace"))
        for section in TLSF_SECTION.findall(source):
            for name in DECLARATION.findall(section):
                owners[name].add(family_stem(path.name))
    distinctive = {name for name, families in owners.items() if len(families) == 1}
    instance_stems = {Path(name).stem for name in names
                      if len(Path(name).stem) >= 8
                      and Path(name).stem.lower() not in
                      GENERIC_FAMILY_WORDS | GENERIC_DECLARED_NAMES}
    return Identities(frozenset(names), frozenset(instance_stems), frozenset(families),
                      frozenset(digests), frozenset(distinctive),
                      len(corpus_files), len(manifests))


def compiled_sources() -> set[Path]:
    """Read source declarations for the executable and its linked archives.

    The boundary in src/meson.build excludes the native-test and research
    executables. The tlsf archive uses src/lib/meson.build, not src/tools.
    Include optional native/TLSF/OxiDD source branches to cover every build.
    """
    main = (ROOT / "src/meson.build").read_text(encoding="utf-8")
    main = main.split("if native_test_hooks", 1)[0]
    tlsf = (ROOT / "subprojects/tlsf-tools/src/lib/meson.build").read_text(
        encoding="utf-8").split("# The fault-injection build", 1)[0]
    assert "executable ('acacia-bonsai', ab_sources" in main
    assert "static_library(\n  'tlsf'" in tlsf
    return ({ROOT / "src" / name for name in COMPILED.findall(main)}
            | {ROOT / "subprojects/tlsf-tools/src/lib" / name
               for name in COMPILED.findall(tlsf)
               if name not in {"tlsf_parse.c", "tlsf_lex.c"}})


def filesystem_code_files() -> set[Path]:
    """Enumerate candidate code independently of Meson's source parser."""
    return {path for directory in CODE_TREES for path in (ROOT / directory).rglob("*")
            if path.is_file() and path.suffix in CODE_SUFFIXES}


def solver_files() -> list[Path]:
    missing = [directory for directory in INCLUDE_DIRS if not (ROOT / directory).is_dir()]
    assert not missing, f"required solver trees missing: {', '.join(missing)}"
    assert all((ROOT / name).is_file() for name in MESON_TARGETS)
    assert "include_directories('lib')" in (
        ROOT / "subprojects/posets/meson.build").read_text(encoding="utf-8")
    sources = compiled_sources()
    absent = [path for path in sources if not path.is_file()]
    assert not absent, f"compiled solver sources missing: {absent}"
    files = set(sources)
    for directory in INCLUDE_DIRS:
        for path in (ROOT / directory).rglob("*"):
            if (not path.is_file() or path.relative_to(ROOT).parts[1:2] in
                    {("research",), ("python",), ("python_pybind",)}
                    or path.name == "meson.build"):
                continue
            if path.suffix in HEADER_SUFFIXES or path.suffix not in {
                    ".c", ".cc", ".cpp", ".py"}:
                files.add(path)
    files.update(filesystem_code_files())
    files.difference_update(ROOT / name for name in EXCLUDED)
    return sorted(files)


def code_without_comments(source: str, path: Path) -> str:
    """Blank comments while preserving offsets and preprocessor directives."""
    if path.suffix == ".py":
        lines = source.splitlines(keepends=True)
        for token in tokenize.generate_tokens(io.StringIO(source).readline):
            if token.type == tokenize.COMMENT:
                row, column = token.start
                lines[row - 1] = lines[row - 1][:column] + (
                    " " * len(token.string)) + lines[row - 1][column + len(token.string):]
        return "".join(lines)

    def blank(match: re.Match[str]) -> str:
        value = match.group()
        return value if value.startswith(('"', "'")) else re.sub(r"[^\n]", " ", value)

    return COMMENT_OR_STRING.sub(blank, source)


def code_statements(source: str, path: Path) -> list[tuple[int, str]]:
    """Join code across physical lines, retaining each statement's start line.

    Preprocessor directives end at a physical newline. Braces that introduce
    blocks separate statements; initializer braces stay with their declaration.
    This is a small lexical view, not a C++ parser.
    """
    clean = code_without_comments(source, path)
    if path.suffix == ".py":
        return [(number, line.strip()) for number, line in
                enumerate(clean.splitlines(), 1) if line.strip()]
    statements: list[tuple[int, str]] = []
    parts: list[str] = []
    start = 1
    number = 1
    quote = ""
    escaped = False
    space = False
    parentheses = 0
    initializer = 0
    directive = False

    def emit() -> None:
        nonlocal parts, space, directive
        if parts:
            statements.append((start, "".join(parts).strip()))
        parts = []
        space = False
        directive = False

    for char in clean:
        if char == "\n":
            number += 1
        if quote:
            parts.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char.isspace():
            if char == "\n" and directive and not (
                    "".join(parts).rstrip().endswith("\\")):
                emit()
            elif parts:
                space = True
            continue
        if not parts:
            start = number
            directive = char == "#"
        if space:
            parts.append(" ")
            space = False
        parts.append(char)
        if char in {'"', "'"}:
            quote = char
        elif directive:
            continue
        elif char == "(":
            parentheses += 1
        elif char == ")":
            parentheses = max(0, parentheses - 1)
        elif char == "{":
            before = "".join(parts[:-1])
            is_initializer = before.rstrip().endswith("=") or (
                "(" not in before and ")" not in before
                and re.search(r"\b(?:const|constexpr)\b[^;{}()]*\b[A-Za-z_]\w*\s*$",
                              before))
            if initializer or is_initializer:
                initializer += 1
            else:
                emit()
        elif char == "}":
            if initializer:
                initializer -= 1
            else:
                emit()
        elif char == ";" and not parentheses and not initializer:
            emit()
    emit()
    return statements


def expanded_macro_literals(source: str, path: Path) -> list[tuple[int, str]]:
    """Resolve simple object macros and adjacent string or macro tokens."""
    macros: dict[str, str] = {}
    values: list[tuple[int, str]] = []
    token = re.compile(r'"(?:\\.|[^"\\])*"|\b[A-Za-z_]\w*\b')

    def resolve(expression: str) -> tuple[str, bool] | None:
        pieces = []
        used_macro = False
        end = 0
        for match in token.finditer(expression):
            if not re.fullmatch(r"[\s+]*", expression[end:match.start()]):
                return None
            word = match.group()
            if word.startswith('"'):
                pieces.append(word[1:-1])
            elif word in macros:
                pieces.append(macros[word])
                used_macro = True
            else:
                return None
            end = match.end()
        return ("".join(pieces), used_macro) if pieces and re.fullmatch(
            r"[\s+;]*", expression[end:]) else None

    for number, line in code_statements(source, path):
        definition = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)(?!\s*\()\s+(.+)", line)
        if definition:
            resolved = resolve(definition.group(2))
            if resolved is not None:
                macros[definition.group(1)] = resolved[0]
                values.append((number, resolved[0]))
        for match in re.finditer(r'(?:(?:"(?:\\.|[^"\\])*"|\b[A-Za-z_]\w*\b)\s*\+?\s*){2,}', line):
            resolved = resolve(match.group().rstrip(" ;"))
            if resolved is not None and resolved[1]:
                values.append((number, resolved[0]))
        for name, value in macros.items():
            if re.search(rf"\b{re.escape(name)}\b", line):
                values.append((number, value))
    return values


def _literal_values(source: str, path: Path) -> list[tuple[int, str]]:
    """Fold all adjacent or plus-joined C strings and Python constants."""
    tokens = list(STRING.finditer(source))
    values = []
    for index, token in enumerate(tokens):
        if index and re.fullmatch(r"\s*(?:\+\s*)?",
                                  source[tokens[index - 1].end():token.start()]):
            continue
        value = token.group()[1:-1]
        if path.suffix != ".py":
            previous_end = token.end()
            for follower in tokens[index + 1:]:
                gap = source[previous_end:follower.start()]
                if not re.fullmatch(r"\s*(?:\+\s*)?", gap):
                    break
                value += follower.group()[1:-1]
                previous_end = follower.end()
        values.append((source.count("\n", 0, token.start()) + 1, value))
    if path.suffix == ".py":
        tree = ast.parse(source)
        values.extend((node.lineno, value) for node in ast.walk(tree)
                      if hasattr(node, "lineno")
                      and (value := folded_string(node)) is not None)
    return values


def contains_corpus_identity(value: str, ids: Identities) -> bool:
    """Check complete identities inside a literal, including underscored labels."""
    lower = value.lower()
    return bool(
        ((".tlsf" in lower or ".ltl" in lower)
         and any(name in value for name in ids.basenames))
        or len(value) >= 8 and any(stem in value for stem in ids.instance_stems)
        or len(value) >= 64 and any(digest in lower for digest in ids.digests)
        or any(match.group().lower() in ids.families
               for pattern in (FAMILY_TOKEN, LITERAL_SEGMENT)
               for match in pattern.finditer(value))
    )


def identity_hits(path: Path, source: str, ids: Identities) -> list[Hit]:
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    source = code_without_comments(source, path)
    hits: set[Hit] = set()
    # Keep ordinary identifiers visible, but never apply the generic-word
    # exemption to text inside a literal.
    code = STRING_LITERAL.sub(lambda match: re.sub(r"[^\n]", " ", match.group()),
                              source)
    for number, line in enumerate(code.splitlines(), 1):
        for value in FILE_NAME.findall(line):
            if value in ids.basenames:
                hits.add(Hit(rel, number, "basename", value))
        for value in HEX_DIGEST.findall(line):
            if value.lower() in ids.digests:
                hits.add(Hit(rel, number, "corpus SHA-256", value.lower()))
        for value in IDENTIFIER.findall(line):
            if value in ids.identifiers:
                hits.add(Hit(rel, number, "unique signal/parameter", value))
        for match in FAMILY_TOKEN.finditer(line):
            token = match.group().lower()
            if len(token) >= 4 and token in ids.families:
                hits.add(Hit(rel, number, "family stem", token))
        for match in INSTANCE_TOKEN.finditer(line):
            if match.group() in ids.instance_stems:
                hits.add(Hit(rel, number, "instance stem", match.group()))
    for number, statement in code_statements(source, path):
        for match in BOOL_VERDICT_ROW.finditer(statement):
            key = match.group(1)
            if (key in ids.basenames or key in ids.instance_stems
                    or key in ids.families or key in ids.identifiers):
                hits.add(Hit(rel, number, "identity-keyed verdict candidate", key))
    for number, value in _literal_values(source, path):
        if contains_corpus_identity(value, ids):
            hits.add(Hit(rel, number, "identity literal", value))
    return sorted(hits)


def macro_identity_hits(path: Path, source: str, ids: Identities) -> list[Hit]:
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    hits = set()
    clean = code_without_comments(source, path)
    for number, value in expanded_macro_literals(clean, path):
        if contains_corpus_identity(value, ids):
            hits.add(Hit(rel, number, "macro-expanded identity literal", value))
    names = set()
    lines = clean.splitlines()
    for line in lines:
        match = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)(?!\s*\()", line)
        if match:
            name = match.group(1)
            if (name in ids.basenames or name in ids.instance_stems
                    or name.lower() in ids.families):
                names.add(name)
    statements = code_statements(source, path)
    for index, (number, statement) in enumerate(statements):
        if not BRANCH.search(statement) or not COMPARISON.search(statement):
            continue
        if not VERDICT_ACTION.search(" ".join(text for _, text in statements[index:index + 4])):
            continue
        for name in names:
            if re.search(rf"\b{re.escape(name)}\b", statement):
                hits.add(Hit(rel, number, "macro identity verdict", name))
    return sorted(hits)


def data_verdict_hits(path: Path, source: str, ids: Identities) -> list[Hit]:
    """Find identity-keyed answers in any text data file, regardless of suffix."""
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    identities = ids.basenames | ids.instance_stems | ids.families
    hits = []
    row = re.compile(r'''["']?([\w.+-]+)["']?\s*[:,\t]\s*["']?
                      (?:true|false|REALIZABLE|UNREALIZABLE|UNKNOWN)\b''', re.I | re.X)
    for number, line in enumerate(source.splitlines(), 1):
        for match in row.finditer(line):
            if match.group(1) in identities:
                hits.append(Hit(rel, number, "data verdict table", match.group(1)))
        cells = [cell.strip().strip('"\' ') for cell in re.split(r"[,\t]", line)]
        if len(cells) > 1:
            for index, cell in enumerate(cells[:-1]):
                if cell in identities and any(later.lower() in {
                        "true", "false", "realizable", "unrealizable", "unknown"}
                        for later in cells[index + 1:]):
                    hits.append(Hit(rel, number, "data verdict table", cell))
    if path.suffix == ".json":
        try:
            data = json.loads(source)
        except json.JSONDecodeError:
            data = None

        def walk(value: object) -> None:
            if isinstance(value, list):
                for child in value:
                    walk(child)
            elif isinstance(value, dict):
                names = [item for key, item in value.items()
                         if re.search(r"case|instance|family|basename|name", key, re.I)
                         and isinstance(item, str) and item in identities]
                verdict = any(re.search(r"verdict|outcome|result|answer", key, re.I)
                              and (isinstance(item, bool) or
                                   isinstance(item, str) and VERDICT.fullmatch(item))
                              for key, item in value.items())
                if verdict:
                    for name in names:
                        position = source.find(name)
                        hits.append(Hit(rel, source.count("\n", 0, position) + 1,
                                        "data verdict table", name))
                for child in value.values():
                    walk(child)

        walk(data)
    return sorted(set(hits))


def data_identity_hits(path: Path, source: str, ids: Identities) -> list[Hit]:
    """Text assets read on the solver path obey the same identity rule."""
    if path.suffix in {".c", ".cc", ".h", ".hh", ".hpp", ".hxx",
                       ".in", ".l", ".y", ".cpp", ".py", ".json"}:
        return []
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    hits = set()
    for number, line in enumerate(source.splitlines(), 1):
        if contains_corpus_identity(line, ids):
            hits.add(Hit(rel, number, "data identity", line.strip()))
    return sorted(hits)


def input_identity_hits(path: Path, source: str) -> list[Hit]:
    """Catch literal comparisons and lookups keyed by an input's identity.

    This tracks simple local copies and literal-valued constants, including
    object-like string macros. It deliberately does not infer arbitrary types.
    """
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    statements = code_statements(source, path)
    tainted: set[str] = set()
    constants: dict[str, str] = {}
    hits = []
    for number, comparison_line in statements:
        line = comparison_line
        if re.search(r"\b(?:std::)?(?:string(?:_view)?|filesystem::path|char\s*\*)"
                     r"\s*(?:const\s+)?(?:&\s*)?source\b", line):
            tainted.add("source")
        brace = re.search(r"\b(?:constexpr|const)\b[^;{}]*\b([A-Za-z_]\w*)"
                          rf"\s*[{{(]\s*({STRING_LITERAL.pattern})\s*[}})]\s*;", line)
        if brace:
            constants[brace.group(1)] = brace.group(2)
        macro = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)\s+(.+)", line)
        if macro and STRING_LITERAL.search(macro.group(2)):
            constants[macro.group(1)] = macro.group(2).strip()
        assignment = ASSIGNMENT.search(line)
        if assignment:
            left = re.search(r"([A-Za-z_]\w*)\s*$", line[:assignment.start()])
            if left:
                name = left.group(1)
                rhs = assignment.group("rhs").strip()
                copied = re.fullmatch(r"(?:std::move\s*\(\s*)?([A-Za-z_]\w*)\s*\)?", rhs)
                if STRING_LITERAL.fullmatch(rhs):
                    constants[name] = rhs
                elif copied and copied.group(1) in constants:
                    constants[name] = constants[copied.group(1)]
                else:
                    constants.pop(name, None)
                if (re.search(r"\b(?:basename(?:_of)?|filename|\.stem|\.path)\s*\(", rhs)
                        or copied and (copied.group(1) in tainted
                                       or SOURCE_IDENTITY.fullmatch(copied.group(1)))):
                    tainted.add(name)
                else:
                    tainted.discard(name)
        names = set(SOURCE_IDENTITY.findall(comparison_line)) | tainted
        names = {name for name in names if re.search(rf"\b{re.escape(name)}\b",
                                                     comparison_line)}
        values = [STRING_LITERAL.pattern, *(rf"\b{re.escape(name)}\b" for name in constants)]
        literal = "(?:" + "|".join(values) + ")"
        for name in names:
            key = rf"\b{re.escape(name)}\b"
            pair = (rf"(?:{key}\s*(?:==|!=)\s*{literal}|"
                    rf"{literal}\s*(?:==|!=)\s*{key})")
            call = (rf"\b(?:str(?:n)?cmp|compare)\s*\(\s*(?:{key}\s*,\s*{literal}|"
                    rf"{literal}\s*,\s*{key})")
            method = (rf"{key}\s*(?:\.|->)\s*[A-Za-z_]\w*\s*\("
                      rf"\s*[^();]*?{literal}")
            if (re.search(pair, comparison_line) or re.search(call, comparison_line)
                    or re.search(method, comparison_line)):
                hits.append(Hit(rel, number, "input identity literal comparison",
                                comparison_line.strip()))
    return sorted(set(hits))


def alias_verdict_hits(path: Path, source: str) -> list[Hit]:
    """Follow simple local assignments from identity reads to verdict decisions."""
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    statements = code_statements(source, path)
    tainted: dict[str, str] = {}
    integer_return = False
    hits = []
    for index, (number, line) in enumerate(statements):
        if re.match(r"\s*(?:def\s+\w+|(?:[\w:<>,*&]+\s+)+\w+\s*\([^;]*\)\s*\{)", line):
            tainted.clear()
            integer_return = bool(re.match(r"\s*(?:(?:static|inline|extern)\s+)*"
                                           r"int\s+\w+\s*\(", line))
        assignment = ASSIGNMENT.search(line)
        if assignment:
            lhs = line[:assignment.start()].strip()
            name_match = re.search(r"([A-Za-z_]\w*)\s*$", lhs)
            if name_match:
                name = name_match.group(1)
                rhs = assignment.group("rhs").strip()
                kind = None
                if re.search(r"\b(?:getenv|os\.environ)\b", rhs):
                    kind = "environment"
                elif (re.search(r"\b(?:basename(?:_of)?|filename|\.stem|\.path)\s*\(|\b(?:path|instance)\.stem\b", rhs)
                      or (re.search(r"\bargv\s*\[", rhs)
                          and re.search(r"(?:file|path|source|name)", name, re.I))):
                    kind = "basename"
                elif re.search(r"\b(?:sha256|digest|\w*hash\w*)\s*\(", rhs, re.I):
                    kind = "hash"
                elif re.fullmatch(r"(?:std::move\s*\()?\s*([A-Za-z_]\w*)\s*\)?", rhs):
                    copied = re.search(r"([A-Za-z_]\w*)\s*\)?$", rhs)
                    kind = tainted.get(copied.group(1)) if copied else None
                if kind:
                    tainted[name] = kind
                else:
                    tainted.pop(name, None)
        origins = dict(tainted)
        if re.search(r"\b(?:getenv|os\.environ)\s*\(", line):
            origins["(?:getenv|os\\.environ)"] = "environment"
        if re.search(r"\b(?:basename(?:_of)?|filename)\s*\(", line):
            origins["(?:basename(?:_of)?|filename)"] = "basename"
        if re.search(r"\b(?:sha256|digest|\w*hash\w*)\s*\(", line, re.I):
            origins["(?:sha256|digest|\\w*hash\\w*)"] = "hash"
        for name, kind in origins.items():
            pattern = name if name.startswith("(?:") else rf"\b{re.escape(name)}\b"
            if not re.search(pattern, line):
                continue
            if re.search(rf"\b(?:verdict\w*|outcome\w*|answer\w*|results?)\s*(?:\[|\.(?:at|find|get)\s*\()\s*{pattern}", line, re.I):
                hits.append(Hit(rel, number, f"{kind} verdict-map lookup", line.strip()))
            if not BRANCH.search(line) or not COMPARISON.search(line):
                continue
            # A condition on null merely validates a read. The next branch
            # must compare the value itself to an identity or option value.
            if re.search(rf"{pattern}\s*(?:==|!=)\s*(?:nullptr|NULL|0)\b", line):
                continue
            following = " ".join(text for _, text in statements[index:index + 4])
            if verdict_action(following, integer_return=integer_return):
                hits.append(Hit(rel, number, f"{kind} alias verdict", line.strip()))
    return sorted(set(hits))


def verdict_action(source: str, *, integer_return: bool = False) -> bool:
    """Recognize a solver answer or exit status, excluding boolean helpers."""
    return bool(VERDICT_RETURN.search(source)
                or integer_return and re.search(r"\breturn\s+[0-9]+\s*;", source)
                or re.search(r"\b(?:exit_code|verdict|outcome)\w*\s*=\s*"
                             r"(?:EXIT_CODE_\w*|[A-Z_]*(?:REALIZABLE|UNREALIZABLE|UNKNOWN))\b",
                             source, re.I)
                or re.search(r"\b(?:puts|printf|fprintf|fputs|print)\s*\([^;]*"
                             r'"(?:REALIZABLE|UNREALIZABLE|UNKNOWN)"', source)
                or re.search(r'<<\s*"(?:REALIZABLE|UNREALIZABLE|UNKNOWN)"', source))


def dispatch_hits(path: Path, source: str) -> list[Hit]:
    """Expose source names, digests, manifests, options and verdict maps for audit."""
    rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.name
    hits = []
    for number, line in code_statements(source, path):
        if re.search(r"\b(?:basename(?:_of)?|filename|\.stem)\s*\(|\b(?:path|instance)\.stem\b", line):
            hits.append(Hit(rel, number, "basename read candidate", line.strip()))
        if re.search(r"(?<!#)\b(?:if|switch)\s*\([^\n]*(?:sha256|hash|digest)", line, re.I):
            hits.append(Hit(rel, number, "hash branch candidate", line.strip()))
        if re.search(r"(?:all\.list|tlsf-corpus|tests/suites/benchmarks)", line):
            hits.append(Hit(rel, number, "corpus path candidate", line.strip()))
        if re.search(r"\b(?:getenv|os\.environ)\b", line):
            hits.append(Hit(rel, number, "environment option candidate", line.strip()))
        if re.search(r'\{\s*"[^"]+"\s*,\s*"(?:REALIZABLE|UNREALIZABLE|UNKNOWN)"', line):
            hits.append(Hit(rel, number, "verdict table candidate", line.strip()))
    if path.suffix == ".py":
        for node in ast.walk(ast.parse(source)):
            if isinstance(node, ast.Dict):
                for key, value in zip(node.keys, node.values, strict=True):
                    if key is not None and VERDICT.fullmatch(folded_string(value) or ""):
                        hits.append(Hit(rel, node.lineno, "verdict table candidate",
                                        ast.get_source_segment(source, node) or "dict"))
    return hits


DETECTORS = frozenset({"identity", "macro", "alias", "data", "candidate"})


def file_hits(path: Path, source: str, ids: Identities,
              detectors: frozenset[str] = DETECTORS) -> list[Hit]:
    hits = []
    if "identity" in detectors:
        hits.extend(identity_hits(path, source, ids))
    if "macro" in detectors:
        hits.extend(macro_identity_hits(path, source, ids))
    if "alias" in detectors:
        hits.extend(input_identity_hits(path, source))
        hits.extend(alias_verdict_hits(path, source))
    if "data" in detectors:
        hits.extend(data_identity_hits(path, source, ids))
        hits.extend(data_verdict_hits(path, source, ids))
    if "candidate" in detectors:
        hits.extend(dispatch_hits(path, source))
    return sorted(set(hits))


def read_source(path: Path) -> str | None:
    data = path.read_bytes()
    return None if b"\0" in data else data.decode("utf-8", errors="replace")


def scan(ids: Identities, paths: list[Path] | None = None,
         detectors: frozenset[str] = DETECTORS) -> list[Hit]:
    hits = []
    for path in solver_files() if paths is None else paths:
        source = read_source(path)
        if source is not None:
            hits.extend(file_hits(path, source, ids, detectors))
    return sorted(set(hits))


def allowed_hit(hit: Hit, source: str) -> str | None:
    """Return a narrow reason for an audited non-dispatch collision."""
    line = source.splitlines()[hit.line - 1]
    if hit.kind == "input identity literal comparison":
        literals = [value[1:-1] for value in STRING_LITERAL.findall(hit.value)]
        if literals and all(EXTENSION_LITERAL.fullmatch(value)
                            and value[1:].lower() not in corpus_identities().families
                            for value in literals):
            return "pure file-extension check"
    if hit.kind in {"identity literal", "macro-expanded identity literal",
                    "data identity"}:
        return SAFE_IDENTITY_LITERALS.get((hit.path, hit.value))
    if hit.kind == "family stem" and hit.value in GENERIC_FAMILY_WORDS:
        return "ordinary identifier sharing a generic family word"
    if hit.kind == "unique signal/parameter":
        if hit.value in GENERIC_DECLARED_NAMES:
            if not (BRANCH.search(line) and f'"{hit.value}"' in line):
                return "ordinary language/API term outside a literal identity comparison"
    if hit.kind == "environment option candidate":
        if not verdict_action(" ".join(source.splitlines()[hit.line - 1:hit.line + 3])):
            return "environment read does not control a solver verdict"
        names = re.findall(r'\b(?:std::)?getenv\s*\(\s*(?:"([^"]+)"|(name))', line)
        symbols = [literal or variable for literal, variable in names]
        if symbols and all((hit.path, symbol) in SAFE_ENV_READS for symbol in symbols):
            return "; ".join(SAFE_ENV_READS[hit.path, symbol] for symbol in symbols)
    if hit.kind == "hash branch candidate":
        if not verdict_action(" ".join(source.splitlines()[hit.line - 1:hit.line + 3])):
            return "hash comparison does not control a solver verdict"
        return SAFE_HASH_BRANCHES.get((hit.path, line.strip()))
    if hit.kind == "basename read candidate":
        if not BRANCH.search(line):
            return "filename used for input handling, sorting or output label"
    return None


def violations(ids: Identities, paths: list[Path] | None = None,
               detectors: frozenset[str] = DETECTORS) -> list[Hit]:
    result = []
    for path in solver_files() if paths is None else paths:
        source = read_source(path)
        if source is None:
            continue
        for hit in file_hits(path, source, ids, detectors):
            if allowed_hit(hit, source) is None:
                result.append(hit)
    return sorted(result)


def test_no_benchmark_hardcoding() -> None:
    ids = corpus_identities()
    assert ids.corpus_files > 0 and ids.manifest_files > 0
    assert solver_files()
    assert not violations(ids)


def test_all_compiled_sources_are_scanned() -> None:
    """Filesystem enumeration checks coverage independently of Meson parsing."""
    files = set(solver_files())
    candidates = {path for tree in ("src", "subprojects/tlsf-tools/src",
                                    "subprojects/posets/lib")
                  for path in (ROOT / tree).rglob("*") if path.is_file()
                  and path.suffix in {".c", ".cc", ".cpp", ".h", ".hh",
                                      ".hpp", ".inc", ".in"}}
    excluded = {ROOT / name for name in EXCLUDED}
    assert excluded <= candidates
    assert all(EXCLUDED.values())
    assert candidates <= files | excluded
    assert not files & excluded
    assert compiled_sources() <= files
    assert all(path.suffix in {".c", ".cc"} for path in compiled_sources())


def test_new_meson_source_enters_scan(tmp_path: Path,
                                     monkeypatch: pytest.MonkeyPatch) -> None:
    for directory in INCLUDE_DIRS:
        (tmp_path / directory).mkdir(parents=True, exist_ok=True)
    (tmp_path / "subprojects/posets/meson.build").write_text(
        "boost_inc = include_directories('lib')\n")
    main = tmp_path / "src/meson.build"
    main.write_text("ab_sources = ['base.cc']\n"
                    "ab_exe = executable ('acacia-bonsai', ab_sources)\n"
                    "if native_test_hooks\n"
                    "executable ('test', 'research/probe.cc')\n")
    lib = tmp_path / "subprojects/tlsf-tools/src/lib/meson.build"
    lib.write_text("tlsf_lib = static_library(\n  'tlsf', files('dep.c'))\n")
    (tmp_path / "src/base.cc").write_text("int base;\n")
    (tmp_path / "subprojects/tlsf-tools/src/lib/dep.c").write_text("int dep;\n")
    monkeypatch.setitem(globals(), "ROOT", tmp_path)
    assert tmp_path / "src/base.cc" in solver_files()
    main.write_text(main.read_text().replace("'base.cc'", "'base.cc', 'added.cc'"))
    added = tmp_path / "src/added.cc"
    added.write_text("int added;\n")
    assert added in compiled_sources()
    assert added in solver_files()


def test_subdir_source_enters_scan(tmp_path: Path,
                                  monkeypatch: pytest.MonkeyPatch) -> None:
    for directory in INCLUDE_DIRS:
        (tmp_path / directory).mkdir(parents=True, exist_ok=True)
    (tmp_path / "subprojects/posets/meson.build").write_text(
        "boost_inc = include_directories('lib')\n")
    (tmp_path / "src/meson.build").write_text(
        "ab_sources = ['base.cc']\nsubdir('solver')\n"
        "ab_exe = executable ('acacia-bonsai', ab_sources)\n")
    (tmp_path / "subprojects/tlsf-tools/src/lib/meson.build").write_text(
        "tlsf_lib = static_library(\n  'tlsf', files('dep.c'))\n")
    (tmp_path / "src/base.cc").write_text("int base;\n")
    (tmp_path / "subprojects/tlsf-tools/src/lib/dep.c").write_text("int dep;\n")
    added = tmp_path / "src/solver/added.cc"
    added.parent.mkdir()
    (added.parent / "meson.build").write_text("ab_sources += files('added.cc')\n")
    added.write_text('constexpr auto family = "mux";\n')
    monkeypatch.setitem(globals(), "ROOT", tmp_path)
    assert added not in compiled_sources()
    assert added in filesystem_code_files()
    assert added in solver_files()
    assert any(hit.kind == "identity literal" for hit in scan(corpus_identities()))


def mutation_case(kind: str, ids: Identities) -> tuple[Path, str, str, str]:
    """Return logical solver path, injection, expected hit kind, and detector."""
    basename = next(name for name in sorted(ids.basenames) if len(name) > 20)
    family = next(name for name in sorted(ids.families)
                  if len(name) > 12 and "_" in name
                  and name[:3] not in ids.families and name[3:] not in ids.families)
    cases = {
        "basename": ("src/solver/real_backend_selector.hh",
                     f'\nconstexpr auto target = "{basename}";\n', "identity literal", "identity"),
        "split family": ("src/solver/real_backend_selector.hh",
                         f'\nconstexpr auto target = "{family[:3]}" "{family[3:]}";\n',
                         "identity literal", "identity"),
        "embedded family": ("src/solver/real_backend_selector.hh",
                            '\nconstexpr auto label = "prefix_arbiter_suffix";\n',
                            "identity literal", "identity"),
        "short family": ("src/new_solver.cc",
                         '\nconstexpr auto family = "mux";\n',
                         "identity literal", "identity"),
        "family extension": ("src/new_solver.cc",
                             '\nsource_name.ends_with(".mux");\n',
                             "input identity literal comparison", "alias"),
        "contains verdict": ("src/new_solver.cc",
                             '\nif (source_name.contains("special")) return REALIZABLE;\n',
                             "input identity literal comparison", "alias"),
        "contains standalone": ("src/new_solver.cc",
                                '\nsource_name.contains("special");\n',
                                "input identity literal comparison", "alias"),
        "multiline find verdict": ("src/new_solver.cc",
                                   '\nif (source_name.find(\n    "special") '
                                   '!= std::string::npos) return REALIZABLE;\n',
                                   "input identity literal comparison", "alias"),
        "multiline receiver": ("src/new_solver.cc",
                               '\nif (source_name\n    .find("special") '
                               '!= std::string::npos) return REALIZABLE;\n',
                               "input identity literal comparison", "alias"),
        "multiline standalone": ("src/new_solver.cc",
                                 '\nsource_name.find(\n    "special");\n',
                                 "input identity literal comparison", "alias"),
        "multiline contains": ("src/new_solver.cc",
                               '\nif (source_name.contains(\n    "special")) '
                               'return REALIZABLE;\n',
                               "input identity literal comparison", "alias"),
        "multiline starts with": ("src/new_solver.cc",
                                  '\nsource_name\n    .starts_with(\n    "special");\n',
                                  "input identity literal comparison", "alias"),
        "multiline alias verdict": ("src/new_solver.cc",
                                    '\nint decide() {\n'
                                    '  auto key =\n    basename(source_path);\n'
                                    '  if (key ==\n      "ordinary") return 2;\n}\n',
                                    "basename alias verdict", "alias"),
        "corpus hash": ("src/solver/real_backend_selector.hh",
                        f'\nconstexpr auto target = "{min(ids.digests)}";\n',
                        "identity literal", "identity"),
        "macro basename": ("src/solver/real_backend_selector.hh",
                           f'\n#define PART_A "{basename[:len(basename) // 2]}"\n'
                           f'#define PART_B "{basename[len(basename) // 2:]}"\n'
                           '#define TARGET PART_A PART_B\n'
                           'if (source_name == TARGET) return true;\n',
                           "macro-expanded identity literal", "macro"),
        "macro split family": ("src/solver/real_backend_selector.hh",
                               f'\n#define FAMILY_A "{family[:3]}"\n'
                               f'#define FAMILY_B "{family[3:]}"\n'
                               '#define TARGET FAMILY_A FAMILY_B\n'
                               'if (source_name == TARGET) return true;\n',
                               "macro-expanded identity literal", "macro"),
        "macro family name": ("src/solver/real_backend_selector.hh",
                              '\n#define arbiter 1\n'
                              'if (source_name == arbiter) return true;\n',
                              "macro identity verdict", "macro"),
        "alias next line": ("src/solver/real_backend_selector.hh",
                            '\nauto input_name = basename(source_path);\n'
                            'auto copied_name = input_name;\n'
                            'if (copied_name == "special") return EXIT_CODE_REAL;\n',
                            "basename alias verdict", "alias"),
        "verdict map": ("src/solver/real_backend_selector.hh",
                        '\nauto input_name = basename(source_path);\n'
                        'return verdict_map[input_name];\n',
                        "basename verdict-map lookup", "alias"),
        "environment verdict": ("src/solver/diagnostics.hh",
                                '\nauto key = std::getenv (name);\n'
                                'if (strcmp(key, "special") == 0) return EXIT_CODE_REAL;\n',
                                "environment alias verdict", "alias"),
        "environment map": ("src/solver/diagnostics.hh",
                            '\nauto key = std::getenv (name);\n'
                            'return verdict_map[key];\n',
                            "environment verdict-map lookup", "alias"),
        "hash verdict": ("src/solver/real_backend_selector.hh",
                         '\nauto key = sha256(input_bytes);\n'
                         'if (key == "special") return REALIZABLE;\n',
                         "hash alias verdict", "alias"),
        "hash map": ("src/solver/real_backend_selector.hh",
                     '\nauto key = sha256(input_bytes);\n'
                     'return verdict_map[key];\n',
                     "hash verdict-map lookup", "alias"),
        "environment exit status": ("src/solver/diagnostics.hh",
                                    '\nint decide() {\n'
                                    '  auto key = std::getenv("MODE");\n'
                                    '  if (strcmp(key, "special") == 0) return 2;\n}\n',
                                    "environment alias verdict", "alias"),
        "data verdict table": ("src/solver/verdicts.custom",
                               '\narbiter\tREALIZABLE\n',
                               "data verdict table", "data"),
        "data identity": ("src/solver/labels.custom", '\narbiter\n',
                          "data identity", "data"),
        "stored family comparison": ("src/new_solver.cc",
                                     '\nconstexpr std::string_view family = "arbiter";\n'
                                     'if (source_name == family) return true;\n',
                                     "identity literal", "identity"),
        "brace constexpr": ("src/new_solver.cc",
                            '\nconstexpr std::string_view family{"arbiter"};\n'
                            'if (source_name == family) return REALIZABLE;\n',
                            "identity literal", "identity"),
        "split-line comparison": ("src/new_solver.cc",
                                  '\nconstexpr std::string_view family = "arbiter";\n'
                                  'if (source_name ==\n    family) return REALIZABLE;\n',
                                  "identity literal", "identity"),
        "copied condition in new function": ("src/new_solver.cc",
                                             '\nint decide() {\n'
                                             '  auto first = basename(source_path);\n'
                                             '  const auto second = first;\n'
                                             '  if (second == "ordinary") return EXIT_CODE_REAL;\n'
                                             '}\n',
                                             "basename alias verdict", "alias"),
        "strcmp family comparison": (
            "subprojects/tlsf-tools/src/lib/new_solver.c",
            '\nif (strcmp(source_name, "arbiter") == 0) return 1;\n',
            "identity literal", "identity"),
        "candidate corpus path": ("src/new_solver.cc",
                                  '\nconst char* manifest = "tlsf-corpus/unknown.list";\n',
                                  "corpus path candidate", "candidate"),
    }
    name, injection, expected, detector = cases[kind]
    return ROOT / name, injection, expected, detector


MUTATIONS = ("basename", "split family", "embedded family", "short family",
             "family extension", "contains verdict", "contains standalone",
             "multiline find verdict", "multiline receiver", "multiline standalone",
             "multiline contains", "multiline starts with", "multiline alias verdict",
             "corpus hash",
             "macro basename",
             "macro split family", "macro family name", "alias next line", "verdict map",
             "environment verdict", "environment map", "hash verdict", "hash map",
             "environment exit status",
             "data verdict table", "data identity", "stored family comparison",
             "strcmp family comparison",
             "brace constexpr", "split-line comparison", "copied condition in new function",
             "candidate corpus path")


@pytest.mark.parametrize("kind", MUTATIONS)
def test_guard_rejects_solver_mutations(kind: str, tmp_path: Path) -> None:
    ids = corpus_identities()
    logical, injection, expected, _ = mutation_case(kind, ids)
    template = ROOT / "src/solver/real_backend_selector.hh" if not logical.exists() else logical
    mutant = tmp_path / logical.name
    mutant.write_text(template.read_text(encoding="utf-8") + injection, encoding="utf-8")
    source = mutant.read_text(encoding="utf-8")
    bad = [hit for hit in file_hits(logical, source, ids)
           if allowed_hit(hit, source) is None]
    assert expected in {hit.kind for hit in bad}, bad


@pytest.mark.parametrize("kind", MUTATIONS)
def test_mutation_requires_its_detector(kind: str, tmp_path: Path) -> None:
    ids = corpus_identities()
    logical, injection, expected, detector = mutation_case(kind, ids)
    template = ROOT / "src/solver/real_backend_selector.hh" if not logical.exists() else logical
    mutant = tmp_path / logical.name
    mutant.write_text(template.read_text(encoding="utf-8") + injection, encoding="utf-8")
    source = mutant.read_text(encoding="utf-8")
    active = [hit for hit in file_hits(logical, source, ids)
              if allowed_hit(hit, source) is None]
    disabled = [hit for hit in file_hits(logical, source, ids, DETECTORS - {detector})
                if allowed_hit(hit, source) is None]
    assert expected in {hit.kind for hit in active}
    assert expected not in {hit.kind for hit in disabled}


def test_every_detector_has_a_disabling_mutation() -> None:
    ids = corpus_identities()
    covered = set()
    for kind in MUTATIONS:
        logical, injection, _, detector = mutation_case(kind, ids)
        template = ROOT / "src/solver/real_backend_selector.hh" if not logical.exists() else logical
        source = template.read_text(encoding="utf-8") + injection
        active = [hit for hit in file_hits(logical, source, ids)
                  if allowed_hit(hit, source) is None]
        disabled = [hit for hit in file_hits(logical, source, ids, DETECTORS - {detector})
                    if allowed_hit(hit, source) is None]
        if active and not disabled:
            covered.add(detector)
    assert covered == DETECTORS


@pytest.mark.parametrize("path,source", (
    ("src/solver/diagnostics.hh",
     'bool diagnostics_enabled() {\n  auto env = std::getenv("ACACIA_DIAG_PREPROCESSING_CENSUS");\n'
     '  if (strcmp(env, "on") == 0) return true;\n  return false;\n}\n'),
    ("src/native_gr1_arm.cc",
     'bool valid_source(const Bytes& input, const Digest& expected) {\n'
     '  auto digest = sha256(input);\n  if (digest != expected) return false;\n'
     '  return true;\n}\n'),
))
def test_non_verdict_helpers_pass(path: str, source: str) -> None:
    logical = ROOT / path
    assert not [hit for hit in file_hits(logical, source, corpus_identities())
                if allowed_hit(hit, source) is None]


def test_new_diagnostic_option_without_verdict_passes() -> None:
    source = ('bool verbose() {\n  auto option = getenv("NEW_DIAGNOSTIC_OPTION");\n'
              '  if (strcmp(option, "on") == 0) return true;\n  return false;\n}\n')
    assert not [hit for hit in file_hits(ROOT / "src/new_solver.cc", source,
                                         corpus_identities()) if allowed_hit(hit, source) is None]


def test_environment_comparison_writing_verdict_string_fails() -> None:
    source = ('void decide() {\n  auto option = getenv("MODE");\n'
              '  if (strcmp(option, "on") == 0) puts("REALIZABLE");\n}\n')
    hits = alias_verdict_hits(ROOT / "src/new_solver.cc", source)
    assert any(hit.kind == "environment alias verdict" for hit in hits)


@pytest.mark.parametrize("expression", (
    'source_name == "ordinary"',
    '"ordinary" != source_name',
    'source_name.compare("ordinary")',
    'source_name.find("ordinary")',
    'source_name.starts_with("ordinary")',
    'source_name.ends_with("ordinary")',
    'source_name.contains("ordinary")',
    'source_name.rfind("ordinary")',
    'source_name.find_first_of("ordinary")',
    'source_name.custom_method("ordinary")',
))
def test_input_identity_comparisons_rejected(expression: str) -> None:
    source = f'if ({expression}) return false;\n'
    assert input_identity_hits(ROOT / "src/new_solver.cc", source)


def test_multiline_comparison_reports_statement_start() -> None:
    source = ('// source_name.find("ignored")\n\n'
              'if (source_name /* comment */\n'
              '    .find(\n'
              '    "special") != std::string::npos) return REALIZABLE;\n')
    hits = input_identity_hits(ROOT / "src/new_solver.cc", source)
    assert len(hits) == 1
    assert hits[0].line == 3


def test_runtime_collection_and_new_extension_check_pass() -> None:
    path = ROOT / "src/new_solver.cc"
    ids = corpus_identities()
    for source in ('if (seen_sources.contains(source_name)) return false;\n',
                   'if (source_name.ends_with(".tlsf")) return false;\n'):
        assert not [hit for hit in file_hits(path, source, ids)
                    if allowed_hit(hit, source) is None]
    assert input_identity_hits(path, 'if (source_name.ends_with(".tlsf")) return false;')
    multiline = 'if (source_name.ends_with(\n    ".tlsf")) return false;\n'
    assert not [hit for hit in file_hits(path, multiline, ids)
                if allowed_hit(hit, multiline) is None]


def test_short_family_tokens_and_extension_are_rejected() -> None:
    ids = corpus_identities()
    path = ROOT / "src/new_solver.cc"
    assert "mux" in ids.families
    for literal in ("mux", "prefix_mux_suffix", ".mux"):
        source = f'constexpr auto value = "{literal}";\n'
        assert any(hit.kind == "identity literal" for hit in file_hits(path, source, ids))
    source = 'source_name.ends_with(".mux");\n'
    assert any(hit.kind == "input identity literal comparison" and
               allowed_hit(hit, source) is None for hit in file_hits(path, source, ids))
    for literal in ("demux", "muxed"):
        assert not contains_corpus_identity(literal, ids)


@pytest.mark.parametrize("source", (
    'constexpr std::string_view family{"ordinary"};\n'
    'if (source_name == family) return REALIZABLE;\n',
    'constexpr std::string_view family = "ordinary";\n'
    'if (source_name ==\n    family) return REALIZABLE;\n',
))
def test_literal_initialized_comparisons_are_found(source: str) -> None:
    assert input_identity_hits(ROOT / "src/new_solver.cc", source)


def test_literal_exception_is_exact() -> None:
    (path, literal), reason = next(iter(SAFE_IDENTITY_LITERALS.items()))
    assert reason
    hit = Hit(path, 1, "identity literal", literal)
    assert allowed_hit(hit, f'"{literal}"\n') == reason
    assert allowed_hit(Hit("src/new_solver.cc", 1, hit.kind, literal),
                       f'"{literal}"\n') is None
    changed = literal + "-new"
    assert allowed_hit(Hit(path, 1, hit.kind, changed), f'"{changed}"\n') is None


@pytest.mark.parametrize("comment", ("//", "/*"))
def test_comments_are_evidence_but_directives_are_code(comment: str) -> None:
    ids = corpus_identities()
    basename = next(name for name in sorted(ids.basenames) if len(name) > 20)
    path = ROOT / "src/solver/real_backend_selector.hh"
    ending = " */" if comment == "/*" else ""
    source = f'int x = 0; {comment} evidence: {basename}{ending}\n'
    assert not identity_hits(path, source, ids)
    assert any(hit.kind == "identity literal" for hit in identity_hits(
        path, f'#define TARGET "{basename}"\n', ids))


@pytest.mark.parametrize("removed", ("subprojects/tlsf-tools/src/lib", "subprojects/posets/lib"))
def test_missing_required_tree_fails_loudly(removed: str, tmp_path: Path,
                                            monkeypatch: pytest.MonkeyPatch) -> None:
    for directory in SOURCE_DIRS:
        if directory != removed:
            (tmp_path / directory).mkdir(parents=True, exist_ok=True)
    monkeypatch.setitem(globals(), "ROOT", tmp_path)
    with pytest.raises(AssertionError, match="required solver trees missing"):
        solver_files()


def test_allowlists_do_not_cover_new_file_or_symbol() -> None:
    env_path, env_symbol = next(iter(SAFE_ENV_READS))
    env_line = f'const char* value = getenv("{env_symbol}");'
    env_source = env_line + "\nreturn REALIZABLE;\n"
    env_hit = Hit(env_path, 1, "environment option candidate", env_line)
    assert allowed_hit(env_hit, env_source)
    assert allowed_hit(Hit("src/new_solver.cc", 1, env_hit.kind, env_line),
                       env_source) is None
    changed = env_line.replace(env_symbol, "NEW_INSTANCE_OPTION")
    assert allowed_hit(Hit(env_path, 1, env_hit.kind, changed),
                       changed + "\nreturn REALIZABLE;\n") is None
    hash_path, pattern = next(iter(SAFE_HASH_BRANCHES))
    hash_source = pattern + "\nreturn REALIZABLE;\n"
    hash_hit = Hit(hash_path, 1, "hash branch candidate", pattern)
    assert allowed_hit(hash_hit, hash_source)
    assert allowed_hit(Hit("src/new_solver.cc", 1, hash_hit.kind, pattern),
                       hash_source) is None
    changed_hash = hash_source.replace(pattern, "if (new_input_digest == known_digest)")
    assert allowed_hit(Hit(hash_path, 1, hash_hit.kind, pattern), changed_hash) is None


def test_json_data_object_with_instance_and_verdict() -> None:
    ids = corpus_identities()
    basename = next(name for name in sorted(ids.basenames) if len(name) > 20)
    path = ROOT / "src/solver/answers.json"
    source = json.dumps({"rows": [{"instance": basename, "verdict": True}]}, indent=2)
    assert any(hit.kind == "data verdict table" for hit in data_verdict_hits(path, source, ids))

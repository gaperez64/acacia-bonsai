#include <bddx.h>

#include <cstddef>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#if defined(__GNUC__)
#define P2A_EXPORT __attribute__((visibility("default")))
#else
#define P2A_EXPORT
#endif

namespace {

// The installed BuDDy header exposes no ceiling query.  This value is paired
// with the exact libbddx hash in buddy_veccompose.py and its build sidecar.
constexpr int kPinnedMaxVariableCount = 2097150;

thread_local char last_error[256] = {};
thread_local int forced_failure = 0;
thread_local int buddy_error = 0;
thread_local bool prior_handler_restored = false;

void record_buddy_error(int status) noexcept {
  if (buddy_error == 0) {
    buddy_error = status;
  }
}

class ErrorHookScope {
 public:
  ErrorHookScope() noexcept
      : prior_(bdd_error_hook(&record_buddy_error)) {
    buddy_error = 0;
  }

  ErrorHookScope(const ErrorHookScope&) = delete;
  ErrorHookScope& operator=(const ErrorHookScope&) = delete;

  ~ErrorHookScope() noexcept { bdd_error_hook(prior_); }

  int status() const noexcept { return buddy_error; }

 private:
  bddinthandler prior_;
};

struct PairDeleter {
  void operator()(bddPair* pair) const noexcept {
    if (pair != nullptr) {
      bdd_freepair(pair);
    }
  }
};

int fail(int status, const char* message) noexcept {
  std::snprintf(last_error, sizeof(last_error), "%s", message);
  return status;
}

int fail_buddy(int status, const char* operation) noexcept {
  std::snprintf(last_error, sizeof(last_error),
                "%s raised BuDDy error %d", operation, status);
  return status;
}

thread_local int sentinel_calls = 0;

void sentinel_error_handler(int) noexcept { ++sentinel_calls; }

}  // namespace

extern "C" {

P2A_EXPORT int p2a_bdd_veccompose(
    bdd* output,
    const bdd* function,
    const int* variables,
    const bdd* const* replacements,
    std::size_t count) noexcept {
  last_error[0] = '\0';
  ErrorHookScope errors;
  if (forced_failure != 0) {
    const int status = forced_failure;
    forced_failure = 0;
    return fail(status, "forced BuDDy resource failure for testing");
  }
  if (output == nullptr || function == nullptr ||
      (count != 0 && (variables == nullptr || replacements == nullptr))) {
    return fail(-1000, "invalid native composition arguments");
  }
  try {
    const int variable_count = bdd_varnum();
    if (errors.status() != 0) {
      return fail_buddy(errors.status(), "bdd_varnum");
    }
    for (std::size_t index = 0; index < count; ++index) {
      if (variables[index] < 0 || variables[index] >= variable_count) {
        return fail(-1002, "substitution variable is outside bdd_varnum");
      }
      if (replacements[index] == nullptr) {
        return fail(-1000, "null replacement proxy");
      }
      for (std::size_t previous = 0; previous < index; ++previous) {
        if (variables[previous] == variables[index]) {
          return fail(-1002, "substitution variables must be unique");
        }
      }
    }

    bdd result;
    {
      // The pair is destroyed before the error hook scope, including on
      // exceptions and early returns from this block.
      std::unique_ptr<bddPair, PairDeleter> pair(bdd_newpair());
      if (errors.status() != 0) {
        return fail_buddy(errors.status(), "bdd_newpair");
      }
      if (pair == nullptr) {
        return fail(BDD_MEMORY, "bdd_newpair returned null");
      }
      for (std::size_t index = 0; index < count; ++index) {
        const int status = bdd_setbddpair(
            pair.get(), variables[index], *replacements[index]);
        if (errors.status() != 0) {
          return fail_buddy(errors.status(), "bdd_setbddpair");
        }
        if (status < 0) {
          return fail(status, "bdd_setbddpair failed without an error hook call");
        }
      }
      result = bdd_veccompose(*function, pair.get());
      if (errors.status() != 0) {
        return fail_buddy(errors.status(), "bdd_veccompose");
      }
    }
    if (errors.status() != 0) {
      return fail_buddy(errors.status(), "bdd_freepair");
    }
    // bddfalse is a valid result.  Only the scoped hook distinguishes it
    // from BuDDy's checked-API error sentinel.
    *output = std::move(result);
    return 0;
  } catch (const std::exception& error) {
    return fail(-1001, error.what());
  } catch (...) {
    return fail(-1001, "unknown native composition failure");
  }
}

P2A_EXPORT int p2a_bdd_gbc() noexcept {
  last_error[0] = '\0';
  ErrorHookScope errors;
  try {
    bdd_gbc();
    if (errors.status() != 0) {
      return fail_buddy(errors.status(), "bdd_gbc");
    }
    return 0;
  } catch (const std::exception& error) {
    return fail(-1001, error.what());
  } catch (...) {
    return fail(-1001, "unknown native garbage-collection failure");
  }
}

P2A_EXPORT int p2a_bdd_setvarorder_for_testing(
    const int* variables, std::size_t count) noexcept {
  last_error[0] = '\0';
  ErrorHookScope errors;
  if (variables == nullptr && count != 0) {
    return fail(-1000, "null variable-order array");
  }
  try {
    const int variable_count = bdd_varnum();
    if (errors.status() != 0) {
      return fail_buddy(errors.status(), "bdd_varnum");
    }
    if (count != static_cast<std::size_t>(variable_count)) {
      return fail(-1002, "variable order must contain every BDD variable");
    }
    std::vector<unsigned char> seen(count, 0);
    for (std::size_t index = 0; index < count; ++index) {
      const int variable = variables[index];
      if (variable < 0 || variable >= variable_count || seen[variable] != 0) {
        return fail(-1002, "variable order must be a permutation");
      }
      seen[variable] = 1;
    }
    bdd_setvarorder(const_cast<int*>(variables));
    if (errors.status() != 0) {
      return fail_buddy(errors.status(), "bdd_setvarorder");
    }
    return 0;
  } catch (const std::exception& error) {
    return fail(-1001, error.what());
  } catch (...) {
    return fail(-1001, "unknown variable-order failure");
  }
}

P2A_EXPORT int p2a_bdd_varnum() noexcept {
  last_error[0] = '\0';
  ErrorHookScope errors;
  const int result = bdd_varnum();
  if (errors.status() != 0) {
    return fail_buddy(errors.status(), "bdd_varnum");
  }
  return result;
}

P2A_EXPORT int p2a_bdd_max_variable_count() noexcept {
  return kPinnedMaxVariableCount;
}

P2A_EXPORT int p2a_bdd_setvarnum_checked(int required) noexcept {
  last_error[0] = '\0';
  if (required < 0 || required > kPinnedMaxVariableCount) {
    return fail(-1002, "variable count is outside the pinned libbddx range");
  }
  ErrorHookScope errors;
  try {
    const int status = bdd_setvarnum(required);
    if (errors.status() != 0) {
      bdd_clear_error();
      return fail_buddy(errors.status(), "bdd_setvarnum");
    }
    if (status < 0) {
      bdd_clear_error();
      return fail(status, "bdd_setvarnum failed without an error hook call");
    }
    return 0;
  } catch (const std::exception& error) {
    return fail(-1001, error.what());
  } catch (...) {
    return fail(-1001, "unknown variable-count failure");
  }
}

P2A_EXPORT const char* p2a_bdd_last_error() noexcept {
  return last_error;
}

P2A_EXPORT const void* p2a_bdd_versionnum_address() noexcept {
  return reinterpret_cast<const void*>(&bdd_versionnum);
}

P2A_EXPORT void p2a_bdd_force_failure_for_testing(int status) noexcept {
  forced_failure = status;
}

P2A_EXPORT int p2a_bdd_trigger_error_for_testing() noexcept {
  last_error[0] = '\0';
  sentinel_calls = 0;
  prior_handler_restored = false;
  const bddinthandler original = bdd_error_hook(&sentinel_error_handler);
  int status = 0;
  {
    ErrorHookScope errors;
    {
      std::unique_ptr<bddPair, PairDeleter> pair(bdd_newpair());
      if (pair != nullptr && errors.status() == 0) {
        (void)bdd_setbddpair(pair.get(), bdd_varnum(), bddtrue);
      }
      status = errors.status();
    }
    if (status == 0) {
      status = errors.status();
    }
  }
  const bddinthandler restored = bdd_error_hook(original);
  prior_handler_restored = restored == &sentinel_error_handler;
  if (!prior_handler_restored || sentinel_calls != 0) {
    return fail(-1003, "scoped BuDDy error hook did not restore the prior handler");
  }
  if (status == 0) {
    return fail(-1003, "out-of-range bdd_setbddpair did not raise a BuDDy error");
  }
  return fail_buddy(status, "out-of-range bdd_setbddpair test");
}

P2A_EXPORT int p2a_bdd_prior_handler_restored_for_testing() noexcept {
  return prior_handler_restored ? 1 : 0;
}

}  // extern "C"

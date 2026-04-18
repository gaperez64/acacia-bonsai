%module acacia_python
%{
#include "python_interface.hh"

// Lippincott-function for exception translation.
static void handle_any_exception()
{
  try {
    throw;
  }
  catch (const std::invalid_argument& e) {
    SWIG_Error(SWIG_ValueError, e.what());
  }
  catch (const std::overflow_error& e) {
    SWIG_Error(SWIG_OverflowError, e.what());
  }
  catch (const std::out_of_range& e) {
    SWIG_Error(SWIG_IndexError, e.what());
  }
  catch (const std::runtime_error& e) {
    SWIG_Error(SWIG_RuntimeError, e.what());
  }
}
%}

// Tell SWIG to wrap std::string and std::vector<std::string>
%include "std_string.i"
%include "std_vector.i"
%template(StringVector) std::vector<std::string>;
%template(IntVector) std::vector<int>;

// Global exception handler for all wrapped functions.
%exception {
  try {
    $action
  }
  catch (...) {
    handle_any_exception();
    SWIG_fail;
  }
}

// Hide internal fields of Game; Python only needs the factory function.
%ignore Game::twa;
%ignore Game::inputs;
%ignore Game::outputs;
%ignore Game::dict;
%ignore Game::input_aps;
%ignore Game::output_aps;

// Functions that return heap-allocated objects owned by Python.
%newobject create_twa;
%newobject solve_acacia_safety_game;
%newobject get_initial_state;
%newobject successor;
%newobject make_vector;
// winreg_iterator::__next__ returns an owned vector_wrapper*
%newobject winreg_iterator::__next__;

// SWIG doesn't reliably handle rvalue-reference constructors. These wrapper
// classes are produced by the C++ factory functions above, so Python never
// needs to construct them directly.
%ignore WinningRegion::WinningRegion;
%ignore GameResult::GameResult;
%ignore vector_wrapper::vector_wrapper;

// WinningRegion is returned as a pointer into the GameResult's internal
// std::optional; if the Python GameResult object is collected, that pointer
// dangles. Attach a reference to the GameResult on the returned object so
// Python's GC keeps the owner alive.
%pythonappend GameResult::get_winning_region %{
    if val is not None:
        val._owner = self
%}

// Likewise, winreg_iterator borrows from the WinningRegion's downset.
%pythonappend WinningRegion::__iter__ %{
    val._owner = self
%}

// vector_iterator borrows from its vector_wrapper.
%pythonappend vector_wrapper::__iter__ %{
    val._owner = self
%}

// this generates the Python bindings
%include "python_interface.hh"


%extend vector_wrapper {
    vector_iterator __iter__() {
        return vector_iterator(*$self);
    }

    size_t __len__() {
        return $self->len();
    }

    // Integer indexing. Python iteration over the wrapper already uses
    // __iter__ above, but __getitem__ makes "for i,v in enumerate(w)"
    // interactive-friendly and supports expressions like w[5].
    int __getitem__(size_t i) {
        if (i >= $self->len()) {
            PyErr_SetString(PyExc_IndexError, "vector index out of range");
            return 0;
        }
        // Promote to int so Python gets a number, not a single-char string.
        return (int) $self->get_vec()[i];
    }
}

// ---------------- VectorIterator ----------------
// Override the global %exception for __next__: after running the action we
// check PyErr_Occurred() and SWIG_fail if set. Without this, setting
// PyExc_StopIteration inside the method is not enough — SWIG still wraps the
// (dummy) return value into a non-NULL PyObject*, which makes CPython either
// raise a SystemError ("returned a result with an error set") or silently
// discard the StopIteration.
%exception vector_iterator::__next__ {
    try {
        $action
    } catch (...) {
        handle_any_exception();
        SWIG_fail;
    }
    if (PyErr_Occurred()) SWIG_fail;
}

%extend vector_iterator {
    vector_iterator *__iter__() {
        return $self;
    }

    // Returning `int` rather than the storage type (signed char /
    // VECTOR_ELT_T). SWIG maps `char` to a 1-char Python string, which would
    // surface -1 as the Unicode escape sequence \udcff; using `int` yields
    // proper Python numbers like -1, 0, 1, ...
    int __next__() {
        if (!$self->has_next()) {
            PyErr_SetNone(PyExc_StopIteration);
            return 0;
        }
        return (int) $self->next();
    }

    size_t __len__() {
        return $self->len();
    }
}

%extend WinningRegion {
    winreg_iterator __iter__() {
        return winreg_iterator($self->get_region());
    }

    size_t __len__() {
        return $self->len();
    }

    // VECTOR_ELT_T is a macro (signed char by default) that SWIG does not
    // resolve through the #define; without this wrapper, get_k() surfaces a
    // raw "VECTOR_ELT_T *" SWIG proxy. Expose it as a plain int instead.
    int k() { return (int) $self->get_k(); }
}

// Same StopIteration fix as for vector_iterator::__next__ above.
%exception winreg_iterator::__next__ {
    try {
        $action
    } catch (...) {
        handle_any_exception();
        SWIG_fail;
    }
    if (PyErr_Occurred()) SWIG_fail;
}

%extend winreg_iterator {
    winreg_iterator *__iter__() {
        return $self;
    }

    vector_wrapper* __next__() {
        if (!$self->has_next()) {
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;
        }
        return $self->next();
    }

    size_t __len__() {
        return $self->len();
    }
}

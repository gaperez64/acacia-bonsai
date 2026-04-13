%module acacia_python
%{
#include "python_interface.hh"
%}

// Tell SWIG to wrap std::vector<std::string>
%include "std_string.i"
%include "std_vector.i"
%template(StringVector) std::vector<std::string>;

// this file is included to make Spot objects non-opaque in the Python interface when returned by Acacia functions.
// NOTE: this introduces ALL Spot functionality into the "acacia_python" package
%include "impl.i"

// this generates the Python bindings
%include "python_interface.hh"


%extend vector_wrapper {
    vector_iterator __iter__() {
        return vector_iterator(*$self);
    }

    size_t __len__() {
        return $self->len();
    }
}

// ---------------- VectorIterator ----------------
// Override the global %exception (see impl.i) for __next__: after running the
// action we check PyErr_Occurred() and SWIG_fail if set. Without this, setting
// PyExc_StopIteration inside the method is not enough -- SWIG still wraps the
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

    // Note: I am using "char" here, since SWIG does not understand the #define in configuration.hh
    char __next__() {
        if (!$self->has_next()) {
            PyErr_SetNone(PyExc_StopIteration);
            return (char)0;
        }
        return $self->next();
    }

    size_t __len__() {
        return $self->len();
    }
}

%extend winreg_wrapper {
    winreg_iterator __iter__() {
        return winreg_iterator($self->get_region());
    }

    size_t __len__() {
        return $self->len();
    }
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

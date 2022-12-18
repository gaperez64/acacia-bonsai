#pragma once

// https://stackoverflow.com/questions/4484982/how-to-convert-typename-t-to-string-in-c

#include <typeinfo>
#include <cxxabi.h>

template<typename T>
std::string get_typename(const T& x)
{
    char* name = nullptr;
    int status;
    name = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
    if (name)
    {
        std::string name2(name);
        free(name);
        return name2;
    }
    else
    {
        // not human readable
        return typeid(T).name();
    }
}

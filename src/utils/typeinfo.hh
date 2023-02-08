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

template<typename T, typename U>
class itpair
{
public:
    T first;
    U second;

    const auto& begin() const
    {
        return first.begin();
    }
    const auto& end() const
    {
        return first.end();
    }

    auto& operator[](size_t i)
    {
        return first[i];
    }
};

//

template<class T, class U>
std::ostream& operator<<(std::ostream& ost, const std::pair<T, U>& pair)
{
    ost << "(" << pair.first << ", " << pair.second << ")";
    return ost;
}

template<class T, class U>
std::ostream& operator<<(std::ostream& ost, const itpair<T, U>& pair)
{
    ost << "(" << pair.first << ", " << pair.second << ")";
    return ost;
}

template<class T>
std::ostream& operator<<(std::ostream& ost, const std::vector<T>& vec)
{
    ost << "[";
    for(size_t i = 0; i < vec.size(); i++)
    {
        ost << vec[i];
        if (i != (vec.size()-1)) ost << ", ";
    }
    ost << "]";
    return ost;
}

template<class T>
std::ostream& operator<<(std::ostream& ost, const std::set<T>& s)
{
    ost << "{";
    size_t i = 0;
    for(const T& elem: s)
    {
        ost << elem;
        if (i != (s.size()-1)) ost << ", ";
        i++;
    }
    ost << "}";
    return ost;
}

template<class T, class U>
std::ostream& operator<<(std::ostream& ost, const std::map<T, U>& m)
{
    ost << "{";
    size_t i = 0;

    for(auto it = m.begin(); it != m.end(); it++)
    {
        ost << it->first << ": " << it->second;
        if (i != (m.size()-1)) ost << ", ";
        i++;
    }

    ost << "}";
    return ost;
}
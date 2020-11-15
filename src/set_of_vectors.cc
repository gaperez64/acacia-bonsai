#include "set_of_vectors.hh"
#include <iostream>

std::ostream& operator<<(std::ostream& os, const set_of_vectors::vector& v)
{
  os << "{ ";
  for (auto el : v)
    os << el << " ";
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const set_of_vectors::set& f)
{
  for (auto el : f.vector_set)
    os << el << std::endl;

  return os;
}

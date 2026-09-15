#include <vector>

#include "vector.hpp"
#include "PerfEvent.hpp"

void fill_vector(msc::vector<int>& a, std::size_t size){
  for(std::size_t i = 0; i < size; ++i){
    a.emplace_back(i);
  }
}

template<class T>
void splice_vectors(msc::vector<T>& a, msc::vector<T>& b){
  a.splice(std::move(b));
}


void fill_vector(std::vector<int>& a, std::size_t size){
  for(std::size_t i = 0; i < size; ++i){
    a.emplace_back(i);
  }
}

template<class T>
void splice_vectors(std::vector<T>& a, std::vector<T>& b){
  a.append_range(b);
}

int main(){
  PerfEvent e;
  const std::size_t max_size = (1ull << 30);
  {
    e.setParam("method", "splice");
    msc::vector<int> a;
    msc::vector<int> b;
    
    for(std::size_t i = 1; i < max_size; i<<=1){
      e.setParam("size", i);
      a.clear();
      b.clear();
      fill_vector(a, i);
      fill_vector(b, i);
      PerfEventBlock block (e);
      splice_vectors(a, b);
    }
  }

  {
    e.setParam("method", "copy");
    std::vector<int> a;
    std::vector<int> b;
    
    for(std::size_t i = 1; i < max_size; i<<=1){
      e.setParam("size", i);
      a.clear();
      b.clear();
      fill_vector(a, i);
      fill_vector(b, i);
      PerfEventBlock block (e);
      splice_vectors(a, b);
    }
  }
}
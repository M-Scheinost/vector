#include <vector>

#include "vector.hpp"
#include "bench/PerfEvent.hpp"

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

void bench_splice(std::size_t max_size){
  PerfEvent e;
  {
    e.setParam("method", "splice");
    
    for(std::size_t i = 1; i < max_size; i<<=1){
      e.setParam("size", i);
      msc::vector<int> a;
      msc::vector<int> b;
      fill_vector(a, i);
      fill_vector(b, i);
      PerfEventBlock block (e, i);
      splice_vectors(a, b);
    }
  }

  {
    e.setParam("method", "copy");
    
    for(std::size_t i = 1; i < max_size; i<<=1){
      e.setParam("size", i);
      std::vector<int> a;
      std::vector<int> b;
      fill_vector(a, i);
      fill_vector(b, i);
      PerfEventBlock block (e, i);
      splice_vectors(a, b);
    }
  }
}

void bench_splice_multi(std::size_t reps, std::size_t count, std::size_t size){
  PerfEvent e;

  for(std::size_t r = 0; r < reps; ++r){
    e.setParam("method", "one-by-one");
    std::vector<msc::vector<int>> parts(count);
    for(size_t k = 0; k < parts.size(); ++k){
      fill_vector(parts[k], size);
    }
    msc::vector<int> a;
    fill_vector(a,size);
    
    PerfEventBlock block (e);
    for(std::size_t i = 0; i < count; ++i){
      splice_vectors(a, parts[i]);
    }
  }
  
  for(std::size_t r = 0; r < reps;++r){
    e.setParam("method", "together");
    std::vector<msc::vector<int>> parts(count);
    for(size_t k = 0; k < parts.size(); ++k){
      fill_vector(parts[k], size);
    }
    msc::vector<int> a;
    fill_vector(a,size);
    
    PerfEventBlock block (e);
    a.splice_range(parts);
  }
}


void bench_fill(std::size_t max_size){
  PerfEvent e;
  {
    e.setParam("method", "splice");
    
    for(std::size_t i = 1; i < max_size; i<<=1){
      e.setParam("size", i);
      PerfEventBlock block (e, i);
      msc::vector<int> a;
      fill_vector(a, i);
    }
  }

  {
    e.setParam("method", "copy");
    
    for(std::size_t i = 1; i < max_size; i<<=1){
      e.setParam("size", i);
      PerfEventBlock block (e, i);
      std::vector<int> a;
      fill_vector(a, i);
    }
  }
}

int main(){
  std::size_t test_size = (1ull << 20) + 1011;
  std::size_t reps = 10;
  bench_splice_multi(reps, 20, test_size);
}
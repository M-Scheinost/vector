#include <vector>

#include <msc/vector.hpp>
#include "bench/PerfEvent.hpp"

struct vma_region {
  std::uintptr_t start = 0;
  std::uintptr_t end   = 0;
  std::string perms;
  std::size_t offsetof;
  std::string device;
  std::size_t inode;
  std::string path_name;
  std::size_t bytes() const { return end - start; }

  std::string to_string(){
   return std::format("{:012x}-{:012x}\t{}\t{:>8}",
                   start, end, perms, bytes());
  }
};


/**
 * Every mapping the process currently holds, in address order, straight from
 * /proc/self/maps.
 */




inline std::vector<vma_region> read_vma_map(){
  std::vector<vma_region> out;
  std::ifstream maps("/proc/self/maps");
  std::string line;
  char zw_perms[5]  = {};
  char zw_device[6]  = {};
  char zw_path[100] = {};
  while(std::getline(maps, line)){
    vma_region r;

    if(line.size() > 50){
      std::sscanf(line.c_str(), "%lx-%lx %4s %ld %5s %ld %s", &r.start, &r.end, zw_perms, &r.offsetof, zw_device, &r.inode, zw_path);
      r.perms = zw_perms;
      r.device = zw_device;
      r.path_name =zw_path;
    }
    else {
      std::sscanf(line.c_str(), "%lx-%lx %4s %ld %5s %ld", &r.start, &r.end, zw_perms, &r.offsetof, zw_device, &r.inode);
      r.perms = zw_perms;
      r.device = zw_device;
    }
    out.push_back(r);
  }
  return out;
}

void print_vma_report(){
  auto mappings = read_vma_map();
  std::cout << "index\tstart-end\tperms\tsize" << std::endl;
  int i = 0;
  for(auto& map: mappings){
    std::cout << std::to_string(++i) << ":\t" << map.to_string() << std::endl;
  }
}



void print_anon_vma_report(){
  auto mappings = read_vma_map();
  std::cout << "index\tstart-end\tperms\tsize" << std::endl;
  int i = 0;
  for(auto& map: mappings){
    if(map.path_name.size() > 0) continue;
    std::cout << std::to_string(++i) << ":\t" << map.to_string() << std::endl;
  }
}




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
  
  
  std::size_t test_size = (1ull << 20); // + 1011;
  bench_fill(test_size);
  // std::size_t reps = 10;
  // bench_splice_multi(reps, 20, test_size);
}
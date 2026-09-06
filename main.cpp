#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <format>

#include "vector.hpp"


struct vma_region {
  std::uintptr_t start = 0;
  std::uintptr_t end   = 0;
  char perms[5] = {};                                   // e.g. "rw-p"
  std::size_t bytes() const { return end - start; }

  std::string to_string(){
   return std::format("0x{:012x}-0x{:012x}\t{}\tsize:\t{:>8}",
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
  while(std::getline(maps, line)){
    unsigned long s = 0, e = 0;
    char perms[5] = {};
    if(std::sscanf(line.c_str(), "%lx-%lx %4s", &s, &e, perms) != 3) continue;
    vma_region r;
    r.start = s;
    r.end   = e;
    std::memcpy(r.perms, perms, 4);
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




int main(){
  msc::vector<int> a;
  msc::vector<int> b;
  size_t counter = (1024*1024);
  for(size_t i = 0; i < counter; ++i){
    a.emplace_back(i);
    b.emplace_back(i + counter);
  }
  std::cout << "a:\t" << std::hex << a.data() << "\tb:\t" << b.data() << std::endl;
  print_vma_report();

  for(size_t i = 0; i < counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err"};
  }

  a.splice(b);
  
  std::cout << "a:\t" << std::hex << a.data() << "\tb:\t" << b.data() << std::endl;
  print_vma_report();

  for(size_t i = 0; i < counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err1: " + std::to_string(i)};
  }

  for(size_t i = counter; i < 2*counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err2 " + std::to_string(i)};
  }
 
}
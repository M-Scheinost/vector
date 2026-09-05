#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unistd.h>

#include "vector.hpp"


struct vma_region {
  std::uintptr_t start = 0;
  std::uintptr_t end   = 0;
  char perms[5] = {};                                   // e.g. "rw-p"
  std::size_t bytes() const { return end - start; }

  std::string to_string(){
    return "start:\t" + std::to_string(start)
      + "\tend:\t" + std::to_string(end)
      + "\t" + perms + "\tsize:\t" + std::to_string(bytes());
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




int main(){
  msc::vector<int> a {1 << 20};

  auto mappings = read_vma_map();
  for(auto& map: mappings){
    std::cout << map.to_string() << std::endl;
  }
 
}
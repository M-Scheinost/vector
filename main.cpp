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



int main(){
  msc::vector<int> a;
  msc::vector<int> b;
  size_t counter = (1024*1024);
  for(size_t i = 0; i < counter; ++i){
    a.emplace_back(i);
    b.emplace_back(i + counter);
  }
  std::cout << "a:\t" << std::hex << a.data() << "\tb:\t" << b.data() << std::endl;
  print_anon_vma_report();

  for(size_t i = 0; i < counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err"};
  }

  a.splice(std::move(b));
  
  std::cout << "a:\t" << std::hex << a.data() << "\tb:\t" << b.data() << std::endl;
  print_anon_vma_report();

  for(size_t i = 0; i < counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err1: " + std::to_string(i)};
  }

  for(size_t i = counter; i < 2*counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err2 " + std::to_string(i)};
  }
 
}
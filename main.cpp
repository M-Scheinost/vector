#include <msc/vector.hpp>


int main(){
  msc::vector<int> a;
  msc::vector<int> b;
  std::size_t counter = (1024*1024);
  for(std::size_t i = 0; i < counter; ++i){
    a.emplace_back(i);
    b.emplace_back(i + counter);
  }

  for(std::size_t i = 0; i < counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err"};
  }

  a.splice(std::move(b));

  for(std::size_t i = 0; i < counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err1: " + std::to_string(i)};
  }

  for(std::size_t i = counter; i < 2*counter; ++i){
    if(a[i] != static_cast<int>(i)) throw std::runtime_error {"err2 " + std::to_string(i)};
  }
 
}
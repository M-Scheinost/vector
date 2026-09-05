#pragma once

#include <sys/mman.h>
#include <type_traits>
#include <memory>
#include <new>
#include <stdexcept>
#include <cstring>
#include <algorithm>



namespace msc
{
template<class T>
class vector{

private:

  std::byte* data_ = nullptr;
  // in number of T elements
  std::size_t size_ = 0;
  // in bytes
  std::size_t capacity_ = 0;

  // used to grow / shrink the vector;
  static constexpr double size_multiplier = 1.25;
  // get the systems page size (4KiB for amd64, may vary with ARM)
  static constexpr std::size_t PAGE_SIZE = 4096; // sysconf(_SC_PAGESIZE);

  // set once splice() has stitched foreign pages into the mapping. The region
  // then spans several VMAs, and mremap refuses any range that is not a single
  // VMA (EFAULT), so resizing has to go through remap_by_copy() instead.
  bool fragmented_ = false;


  std::size_t roundup(std::size_t x, std::size_t a) {return (x+a-1) / a * a;}

  /**
   * Byte size of a mapping holding n elements, rounded up to whole pages.
   * Never returns 0: mmap and mremap reject zero length mappings with EINVAL,
   * so an empty vector still owns one page and data() stays a valid pointer.
   */
  std::size_t capacity_bytes(std::size_t n) {
    std::size_t bytes = roundup(n * sizeof(T), PAGE_SIZE);
    return bytes == 0 ? PAGE_SIZE : bytes;
  }

  /**
   * Grows the vector by the factor size_multiplier
   */
  void grow(){
    std::size_t new_capacity = static_cast<std::size_t>(static_cast<double>(capacity_) * size_multiplier);
    grow(new_capacity / sizeof(T));
  }

  void grow(std::size_t new_cap){
    static_assert(std::is_trivially_copyable_v<T>,"MREMAP_MAYMOVE grow requires trivial relocatability");
    std::size_t new_capacity = capacity_bytes(new_cap);
    void* p = mremap(static_cast<void*>(data_), capacity_, new_capacity,
                     MREMAP_MAYMOVE);
    if (p == MAP_FAILED) throw std::bad_alloc{};
    capacity_ = new_capacity;
    if(p != data_)
      data_ = reinterpret_cast<std::byte*>(std::launder(reinterpret_cast<T*>(p)));
  }

  /**
   * Shrinks the capacity of vector to size
   */
  void shrink(){
    shrink(size_);
  }

  /**
   * Shrinks the size of the vector to new size, cuts of elements if size > new_size to size
   */
  void shrink(std::size_t new_size){
    static_assert(std::is_trivially_copyable_v<T>,"MREMAP_MAYMOVE grow requires trivial relocatability");
    std::size_t new_capacity = capacity_bytes(new_size);
    if(new_capacity == capacity_) return;
    void* p = mremap(static_cast<void*>(data_), capacity_, new_capacity,
                     MREMAP_MAYMOVE);
    if (p == MAP_FAILED) throw std::bad_alloc{};
    capacity_ = new_capacity;
    if(p != data_)
      data_ = reinterpret_cast<std::byte*>(std::launder(reinterpret_cast<T*>(p)));
    if(size_ > new_size) size_ = new_size;
  }

  void init(){
     void* p = mmap(nullptr, capacity_,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                   -1, 0);
    if (p == MAP_FAILED) throw std::bad_alloc{};
    data_ = static_cast<std::byte*>(p);
  }


public:

  vector() : capacity_(PAGE_SIZE){
    init();
  }

  vector(std::size_t size){
    capacity_ = capacity_bytes(size);
    init();
  }

  ~vector(){
    std::destroy_n(data(), size_);
    if(data_) munmap(data_, capacity_);
  }

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Element access
//----------------------------------------------------------------------------------------------------------------------------------------------------
  T& at(std::size_t i){
    if (i >= size_) throw std::out_of_range("vector::at");
    return data()[i];
  }
  T& operator[](std::size_t i) { return data()[i]; }
  T& front(){return data()[0];}
  T& back(){return data()[size_ - 1];}
  T* data() { return reinterpret_cast<T*>(data_); }
//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Iterators
//----------------------------------------------------------------------------------------------------------------------------------------------------
  T* begin(){ return data(); }
  T* end(){ return data()+size_; }

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Capacity
//----------------------------------------------------------------------------------------------------------------------------------------------------
  bool empty(){return size_ == 0 ? true : false;}
  std::size_t size() const { return size_; }
  std::size_t max_size() const {return (1ull << 48) / sizeof(T);}
  void reserve(std::size_t new_cap){
    if((new_cap*sizeof(T)) > capacity_)
      grow(new_cap);
  }
  std::size_t capacity() const {return capacity_ / sizeof(T);}
  void shrink_to_fit(){shrink();}
//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Modifiers
//----------------------------------------------------------------------------------------------------------------------------------------------------
  void clear (){size_ = 0;}
  void insert();
  void insert_range();
  void emplace();
  void erase();
  void push_back(const T& val){
    if((size_+1)*sizeof(T) > capacity_) grow();
    data()[size_] = val;
    size_++;
  }
    
  template <class... Args>
  T& emplace_back(Args&&... a) {
    if ((size_+1)*sizeof(T) > capacity_) grow();
    T* slot = data() + size_;
    ::new (slot) T(std::forward<Args>(a)...);   // <-- construction happens HERE
    ++size_;
    return *slot;
  }

  void append_range();
  void pop_back(){size_--;}
  void resize();
  void swap();

  void splice(vector&& other){
    
  }
};



} // namespace msc
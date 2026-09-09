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
  static constexpr std::size_t max_capacity_ = (1ull<<40);


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
    std::size_t new_capacity = capacity_bytes(new_cap);
    if(capacity_ >= new_capacity) return;

    int success = mprotect(static_cast<void*>(data_), new_capacity, PROT_READ | PROT_WRITE);
    if (success) throw std::bad_alloc{};
    capacity_ = new_capacity;
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
    std::size_t new_capacity = capacity_bytes(new_size);
    if(new_capacity == capacity_) return;

    int success = mprotect(data_+new_capacity, capacity_ - new_capacity, PROT_NONE);
    if(success) throw std::bad_alloc{};
    success = madvise(data_+new_capacity, capacity_-new_capacity, MADV_DONTNEED);
    if(success) throw std::bad_alloc{};
    capacity_ = new_capacity;
    if(size_ > new_size) size_ = new_size;
  } 


  /**
   * Relocates a mapping onto new_pos. All sizes are in bytes. old_size has to
   * be the whole source mapping, otherwise the part beyond it stays mapped and
   * is leaked; new_size may be smaller, the kernel unmaps the excess.
   * new_pos must be page aligned, MREMAP_FIXED rejects anything else.
   */
  void move(std::byte* old_pos, std::size_t old_size,
            std::byte* new_pos, std::size_t new_size){
    void* p = mremap(static_cast<void*>(old_pos), old_size, new_size,
                     MREMAP_MAYMOVE | MREMAP_FIXED, static_cast<void*>(new_pos));
    if (p == MAP_FAILED) throw std::bad_alloc{};
    if(p != new_pos) throw std::runtime_error {"Moving the vector failed"};
  }


  void init(){
     void* p = mmap(nullptr, max_capacity_,
                    PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                    -1, 0);
    if(p == MAP_FAILED) throw std::bad_alloc{};
    data_ = static_cast<std::byte*>(p);
    int success = mprotect(static_cast<void*>(data_), capacity_, PROT_READ | PROT_WRITE);
    if(success) throw std::bad_alloc{}; 
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
    if(data_) munmap(data_, max_capacity_);
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
  void clear (){
    std::destroy_n(data(), size_);
    int success = madvise(data_, capacity_, MADV_DONTNEED);
    if(success) throw std::bad_alloc{};
    // success = mprotect(data_, capacity_, PROT_NONE);
    //  if(success) throw std::bad_alloc{};
    //  success = mprotect(data_, PAGE_SIZE, PROT_READ | PROT_WRITE);
    //  if(success) throw std::bad_alloc{};
    size_ = 0;
    // capacity_ = PAGE_SIZE;
  }
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
  void pop_back(){ --size_; std::destroy_at(data() + size_); }
  void resize();
  void swap();

  void splice(vector<T>&& other){
    if(this == &other || other.size_ == 0) return;

    const std::size_t tail  = size_ * sizeof(T);
    const std::size_t moved = capacity_bytes(other.size_);

    grow(size_ + other.size_);
    // need to redo -> currently we use memcpy if we aren't ending on a page bound
    // ideally we would like to still move with mremap and only copy the very "tail" at the end of the other vector
    // this way we might lose order of elements but we don't care most of the time since we only want set properties
    if(tail % PAGE_SIZE != 0){
      std::memcpy(data_ + tail, other.data_, other.size_ * sizeof(T));
      size_ += other.size_;
    } else {
      // hand over the whole source mapping so nothing of it is left behind
      move(other.data_, other.capacity_, data_ + tail, moved);
      size_ += other.size_;
    }

    munmap(other.data_, max_capacity_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }
};



} // namespace msc
#pragma once

#include <sys/mman.h>
#include <type_traits>
#include <memory>
#include <new>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <numeric>



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
  std::size_t rounddown(std::size_t x, std::size_t a) {return x / a * a;}

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

  explicit vector(std::size_t size){
    capacity_ = capacity_bytes(size);
    init();
  }

  vector(const vector& other) : capacity_(other.capacity_) {
    init();
    try{
      std::uninitialized_copy_n(reinterpret_cast<const T*>(other.data_),other.size_, data());
    } catch(...){
      munmap(data_, max_capacity_);
      throw;
    }
    size_ = other.size_;
  }
  vector(vector&& other) noexcept : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.size_     = 0;
    other.capacity_ = PAGE_SIZE;
    other.init();
  }

  vector& operator=(const vector& other){
    if(this == &other) return *this;

    // assigning to a moved from husk is allowed, but it owns no mapping yet
    if(!data_){
      capacity_ = PAGE_SIZE;
      init();
    }

    std::destroy_n(data(), size_);
    size_ = 0;

    // grow() counts elements and only ever grows, so a bigger capacity stays
    grow(other.size_);
    std::uninitialized_copy_n(reinterpret_cast<const T*>(other.data_),
                              other.size_, data());
    size_ = other.size_;
    return *this;
  }

  vector& operator=(vector&& other) noexcept {
    if(this == &other) return *this;

    std::destroy_n(data(), size_);
    if(data_) munmap(data_, max_capacity_);

    data_     = other.data_;
    size_     = other.size_;
    capacity_ = other.capacity_;

    other.size_     = 0;
    other.capacity_ = PAGE_SIZE;
    other.init();
    return *this;
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

  /**
   * Appends every element of other to this vector, leaving other empty.
   *
   * Wherever it can, the elements are handed over by remapping other's pages
   * onto this mapping, so nothing is copied however much is spliced. The
   * landing offset has to clear two bars at once: page aligned, or MREMAP_FIXED
   * rejects it, and a whole number of elements, or data()[i] stops addressing
   * elements across the seam. Both hold exactly at the multiples of
   * lcm(PAGE_SIZE, sizeof(T)), so the end of our elements is rounded down to
   * one and the few elements that rounding displaces are parked in a buffer
   * and put back after other's.
   *
   * ORDER IS NOT PRESERVED. The displaced elements come back at the end rather
   * than staying where they were, so the result holds the same elements as an
   * ordered append but not in the same sequence.
   *
   * other is left a husk: data_ is null and using it (data(), begin(), empty(),
   * emplace_back()) is undefined. Only its destructor is safe.
   */
  void splice(vector<T>&& other){
    if(this == &other || other.size_ == 0) return;

    constexpr std::size_t stride = std::lcm(sizeof(T), PAGE_SIZE);

    const std::size_t tail  = size_ * sizeof(T);
    const std::size_t moved = capacity_bytes(other.size_);


    const std::size_t pagebound_size = rounddown(tail, stride);
    const std::size_t keep  = pagebound_size / sizeof(T);
    const std::size_t spill = size_ - keep;

    grow(size_ + other.size_);

    if(spill == 0){
      // we already end on a boundary, so nothing of ours is in the way
      move(other.data_, other.capacity_, data_ + tail, moved);
    } else if(spill < other.size_){ // copy and of A since its cheaper than copying B
      const std::size_t spill_bytes = spill * sizeof(T);
      auto buf = std::make_unique_for_overwrite<std::byte[]>(spill_bytes);

      std::memcpy(buf.get(), data_ + pagebound_size, spill_bytes);
      move(other.data_, other.capacity_, data_ + pagebound_size, moved);
      std::memcpy(data_ + pagebound_size + other.size_ * sizeof(T), buf.get(), spill_bytes);
    } else {
      std::memcpy(data_ + tail, other.data_, other.size_ * sizeof(T));
    }
    size_ += other.size_;

    munmap(other.data_, max_capacity_);
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }
};



} // namespace msc
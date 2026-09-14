#include "gtest/gtest.h"
#include "./vector.hpp"



#include <vector>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <concepts>
#include <type_traits>

struct A {
  int a;
  double b;
  bool c;
  char d;
  uint64_t e;

  A(size_t i) : a(static_cast<int>(i)) , b(static_cast<double>(i)), c(static_cast<bool>(i)) , d(static_cast<char>(i)), e(i) {}
  bool operator==(const A& other) const {return a==other.a && b==other.b && c==other.c && d == other.d && e == other.e;}
};

/**
 * Non-trivially constructible and destructible, unlike A, so that the vector
 * has to run a real constructor and destructor for every element instead of
 * getting away with raw bytes. Every instance is counted, which lets a test
 * check the two are balanced and that nothing is constructed twice or leaked.
 *
 * The payload lives on the heap and the object holds no pointer into itself,
 * so an instance stays valid when its bytes are moved to another address.
 * A std::string member would not survive that: a short string points into its
 * own storage, which grow() and splice() would leave dangling.
 */
struct B {
  static inline size_t live        = 0;
  static inline size_t constructed = 0;
  static inline size_t destroyed   = 0;

  static void reset(){ live = 0; constructed = 0; destroyed = 0; }

  int* payload;

  explicit B(size_t i) : payload(new int(static_cast<int>(i))){
    ++live; ++constructed;
  }
  B(const B& other) : payload(new int(*other.payload)){
    ++live; ++constructed;
  }
  B& operator=(const B& other){
    *payload = *other.payload;
    return *this;
  }
  ~B(){
    delete payload;
    payload = nullptr;
    --live; ++destroyed;
  }

  int value() const { return *payload; }
  bool operator==(const B& other) const { return *payload == *other.payload; }
};

static_assert(!std::is_trivially_default_constructible_v<B>);
static_assert(!std::is_trivially_destructible_v<B>);
static_assert(!std::is_trivially_copyable_v<B>);
// A is the opposite on every count, which is what makes the pair useful
static_assert(std::is_trivially_destructible_v<A>);
static_assert(std::is_trivially_copyable_v<A>);


namespace {

constexpr size_t TEST_SIZE = 1024;

// mirrors msc::vector::PAGE_SIZE, which is private
constexpr size_t PAGE_SIZE = 4096;

/**
 * Appends n elements, element i holding the value i.
 * Takes the vector by reference because msc::vector is not copyable or movable yet.
 */
template<class T>
void fill(msc::vector<T>& v, size_t n){
  for(size_t i = 0; i < n; ++i){
    v.emplace_back(i);
  }
}

/**
 * Appends elements until the vector reallocates, so that accessors can be
 * checked against storage that has moved.
 */
template<class T>
void grow_once(msc::vector<T>& v){
  size_t old_capacity = v.capacity();
  while(v.capacity() == old_capacity){
    v.emplace_back(0);
  }
}

} // namespace


TEST(vector, init)
{

  msc::vector<int> a {};

  size_t size = 1024;
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(a.emplace_back(i));
  }
  for(size_t i = 0; i < size; ++i){
    ASSERT_EQ(i, a[i]);
  }

  size <<= 1;
  msc::vector<int> b {size};
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(b.emplace_back(i));
  }
  for(size_t i = 0; i < size; ++i){
    ASSERT_EQ(i, b[i]);
  }

  msc::vector<A> c (size);
  ASSERT_EQ(c.size(), 0);
  ASSERT_GE(c.capacity(), size);
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(b.emplace_back(i));
  }
  for(size_t i = 0; i < size; ++i){
    A test {i};
    ASSERT_EQ(test, b[i]);
  }

}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Element access
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, data)
{
  msc::vector<int> a {};
  // storage is mapped up front, so data() is never null
  ASSERT_NE(a.data(), nullptr);

  fill(a, TEST_SIZE);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a.data()[i], static_cast<int>(i));
  }

  a.data()[0] = 99;
  ASSERT_EQ(a.data()[0], 99);
  a.data()[0] = 0;

  for(size_t j = 0; j < 5; ++j){
    grow_once(a);
    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a.data()[i], static_cast<int>(i));
    }
  }
}

// operator[]
TEST(vector, subscript)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(&a[i], a.data() + i);
  }

  for(size_t i = 0; i < TEST_SIZE; ++i){
    a[i] = static_cast<int>(i * 2);
  }
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i * 2));
  }

  grow_once(a);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i * 2));
  }

  msc::vector<A> b {};
  fill(b, TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    A test {i};
    ASSERT_EQ(b[i], test);
    ASSERT_EQ(&b[i], b.data() + i);
  }
  b[0] = A{42};
  ASSERT_EQ(b[0], A{42});
}


TEST(vector, at)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  int zw;
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_NO_THROW(zw = a.at(i));
    ASSERT_EQ(zw, static_cast<int>(i));
  }

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(&a.at(i), &a[i]);
  }

  a.at(0) = 99;
  ASSERT_EQ(a.at(0), 99);

  ASSERT_THROW(a.at(TEST_SIZE), std::out_of_range);
  ASSERT_THROW(a.at(-1), std::out_of_range);

  msc::vector<int> b {};
  b.reserve(TEST_SIZE);
  ASSERT_THROW(b.at(0), std::out_of_range);
}


TEST(vector, front)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  ASSERT_EQ(a.front(), 0);
  ASSERT_EQ(&a.front(), a.data());

  a.front() = 99;
  ASSERT_EQ(a.front(), 99);
  ASSERT_EQ(a[0], 99);

  a.emplace_back(7);
  ASSERT_EQ(a.front(), 99);

  grow_once(a);
  ASSERT_EQ(a.front(), 99);
  ASSERT_EQ(&a.front(), a.data());
}


TEST(vector, back)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  ASSERT_EQ(a.back(), static_cast<int>(TEST_SIZE - 1));
  ASSERT_EQ(&a.back(), a.data() + a.size() - 1);

  a.back() = 99;
  ASSERT_EQ(a.back(), 99);
  ASSERT_EQ(a[a.size() - 1], 99);

  a.emplace_back(7);
  ASSERT_EQ(a.back(), 7);
  a.pop_back();
  ASSERT_EQ(a.back(), 99);

  grow_once(a);
  ASSERT_EQ(&a.back(), a.data() + a.size() - 1);

  msc::vector<int> b {};
  b.emplace_back(5);
  ASSERT_EQ(&b.front(), &b.back());
}

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Iterators
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, begin)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.begin(), a.end());

  fill(a, TEST_SIZE);

  ASSERT_EQ(a.begin(), a.data());
  ASSERT_EQ(*a.begin(), 0);
  ASSERT_EQ(&*a.begin(), &a.front());

  *a.begin() = 99;
  ASSERT_EQ(a[0], 99);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(&a.begin()[i], &a[i]);
  }

  grow_once(a);
  ASSERT_EQ(a.begin(), a.data());
  ASSERT_EQ(*a.begin(), 99);

  a.clear();
  ASSERT_EQ(a.begin(), a.data());
  ASSERT_EQ(a.begin(), a.end());
}


TEST(vector, end)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.end(), a.begin());

  fill(a, TEST_SIZE);

  ASSERT_EQ(a.end(), a.data() + a.size());
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(a.size()));
  ASSERT_EQ(&*(a.end() - 1), &a.back());
  ASSERT_EQ(*(a.end() - 1), static_cast<int>(TEST_SIZE - 1));

  a.emplace_back(7);
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE + 1));
  ASSERT_EQ(*(a.end() - 1), 7);
  a.pop_back();
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE));

  grow_once(a);
  ASSERT_EQ(a.end(), a.data() + a.size());

  a.clear();
  ASSERT_EQ(a.end(), a.begin());

  msc::vector<int> b {};
  b.reserve(TEST_SIZE * 4);
  fill(b, TEST_SIZE);
  ASSERT_EQ(b.end() - b.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE));
  ASSERT_LT(b.end(), b.data() + b.capacity());
}


TEST(vector, iteration)
{
  msc::vector<int> a {};

  using iterator = decltype(a.begin());
  static_assert(std::contiguous_iterator<iterator>);
  static_assert(std::same_as<std::iter_value_t<iterator>, int>);

  fill(a, TEST_SIZE);

  size_t i = 0;
  for(int& x : a){
    ASSERT_EQ(x, static_cast<int>(i));
    ASSERT_EQ(&x, &a[i]);
    ++i;
  }
  ASSERT_EQ(i, TEST_SIZE);

  for(int& x : a){
    x *= 2;
  }
  for(size_t j = 0; j < TEST_SIZE; ++j){
    ASSERT_EQ(a[j], static_cast<int>(j * 2));
  }

  ASSERT_EQ(std::accumulate(a.begin(), a.end(), 0LL),
            static_cast<long long>(TEST_SIZE - 1) * static_cast<long long>(TEST_SIZE));
  ASSERT_EQ(std::find(a.begin(), a.end(), 42), a.begin() + 21);
  ASSERT_EQ(std::find(a.begin(), a.end(), -1), a.end());
  ASSERT_TRUE(std::is_sorted(a.begin(), a.end()));

  std::vector<int> copy(a.begin(), a.end());
  ASSERT_EQ(copy.size(), TEST_SIZE);
  ASSERT_TRUE(std::equal(a.begin(), a.end(), copy.begin(), copy.end()));

  // mutating algorithms rearrange the storage in place
  std::reverse(a.begin(), a.end());
  ASSERT_EQ(a.front(), static_cast<int>((TEST_SIZE - 1) * 2));
  ASSERT_EQ(a.back(), 0);
  std::sort(a.begin(), a.end());
  ASSERT_TRUE(std::equal(a.begin(), a.end(), copy.begin(), copy.end()));

  msc::vector<A> b {};
  fill(b, TEST_SIZE);
  size_t k = 0;
  for(const A& x : b){
    ASSERT_EQ(x, A{k});
    ASSERT_EQ(&x, &b[k]);
    ++k;
  }
  ASSERT_EQ(k, TEST_SIZE);
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Capacity
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, empty)
{
  msc::vector<int> a {};
  ASSERT_TRUE(a.empty());

  a.emplace_back(1);
  ASSERT_FALSE(a.empty());

  a.pop_back();
  ASSERT_TRUE(a.empty());

  fill(a, TEST_SIZE);
  ASSERT_FALSE(a.empty());

  a.clear();
  ASSERT_TRUE(a.empty());

  msc::vector<int> b {};
  b.reserve(TEST_SIZE);
  ASSERT_TRUE(b.empty());

  msc::vector<int> c (TEST_SIZE);
  ASSERT_TRUE(c.empty());

  ASSERT_EQ(a.empty(), a.size() == 0);
  ASSERT_EQ(a.empty(), a.begin() == a.end());
}


TEST(vector, size)
{
  msc::vector<int> a {};
  ASSERT_EQ(a.size(), 0);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    a.emplace_back(i);
    ASSERT_EQ(a.size(), i + 1);
  }

  for(size_t i = TEST_SIZE; i > 0; --i){
    ASSERT_EQ(a.size(), i);
    a.pop_back();
  }
  ASSERT_EQ(a.size(), 0);

  fill(a, TEST_SIZE);
  a.push_back(7);
  ASSERT_EQ(a.size(), TEST_SIZE + 1);

  ASSERT_EQ(a.size(), static_cast<size_t>(a.end() - a.begin()));

  a.clear();
  ASSERT_EQ(a.size(), 0);

  msc::vector<int> b {};
  fill(b, TEST_SIZE);
  grow_once(b);
  size_t grown = b.size();
  b.reserve(b.capacity() * 4);
  ASSERT_EQ(b.size(), grown);
}


TEST(vector, max_size)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.max_size(), (1ull << 48) / sizeof(int));

  size_t before = a.max_size();
  fill(a, TEST_SIZE);
  ASSERT_EQ(a.max_size(), before);
  a.clear();
  ASSERT_EQ(a.max_size(), before);

  ASSERT_GT(a.max_size(), a.size());
  ASSERT_GT(a.max_size(), a.capacity());

  static_assert(sizeof(A) > sizeof(int));
  msc::vector<A> b {};
  ASSERT_EQ(b.max_size(), (1ull << 48) / sizeof(A));
  ASSERT_LT(b.max_size(), a.max_size());
}


TEST(vector, capacity)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.capacity(), PAGE_SIZE / sizeof(int));

  size_t last = a.capacity();
  for(size_t i = 0; i < TEST_SIZE * 8; ++i){
    a.emplace_back(i);
    ASSERT_GE(a.capacity(), a.size());
    ASSERT_GE(a.capacity(), last);
    last = a.capacity();
  }

  ASSERT_EQ((a.capacity() * sizeof(int)) % PAGE_SIZE, 0);

  size_t grown = a.capacity();
  a.pop_back();
  ASSERT_EQ(a.capacity(), grown);
  a.clear();
  ASSERT_EQ(a.capacity(), grown);

  msc::vector<int> b (TEST_SIZE);
  ASSERT_GE(b.capacity(), TEST_SIZE);
  ASSERT_EQ(b.size(), 0);

  msc::vector<int> d (0);
  ASSERT_EQ(d.capacity(), PAGE_SIZE / sizeof(int));
  ASSERT_EQ(d.size(), 0);
  ASSERT_NE(d.data(), nullptr);

  msc::vector<A> c {};
  ASSERT_EQ(c.capacity(), PAGE_SIZE / sizeof(A));
  ASSERT_LT(c.capacity(), a.capacity());
}


TEST(vector, shrink_to_fit)
{
  msc::vector<int> a {};
  a.reserve(TEST_SIZE * 16);
  fill(a, TEST_SIZE);

  size_t grown = a.capacity();
  ASSERT_GT(grown, a.size());

  a.shrink_to_fit();

  ASSERT_LT(a.capacity(), grown);
  ASSERT_GE(a.capacity(), a.size());
  ASSERT_EQ(a.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  a.emplace_back(7);
  ASSERT_EQ(a.back(), 7);
  ASSERT_EQ(a.size(), TEST_SIZE + 1);

  a.shrink_to_fit();
  size_t fitted = a.capacity();
  a.shrink_to_fit();
  ASSERT_EQ(a.capacity(), fitted);

  msc::vector<int> b {};
  b.reserve(TEST_SIZE * 16);
  fill(b, TEST_SIZE);
  b.clear();
  ASSERT_NO_THROW(b.shrink_to_fit());
  ASSERT_EQ(b.size(), 0);
  ASSERT_TRUE(b.empty());

  msc::vector<int> c {};
  ASSERT_NO_THROW(c.shrink_to_fit());
  ASSERT_EQ(c.size(), 0);

  ASSERT_EQ(c.capacity(), PAGE_SIZE / sizeof(int));
  ASSERT_NE(c.data(), nullptr);
  ASSERT_EQ(c.begin(), c.end());
  c.emplace_back(7);
  ASSERT_EQ(c.back(), 7);
}


TEST(vector, reserve)
{
  msc::vector<int> a {};
  size_t size = 1024;
  a.reserve(size);
  ASSERT_EQ(a.size(), 0);
  ASSERT_GE(a.capacity(), size);

  size <<= 1;
  a.reserve(size);
  ASSERT_EQ(a.size(), 0);
  ASSERT_GE(a.capacity(), size);
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(a.emplace_back(i));
  }
  ASSERT_EQ(a.size(), size);
  ASSERT_GE(a.capacity(), size);

  size >>= 1;
  a.reserve(size);
  ASSERT_EQ(a.size(), (size << 1));
  ASSERT_GE(a.capacity(), size);


  msc::vector<A> b (size);
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);

  size <<= 1;
  b.reserve(size);
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);

  size >>= 1;
  b.reserve(size);
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Modifiers
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, splice)
{
  // TEST_SIZE ints is exactly one page, so the append starts on a page
  // boundary and takes the zero copy path that relocates the source pages
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, TEST_SIZE);
  fill(b, TEST_SIZE);
  ASSERT_EQ((a.size() * sizeof(int)) % PAGE_SIZE, 0);

  a.splice(std::move(b));

  // size has to account for the elements that came across
  ASSERT_EQ(a.size(), TEST_SIZE * 2);
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE * 2));
  ASSERT_GE(a.capacity(), TEST_SIZE * 2);

  // the destination keeps its own elements, which is what the byte vs element
  // mix up used to destroy from index 262144 onwards
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }
  // followed by every element of the source, in order
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[TEST_SIZE + i], static_cast<int>(i));
  }

  // and they are one contiguous run, readable through the iterators
  ASSERT_EQ(a.front(), 0);
  ASSERT_EQ(a.back(), static_cast<int>(TEST_SIZE - 1));
  size_t seen = 0;
  for(int& x : a){
    ASSERT_EQ(x, static_cast<int>(seen % TEST_SIZE));
    ++seen;
  }
  ASSERT_EQ(seen, TEST_SIZE * 2);
}


TEST(vector, splice_unaligned)
{
  // three ints do not reach one stride, so the landing offset rounds down to 0:
  // the source takes the front of the mapping and the destination's own three
  // elements are parked in a buffer and put back behind it. splice does not
  // preserve order, so the layout is source first, displaced elements last
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, 3);
  fill(b, TEST_SIZE);
  ASSERT_NE((a.size() * sizeof(int)) % PAGE_SIZE, 0);

  a.splice(std::move(b));

  ASSERT_EQ(a.size(), TEST_SIZE + 3);
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE + 3));
  ASSERT_GE(a.capacity(), TEST_SIZE + 3);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }
  for(size_t i = 0; i < 3; ++i){
    ASSERT_EQ(a[TEST_SIZE + i], static_cast<int>(i));
  }
}


TEST(vector, splice_reordering_keeps_every_element)
{
  // whatever path splice takes, the result has to hold exactly the elements of
  // both inputs. checked as a multiset, which is the contract now that the
  // displaced elements no longer stay in place
  for(size_t prefix : {size_t{1}, size_t{2}, PAGE_SIZE/sizeof(int) - 1,
                       PAGE_SIZE/sizeof(int), PAGE_SIZE/sizeof(int) + 1,
                       TEST_SIZE + 7}){
    msc::vector<int> a {};
    msc::vector<int> b {};
    fill(a, prefix);
    fill(b, TEST_SIZE * 4);

    a.splice(std::move(b));
    ASSERT_EQ(a.size(), prefix + TEST_SIZE * 4) << "prefix " << prefix;

    std::vector<int> got(a.begin(), a.end());
    std::vector<int> want;
    for(size_t i = 0; i < prefix; ++i)          want.push_back(static_cast<int>(i));
    for(size_t i = 0; i < TEST_SIZE * 4; ++i)   want.push_back(static_cast<int>(i));
    std::sort(got.begin(), got.end());
    std::sort(want.begin(), want.end());
    ASSERT_EQ(got, want) << "prefix " << prefix;
  }
}


TEST(vector, splice_stride_exceeds_page)
{
  // sizeof(C) is 24, which does not divide the page size, so a landing offset
  // has to be a multiple of lcm(4096, 24) == 12288 rather than of 4096. any
  // page boundary that is not also an element boundary would slice an element
  struct C {
    uint64_t a, b, c;
    explicit C(size_t i) : a(i), b(i*2), c(i*3) {}
    bool operator==(const C& o) const {return a==o.a && b==o.b && c==o.c;}
  };
  static_assert(sizeof(C) == 24);
  static_assert(PAGE_SIZE % sizeof(C) != 0);

  const size_t stride_elems = std::lcm(sizeof(C), PAGE_SIZE) / sizeof(C);
  ASSERT_EQ(stride_elems, 512u);

  for(size_t prefix : {size_t{1}, stride_elems - 1, stride_elems,
                       stride_elems + 3, stride_elems * 2 + 100}){
    msc::vector<C> a {};
    msc::vector<C> b {};
    fill(a, prefix);
    fill(b, stride_elems * 4);

    a.splice(std::move(b));
    ASSERT_EQ(a.size(), prefix + stride_elems * 4) << "prefix " << prefix;

    // every element still reads back as a whole C, which is what a landing
    // offset in the middle of an element would destroy
    std::vector<uint64_t> got;
    for(C& x : a){
      ASSERT_EQ(x.b, x.a * 2) << "prefix " << prefix;
      ASSERT_EQ(x.c, x.a * 3) << "prefix " << prefix;
      got.push_back(x.a);
    }
    std::vector<uint64_t> want;
    for(size_t i = 0; i < prefix; ++i)              want.push_back(i);
    for(size_t i = 0; i < stride_elems * 4; ++i)    want.push_back(i);
    std::sort(got.begin(), got.end());
    std::sort(want.begin(), want.end());
    ASSERT_EQ(got, want) << "prefix " << prefix;
  }
}


TEST(vector, splice_edge_cases)
{
  // an empty source leaves the destination untouched
  msc::vector<int> a {};
  msc::vector<int> empty {};
  fill(a, TEST_SIZE);
  a.splice(std::move(empty));
  ASSERT_EQ(a.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  // splicing a vector into itself is a no op rather than a corruption
  a.splice(std::move(a));
  ASSERT_EQ(a.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  // an empty destination takes everything, appending at offset 0
  msc::vector<int> c {};
  msc::vector<int> d {};
  fill(d, TEST_SIZE);
  c.splice(std::move(d));
  ASSERT_EQ(c.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(c[i], static_cast<int>(i));
  }

  // a class type splices the same way. sizeof(A) divides the page size, so
  // filling one page worth keeps the append page aligned
  const size_t per_page = PAGE_SIZE / sizeof(A);
  msc::vector<A> e {};
  msc::vector<A> f {};
  fill(e, per_page);
  fill(f, 64);
  e.splice(std::move(f));
  ASSERT_EQ(e.size(), per_page + 64);
  for(size_t i = 0; i < per_page; ++i){
    ASSERT_EQ(e[i], A{i});
  }
  for(size_t i = 0; i < 64; ++i){
    ASSERT_EQ(e[per_page + i], A{i});
  }
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Object lifetime, with a type the vector cannot treat as raw bytes
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, non_trivial_lifetime)
{
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, TEST_SIZE);

    // emplace_back constructs in place, exactly once per element
    ASSERT_EQ(B::constructed, TEST_SIZE);
    ASSERT_EQ(B::destroyed, 0u);
    ASSERT_EQ(B::live, TEST_SIZE);

    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }

    // growing extends the mapping in place, so the elements already there are
    // neither relocated nor destroyed and no copy constructor runs for them
    size_t before = B::constructed;
    grow_once(a);
    ASSERT_EQ(B::destroyed, 0u);
    ASSERT_EQ(B::constructed - before, a.size() - TEST_SIZE);
    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }
  }
  // leaving the scope runs one destructor per live element, and no more
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, non_trivial_clear)
{
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, TEST_SIZE);
    ASSERT_EQ(B::live, TEST_SIZE);

    a.clear();

    // clear() destroys what it drops rather than just forgetting the count
    ASSERT_EQ(B::live, 0u);
    ASSERT_EQ(B::destroyed, TEST_SIZE);
    ASSERT_EQ(a.size(), 0u);
    ASSERT_TRUE(a.empty());

    // and the storage stays usable afterwards
    fill(a, TEST_SIZE);
    ASSERT_EQ(B::live, TEST_SIZE);
    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, non_trivial_pop_back)
{
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, TEST_SIZE);

    // pop_back destroys the element it drops, it does not just forget the count
    for(size_t i = 0; i < TEST_SIZE; ++i){
      a.pop_back();
      ASSERT_EQ(B::live, TEST_SIZE - i - 1);
    }
    ASSERT_EQ(a.size(), 0u);
    ASSERT_TRUE(a.empty());
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, non_trivial_reserve)
{
  B::reset();
  {
    msc::vector<B> a {};
    a.reserve(TEST_SIZE * 4);

    // reserve only maps storage, it must not construct anything into it
    ASSERT_EQ(B::constructed, 0u);
    ASSERT_EQ(a.size(), 0u);
    ASSERT_GE(a.capacity(), TEST_SIZE * 4);

    fill(a, TEST_SIZE);
    ASSERT_EQ(B::constructed, TEST_SIZE);
    ASSERT_EQ(B::live, TEST_SIZE);
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}

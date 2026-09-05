#include "gtest/gtest.h"
#include "./vector.hpp"



#include <vector>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <concepts>

struct A {
  int a;
  double b;
  bool c;
  char d;
  uint64_t e;

  A(size_t i) : a(static_cast<int>(i)) , b(static_cast<double>(i)), c(static_cast<bool>(i)) , d(static_cast<char>(i)), e(i) {}
  bool operator==(const A& other) const {return a==other.a && b==other.b && c==other.c && d == other.d && e == other.e;}
};


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

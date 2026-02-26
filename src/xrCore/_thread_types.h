#pragma once

#include <ppl.h>
#include <concurrent_unordered_map.h>
#include <concurrent_vector.h>
#include <atomic>
#include <functional>
#include <algorithm>
#include <iterator>
#include <type_traits>

// Atomic types
using xr_atomic_u32 = std::atomic_uint32_t;
using xr_atomic_s32 = std::atomic_int;
using xr_atomic_bool = std::atomic_bool;

// Tasks Redefinition
using xr_task_group = concurrency::task_group;

template <typename T, typename U>
using xr_concurrent_unordered_map = concurrency::concurrent_unordered_map<T, U>;

template <typename T, typename allocator = xalloc<T>>
using xr_concurrent_vector = concurrency::concurrent_vector<T, allocator>;

template<typename BlockRangeType, typename Body>
inline void xr_parallel_for(BlockRangeType Begin, BlockRangeType End, Body Functor)
{
	concurrency::parallel_for(Begin, End, Functor);
}

template<typename Index, typename Body>
inline void xr_parallel_foreach(Index Begin, Index End, Body Functor)
{
	concurrency::parallel_for_each(Begin, End, Functor);
}
// Helper to deduce the value type for the default predicate
template <typename RandomIt>
using IterValueT = typename std::iterator_traits<RandomIt>::value_type;

// Helper to check if an iterator is Random Access
template <typename It>
using IsRandomAccess = std::is_base_of<
    std::random_access_iterator_tag,
    typename std::iterator_traits<It>::iterator_category
>;

template <typename T, typename P>
ICF void CAS(T& a, T& b, P& pred)
{
    if (pred(b, a)) std::swap(a, b);
}

template <typename RandomIt, typename P>
ICF void Sort2(RandomIt d, P& pred)
{
    CAS(d[0], d[1], pred);
}

template <typename RandomIt, typename P>
ICF void Sort3(RandomIt d, P& pred)
{
    CAS(d[0], d[1], pred); CAS(d[0], d[2], pred); CAS(d[1], d[2], pred);
}

template <typename RandomIt, typename P>
ICF void Sort4(RandomIt d, P& pred)
{
    CAS(d[0], d[1], pred); CAS(d[2], d[3], pred);
    CAS(d[0], d[2], pred); CAS(d[1], d[3], pred);
    CAS(d[1], d[2], pred);
}

template <typename RandomIt, typename P>
ICF void Sort5(RandomIt d, P& pred)
{
    CAS(d[0], d[1], pred); CAS(d[3], d[4], pred); CAS(d[2], d[4], pred);
    CAS(d[2], d[3], pred); CAS(d[0], d[3], pred); CAS(d[0], d[2], pred);
    CAS(d[1], d[4], pred); CAS(d[1], d[3], pred); CAS(d[1], d[2], pred);
}

template <typename RandomIt, typename P>
ICF void Sort6(RandomIt d, P& pred)
{
    CAS(d[1], d[2], pred); CAS(d[4], d[5], pred); CAS(d[0], d[2], pred);
    CAS(d[3], d[5], pred); CAS(d[0], d[1], pred); CAS(d[3], d[4], pred);
    CAS(d[2], d[5], pred); CAS(d[0], d[3], pred); CAS(d[1], d[4], pred);
    CAS(d[2], d[4], pred); CAS(d[1], d[3], pred); CAS(d[2], d[3], pred);
}

template <typename RandomIt, typename P>
ICF void Sort7(RandomIt d, P& pred)
{
    CAS(d[1], d[2], pred); CAS(d[3], d[4], pred); CAS(d[5], d[6], pred); CAS(d[0], d[2], pred);
    CAS(d[3], d[5], pred); CAS(d[4], d[6], pred); CAS(d[0], d[1], pred); CAS(d[4], d[5], pred);
    CAS(d[2], d[6], pred); CAS(d[0], d[4], pred); CAS(d[1], d[5], pred); CAS(d[0], d[3], pred);
    CAS(d[2], d[5], pred); CAS(d[1], d[3], pred); CAS(d[2], d[4], pred); CAS(d[2], d[3], pred);
}

template <typename RandomIt, typename P>
ICF void Sort8(RandomIt d, P& pred)
{
    CAS(d[0], d[1], pred); CAS(d[2], d[3], pred); CAS(d[4], d[5], pred); CAS(d[6], d[7], pred);
    CAS(d[0], d[2], pred); CAS(d[1], d[3], pred); CAS(d[4], d[6], pred); CAS(d[5], d[7], pred);
    CAS(d[1], d[2], pred); CAS(d[5], d[6], pred); CAS(d[0], d[4], pred); CAS(d[3], d[7], pred);
    CAS(d[1], d[5], pred); CAS(d[2], d[6], pred); CAS(d[1], d[4], pred); CAS(d[3], d[6], pred);
    CAS(d[2], d[4], pred); CAS(d[3], d[5], pred); CAS(d[3], d[4], pred);
}

template<typename RandomIt, typename P = std::less<IterValueT<RandomIt>>>
IC void xr_sort(RandomIt first, RandomIt last, P pred = {})
{
    static_assert(IsRandomAccess<RandomIt>::value,
        "xr_sort requires Random Access Iterators (vector, array, deque). ");

    size_t count = std::distance(first, last);
    switch (count)
    {
        case 0:
        case 1: break;
        case 2: Sort2(first, pred); break;
        case 3: Sort3(first, pred); break;
        case 4: Sort4(first, pred); break;
        case 5: Sort5(first, pred); break;
        case 6: Sort6(first, pred); break;
        case 7: Sort7(first, pred); break;
        case 8: Sort8(first, pred); break;
        default: std::sort(first, last, pred); break;
    }
}

// PPL behaviour - fallback to std::sort if chunk size < 2048 and cores < 2
template<typename RandomIt, typename P = std::less<IterValueT<RandomIt>>>
IC void xr_parallel_sort(RandomIt first, RandomIt last, P pred = {})
{
    concurrency::parallel_sort(first, last, pred);
}

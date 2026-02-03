////////////////////////////////////////////////////////////////////////////
// Module : intrusive_ptr.h
// Created : 30.07.2004
// Modified : 04.02.2026
// Author : Dmitriy Iassenev, demonized
// Description : Intrusive pointer template (C++17)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include <type_traits>
#include <utility> // for std::swap
#include <cstddef> // for std::nullptr_t
#include "_thread_types.h"

struct intrusive_base
{
	xr_atomic_u32 m_ref_count;

	IC intrusive_base() : m_ref_count(0) {}

    // Support for intrusive_ptr<base> b = derived;
    IC virtual ~intrusive_base() {}

	template <typename T>
	IC void _release(T* object)
	{
		xr_delete(object);
	}
};

#define TEMPLATE_SPECIALIZATION template <typename object_type, typename base_type>
#define _intrusive_ptr intrusive_ptr<object_type,base_type>

template <typename object_type, typename base_type = intrusive_base>
class intrusive_ptr
{
public:
    typedef base_type base_type;
    typedef object_type object_type;
    typedef intrusive_ptr<object_type, base_type> self_type;

private:
    // Static check instead of the old enum hack
    static_assert(std::is_base_of_v<base_type, object_type> || std::is_same_v<base_type, object_type>,
        "intrusive_ptr<T>: T must be derived from intrusive_base");

    object_type* m_object;

protected:
    IC void dec();

public:
    // Allow intrusive_ptrs of different types to access private m_object for conversion
    template <typename U, typename V> friend class intrusive_ptr;

    IC intrusive_ptr() noexcept;
    IC intrusive_ptr(object_type* rhs);
    IC intrusive_ptr(self_type const& rhs);

    // Move Constructor
    IC intrusive_ptr(self_type&& rhs) noexcept;

    // Generalized Copy Constructor (Derived -> Base)
    template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int> = 0>
    IC intrusive_ptr(intrusive_ptr<other_type, base_type> const& rhs);

    IC ~intrusive_ptr();

    IC self_type& operator=(object_type* rhs);
    IC self_type& operator=(self_type const& rhs);

    // Move Assignment
    IC self_type& operator=(self_type&& rhs) noexcept;

    // Generalized Assignment
    template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int> = 0>
    IC self_type& operator=(intrusive_ptr<other_type, base_type> const& rhs);

    // Accessors
    IC object_type& operator*() const;
    IC object_type* operator->() const;

    // Boolean Conversion
    // Replaces the old "unspecified_bool_type" hack with modern standard
    explicit IC operator bool() const noexcept
    {
        return m_object != nullptr;
    }

    // Legacy support for !ptr checks
    IC bool operator!() const noexcept
    {
        return m_object == nullptr;
    }

    // Utilities
    IC u32 size();
    IC void swap(self_type& rhs) noexcept;
    IC bool equal(const self_type& rhs) const noexcept;

    IC void set(object_type* rhs);
    IC void set(self_type const& rhs);
    IC object_type* get() const noexcept;

private:
	IC void release_internal(object_type* object);
};

TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

TEMPLATE_SPECIALIZATION
IC bool operator!=(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

TEMPLATE_SPECIALIZATION
IC bool operator<(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

TEMPLATE_SPECIALIZATION
IC bool operator>(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

// Modern nullptr comparisons
TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, std::nullptr_t) noexcept { return !a; }

TEMPLATE_SPECIALIZATION
IC bool operator==(std::nullptr_t, _intrusive_ptr const& a) noexcept { return !a; }

TEMPLATE_SPECIALIZATION
IC bool operator!=(_intrusive_ptr const& a, std::nullptr_t) noexcept { return (bool)a; }

TEMPLATE_SPECIALIZATION
IC bool operator!=(std::nullptr_t, _intrusive_ptr const& a) noexcept { return (bool)a; }

TEMPLATE_SPECIALIZATION
IC void swap(_intrusive_ptr& lhs, _intrusive_ptr& rhs) noexcept;


// Implementation

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr() noexcept
{
    m_object = nullptr;
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(object_type* rhs)
{
    m_object = nullptr;
    set(rhs);
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(self_type const& rhs)
{
    m_object = nullptr;
    set(rhs);
}

// Move Constructor
TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(self_type&& rhs) noexcept
    : m_object(rhs.m_object)
{
    rhs.m_object = nullptr;
}

// Generalized Constructor (Derived -> Base)
TEMPLATE_SPECIALIZATION
template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int>>
IC _intrusive_ptr::intrusive_ptr(intrusive_ptr<other_type, base_type> const& rhs)
{
    m_object = nullptr;
    set(rhs.get());
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::~intrusive_ptr()
{
    dec();
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::dec()
{
    if (m_object)
    {
        object_type* temp = m_object;
        m_object = nullptr;
        release_internal(temp);
    }
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::release_internal(object_type* obj)
{
    if (obj->m_ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        std::atomic_thread_fence(std::memory_order_acquire);
        obj->_release(obj);
    }
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(object_type* rhs)
{
    set(rhs);
    return (*this);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(self_type const& rhs)
{
    set(rhs);
    return (*this);
}

// Move Assignment
TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(self_type&& rhs) noexcept
{
    if (this != &rhs)
    {
        dec();
        m_object = rhs.m_object;
        rhs.m_object = nullptr;
    }
    return *this;
}

// Generalized Assignment
TEMPLATE_SPECIALIZATION
template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int>>
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(intrusive_ptr<other_type, base_type> const& rhs)
{
    set(rhs.get());
    return *this;
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::object_type& _intrusive_ptr::operator*() const
{
    VERIFY(m_object);
    return (*m_object);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::object_type* _intrusive_ptr::operator->() const
{
    VERIFY(m_object);
    return (m_object);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::swap(self_type& rhs) noexcept
{
    std::swap(m_object, rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC bool _intrusive_ptr::equal(const self_type& rhs) const noexcept
{
    return (m_object == rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::set(object_type* rhs)
{
    if (rhs)
        rhs->m_ref_count.fetch_add(1, std::memory_order_relaxed);

    object_type* old = m_object;
    m_object = rhs;

    if (old)
        release_internal(old);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::set(self_type const& rhs)
{
    set(rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::object_type* _intrusive_ptr::get() const noexcept
{
    return (m_object);
}

TEMPLATE_SPECIALIZATION
IC u32 _intrusive_ptr::size()
{
    return m_object ? m_object->m_ref_count.load(std::memory_order_relaxed) : 0;
}

// Operator Implementations

TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() == b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator!=(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() != b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator<(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() < b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator>(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() > b.get());
}

TEMPLATE_SPECIALIZATION
IC void swap(_intrusive_ptr& lhs, _intrusive_ptr& rhs) noexcept
{
    lhs.swap(rhs);
}

template <typename T, typename... Args>
IC intrusive_ptr<T> make_intrusive(Args&&... args)
{
    return intrusive_ptr<T>(xr_new<T>(std::forward<Args>(args)...));
}

#undef TEMPLATE_SPECIALIZATION
#undef _intrusive_ptr

////////////////////////////////////////////////////////////////////////////
// Module : intrusive_ptr.h
// Created : 30.07.2004
// Modified : 30.07.2004
// Author : Dmitriy Iassenev
// Description : Intrusive pointer template
////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef XRGAME_EXPORTS
# include "object_type_traits.h"
#endif //XRGAME_EXPORTS

#include "_thread_types.h"

#pragma pack(push,4)

struct intrusive_base
{
	xr_atomic_u32 m_ref_count;

	IC intrusive_base() : m_ref_count(0)
	{
	}

	template <typename T>
	IC void _release(T* object)
	{
		xr_delete(object);
	}
};

template <typename object_type, typename base_type = intrusive_base>
class intrusive_ptr
{
private:
	typedef base_type base_type;
	typedef object_type object_type;
	typedef intrusive_ptr<object_type, base_type> self_type;
	typedef const object_type* (intrusive_ptr::*unspecified_bool_type)() const;
	template <typename U, typename V> friend class intrusive_ptr;

private:
#ifdef XRGAME_EXPORTS
    enum
    {
        result =
        object_type_traits::is_base_and_derived<base_type, object_type>::value ||
        object_type_traits::is_same<base_type, object_type>::value
    };
#else
	enum { result = true };
#endif
private:
	object_type* m_object;

protected:
	IC void dec();

public:
	IC intrusive_ptr();
	IC intrusive_ptr(object_type* rhs);
	IC intrusive_ptr(self_type const& rhs);

	template <typename other_type>
	IC intrusive_ptr(intrusive_ptr<other_type, base_type> const& rhs);

	IC ~intrusive_ptr();
	IC self_type& operator=(object_type* rhs);
	IC self_type& operator=(self_type const& rhs);

	template <typename other_type>
	IC intrusive_ptr<object_type, base_type>& operator=(intrusive_ptr<other_type, base_type> const& rhs);

	IC object_type& operator*() const;
	IC object_type* operator->() const;
	IC bool operator!() const;
	IC operator unspecified_bool_type() const { return (!m_object ? 0 : &intrusive_ptr::get); }
	IC u32 size();
	IC void swap(self_type& rhs);
	IC bool equal(const self_type& rhs) const;
	IC void set(object_type* rhs);
	IC void set(self_type const& rhs);
	IC const object_type* get() const;
};

template <typename object_type, typename base_type>
IC bool operator==(intrusive_ptr<object_type, base_type> const& a, intrusive_ptr<object_type, base_type> const& b);

template <typename object_type, typename base_type>
IC bool operator!=(intrusive_ptr<object_type, base_type> const& a, intrusive_ptr<object_type, base_type> const& b);

template <typename object_type, typename base_type>
IC bool operator<(intrusive_ptr<object_type, base_type> const& a, intrusive_ptr<object_type, base_type> const& b);

template <typename object_type, typename base_type>
IC bool operator>(intrusive_ptr<object_type, base_type> const& a, intrusive_ptr<object_type, base_type> const& b);

template <typename object_type, typename base_type>
IC void swap(intrusive_ptr<object_type, base_type>& lhs, intrusive_ptr<object_type, base_type>& rhs);

#define TEMPLATE_SPECIALIZATION template <typename object_type, typename base_type>
#define _intrusive_ptr intrusive_ptr<object_type,base_type>

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr()
{
	m_object = 0;
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(object_type* rhs)
{
	m_object = 0;
	set(rhs);
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(self_type const& rhs)
{
	m_object = 0;
	set(rhs);
}

// Generalized Constructor
// Allows: intrusive_ptr<CPS_Instance> p = intrusive_ptr<CParticlesObject>(...);
TEMPLATE_SPECIALIZATION
template <typename other_type>
IC _intrusive_ptr::intrusive_ptr(intrusive_ptr<other_type, base_type> const& rhs)
{
	m_object = 0;
	set(rhs.get());
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::~intrusive_ptr()
{
	STATIC_CHECK(result, Class_MUST_Be_Derived_From_The_Base);
	dec();
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::dec()
{
	if (!m_object)
		return;

	auto prev = m_object->base_type::m_ref_count.fetch_sub(1, std::memory_order_acq_rel);
	if (prev == 1)
	{
		m_object->base_type::_release(m_object);
		m_object = 0;
	}

}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(object_type* rhs)
{
	set(rhs);
	return ((self_type&)*this);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(self_type const& rhs)
{
	set(rhs);
	return ((self_type&)*this);
}

// Generalized Assignment Operator
// Allows: pInstance = pParticle;
TEMPLATE_SPECIALIZATION
template <typename other_type>
IC _intrusive_ptr& _intrusive_ptr::operator=(intrusive_ptr<other_type, base_type> const& rhs)
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
IC bool _intrusive_ptr::operator!() const
{
	return (!m_object);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::swap(self_type& rhs)
{
	std::swap(m_object, rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC bool _intrusive_ptr::equal(const self_type& rhs) const
{
	return (m_object == rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::set(object_type* rhs)
{
	if (rhs)
		rhs->m_ref_count.fetch_add(1, std::memory_order_relaxed);
	dec();
	m_object = rhs;
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::set(self_type const& rhs)
{
	set(rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC const typename _intrusive_ptr::object_type* _intrusive_ptr::get() const
{
	return (m_object);
}

TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, _intrusive_ptr const& b)
{
	return (a.get() == b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator !=(_intrusive_ptr const& a, _intrusive_ptr const& b)
{
	return (a.get() != b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator<(_intrusive_ptr const& a, _intrusive_ptr const& b)
{
	return (a.get() < b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator>(_intrusive_ptr const& a, _intrusive_ptr const& b)
{
	return (a.get() > b.get());
}

TEMPLATE_SPECIALIZATION
IC void swap(_intrusive_ptr& lhs, _intrusive_ptr& rhs)
{
	lhs.swap(rhs);
}

#undef TEMPLATE_SPECIALIZATION
#undef _intrusive_ptr


#pragma pack(pop)

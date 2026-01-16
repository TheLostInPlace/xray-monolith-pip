#pragma once

#include <mutex>
#include <shared_mutex>
#include <memory>
#include "_noncopyable.h"
#if 0//def DEBUG
# define PROFILE_CRITICAL_SECTIONS
#endif // DEBUG

#ifdef PROFILE_CRITICAL_SECTIONS
typedef void(*add_profile_portion_callback) (LPCSTR id, const u64& time);
void XRCORE_API set_add_profile_portion(add_profile_portion_callback callback);

# define STRINGIZER_HELPER(a) #a
# define STRINGIZER(a) STRINGIZER_HELPER(a)
# define CONCATENIZE_HELPER(a,b) a##b
# define CONCATENIZE(a,b) CONCATENIZE_HELPER(a,b)
# define MUTEX_PROFILE_PREFIX_ID #mutexes/
# define MUTEX_PROFILE_ID(a) STRINGIZER(CONCATENIZE(MUTEX_PROFILE_PREFIX_ID,a))
#endif // PROFILE_CRITICAL_SECTIONS

// Desc: Simple wrapper for critical section
class XRCORE_API xrCriticalSection : xray::noncopyable
{
public:
	class XRCORE_API raii
	{
	public:
		raii(xrCriticalSection*);
		~raii();

	private:
		xrCriticalSection* critical_section;
	};

private:
	void* pmutex;
#ifdef PROFILE_CRITICAL_SECTIONS
    LPCSTR m_id;
#endif // PROFILE_CRITICAL_SECTIONS

public:
#ifdef PROFILE_CRITICAL_SECTIONS
    xrCriticalSection(LPCSTR id);
#else // PROFILE_CRITICAL_SECTIONS
	xrCriticalSection();
#endif // PROFILE_CRITICAL_SECTIONS
	~xrCriticalSection();

	void Enter();
	void Leave();
	BOOL TryEnter();

	bool IsValid() { return pmutex != nullptr; }
};

class xrCriticalSectionGuard : xray::noncopyable
{
private:
	xrCriticalSection* critical_section;

public:
	void Enter()
	{
		critical_section->Enter();
	}
	void Leave()
	{
		critical_section->Leave();
	}
	xrCriticalSectionGuard(xrCriticalSection* cs) : critical_section(cs) { Enter(); }
	xrCriticalSectionGuard(xrCriticalSection& cs) : critical_section(&cs) { Enter(); }

	~xrCriticalSectionGuard() { Leave(); }
};

// Shared critical section wrapper - manages lifetime of critical section object using shared pointers
// Critical section will be alive even if owning object is destroyed while there are still guards referencing it
typedef std::shared_ptr<xrCriticalSection> xr_shared_ptr_cs;
#define xr_make_shared_cs std::make_shared<xrCriticalSection>
class XRCORE_API xrSharedCriticalSection : xray::noncopyable
{
public:
	// Create a new shared critical section
	xrSharedCriticalSection();

	// Reference an existing shared critical section
	explicit xrSharedCriticalSection(const xr_shared_ptr_cs& cs);

	// Move constructor
	xrSharedCriticalSection(xrSharedCriticalSection&& other) noexcept;

	// Move assignment
	xrSharedCriticalSection& operator=(xrSharedCriticalSection&& other) noexcept;

	~xrSharedCriticalSection() = default;

	// Get the underlying shared pointer
	const xr_shared_ptr_cs& GetPtr() const { return m_critical_section; }

	// Check if the shared pointer is valid
	bool IsValid() const { return m_critical_section != nullptr; }

	// Reference count (for debugging purposes)
	long UseCount() const { return m_critical_section ? m_critical_section.use_count() : 0; }

	void Enter() { m_critical_section->Enter(); }
	void Leave() { m_critical_section->Leave(); }

private:
	xr_shared_ptr_cs m_critical_section;
};

// Guard class for automatic critical section locking using shared pointer
// Use only with xrSharedCriticalSection or xr_shared_ptr_cs
class XRCORE_API xrSharedCriticalSectionGuard : xray::noncopyable
{
public:
	// Constructor - acquires lock from shared critical section
	explicit xrSharedCriticalSectionGuard(const xrSharedCriticalSection& cs);

	// Constructor - acquires lock from shared pointer
	explicit xrSharedCriticalSectionGuard(const xr_shared_ptr_cs& cs);

	// Disable move construction and move assignment to enforce single-owner guard semantics
	xrSharedCriticalSectionGuard(xrSharedCriticalSectionGuard&&) = delete;
	xrSharedCriticalSectionGuard& operator=(xrSharedCriticalSectionGuard&&) = delete;

	// Destructor - automatically releases lock
	~xrSharedCriticalSectionGuard();

	// Manually release lock (will not release on destruction if already released)
	void Leave();

	// Check if lock is currently held
	bool IsLocked() const { return m_critical_section != nullptr; }

private:
	xr_shared_ptr_cs m_critical_section;
	bool m_owns_lock;

	xrSharedCriticalSectionGuard(const xrSharedCriticalSectionGuard&) = delete;
	xrSharedCriticalSectionGuard& operator=(const xrSharedCriticalSectionGuard&) = delete;
};

using ThreadID = HANDLE;


class XRCORE_API xrSRWLock
{
private:
    SRWLOCK smutex;

public:
    xrSRWLock();
    ~xrSRWLock() {};

    void AcquireExclusive();
    void ReleaseExclusive();

    void AcquireShared();
    void ReleaseShared();

    BOOL TryAcquireExclusive();
    BOOL TryAcquireShared();
};
//Write functions guard: lock.AcquireExclusive(); ... lock.ReleaseExclusive();
//Read functions guard: lock.AcquireShared(); ... lock.ReleaseShared();

class XRCORE_API xrSRWLockGuard
{
public:
    xrSRWLockGuard(xrSRWLock& lock, bool shared = false);
    xrSRWLockGuard(xrSRWLock* lock, bool shared = false);
    ~xrSRWLockGuard();

private:
    xrSRWLock* lock;
    bool shared;
};
//Write functions guard: xrSRWLockGuard guard(lock); ...
//Read functions guard: xrSRWLockGuard guard(lock, true); ...

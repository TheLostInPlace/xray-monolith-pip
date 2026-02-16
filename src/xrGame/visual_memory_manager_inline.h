////////////////////////////////////////////////////////////////////////////
//	Module 		: visual_memory_manager_inline.h
//	Created 	: 02.10.2001
//  Modified 	: 19.11.2003
//	Author		: Dmitriy Iassenev
//	Description : Visual memory manager inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC xr_shared_ptr<CVisualMemoryManager::VISIBLES> CVisualMemoryManager::objects() const
{
    if (!this || !m_objectsShared)
        return nullptr;
	return m_objectsShared;
}

IC xr_shared_ptr<CVisualMemoryManager::VISIBLES> CVisualMemoryManager::objectsPtr() const
{
	return objects();
}

IC const CVisualMemoryManager::RAW_VISIBLES& CVisualMemoryManager::raw_objects() const
{
	return (m_visible_objects);
}

IC const CVisualMemoryManager::NOT_YET_VISIBLES& CVisualMemoryManager::not_yet_visible_objects() const
{
	return (m_not_yet_visible_objects);
}

IC void CVisualMemoryManager::set_squad_objects(VISIBLES* squad_objects)
{
    if (squad_objects)
    {
        // Use null deleter since owning object can delete this vector
        m_objectsShared = xr_make_shared_with_deleter(squad_objects, [](VISIBLES*) {});
    }
    else
    {
        m_objectsShared.reset();
        m_not_yet_visible_objects.clear();
    }
}

IC void CVisualMemoryManager::set_squad_objects(xr_shared_ptr<VISIBLES> squad_objects)
{
    if (squad_objects)
    {
        m_objectsShared = squad_objects;
    }
    else
    {
        m_objectsShared.reset();
        m_not_yet_visible_objects.clear();
    }
}

IC float CVisualMemoryManager::visibility_threshold() const
{
	return (current_state().m_visibility_threshold);
}

IC float CVisualMemoryManager::transparency_threshold() const
{
	return (current_state().m_transparency_threshold);
}

IC bool CVisualMemoryManager::enabled() const
{
	return (m_enabled);
}

IC void CVisualMemoryManager::enable(bool value)
{
	m_enabled = value;
}

#pragma once
#include "stdafx.h"
#ifdef CAR_NEW
#include "Car.h"

class CCrew
{
private:
    CCar* m_car;
    shared_str m_sec;

    CGameObject* m_owner;
    CActor* m_actor;

public:
    CCrew(CCar* car, LPCSTR section);
    ~CCrew();

    u16 m_crew_bone;
    u16 m_camera_bone_def;
    u16 m_camera_bone_aim;
    float m_zoom_factor_def;
    float m_zoom_factor_aim;
    
    xr_vector<u16> m_door_bone;

    shared_str m_anim_idle;
    shared_str m_anim_legs;
    shared_str m_anim_ls;
    shared_str m_anim_rs;

    LPCSTR Section() { return m_sec.c_str(); }
    CGameObject* Owner() { return m_owner; }
    CActor* OwnerActor() { return m_actor; }

    bool IsDoor(u16 bid);
    bool Attach(CGameObject* obj);
    void Detach();

    void UpdateXform();
    void PlayAnimationIdle();
    void PlayAnimationSteerLS();
    void PlayAnimationSteerRS();
};
#endif
#include "stdafx.h"
#ifdef CAR_NEW
#include "Car.h"
#include "CarCrew.h"

#include "Level.h"
#include "../Include/xrRender/Kinematics.h"

#include "CharacterPhysicsSupport.h"
#include "detail_path_manager.h"
#include "stalker_animation_manager.h"
#include "stalker_movement_manager_smart_cover.h"
#include "ai/stalker/ai_stalker.h"

CCrew::CCrew(CCar* car, LPCSTR section)
{
    m_car = car;
    m_sec._set(section);

    m_owner = nullptr;
    m_actor = nullptr;

    IKinematics* K = m_car->Visual()->dcast_PKinematics();
    CInifile* ini = K->LL_UserData();

    m_crew_bone = ini->line_exist(section, "crew_bone") ? K->LL_BoneID(ini->r_string(section, "crew_bone")) : BI_NONE;
    m_camera_bone_def = ini->line_exist(section, "camera_bone_def") ? K->LL_BoneID(ini->r_string(section, "camera_bone_def")) : BI_NONE;
    m_camera_bone_aim = ini->line_exist(section, "camera_bone_aim") ? K->LL_BoneID(ini->r_string(section, "camera_bone_aim")) : BI_NONE;
    m_zoom_factor_def = READ_IF_EXISTS(ini, r_float, section, "zoom_factor_def", 1.0F);
    m_zoom_factor_aim = READ_IF_EXISTS(ini, r_float, section, "zoom_factor_aim", 1.0F);

    if (ini->line_exist(section,"door_bone"))
    {
        LPCSTR str = ini->r_string(section, "door_bone");
        string128 sec;
        int n = _GetItemCount(str);
        for (int i = 0; i < n; ++i)
        {
            u16 bid = K->LL_BoneID(_GetItem(str, i, sec));
            if (bid < BI_NONE)
            {
                m_door_bone.emplace_back(bid);
            }
        }
    }

    m_anim_idle = READ_IF_EXISTS(ini, r_string, section, "anim_idle", "steering_torso_idle");
    m_anim_legs = READ_IF_EXISTS(ini, r_string, section, "anim_legs", "steering_legs_idle");
    m_anim_ls = READ_IF_EXISTS(ini, r_string, section, "anim_ls", "steering_torso_idle");
    m_anim_rs = READ_IF_EXISTS(ini, r_string, section, "anim_rs", "steering_torso_idle");
}

CCrew::~CCrew()
{

}

bool CCrew::IsDoor(u16 bid)
{
    if (m_door_bone.size())
    {
        for (auto& I : m_door_bone)
        {
            if (I == bid)
            {
                return true;
            }
        }
    }
    return false;
}

bool CCrew::Attach(CGameObject* obj)
{
    if (Owner())
        return false;
    m_owner = obj;
    m_actor = smart_cast<CActor*>(obj);
    Msg("%s:%d %s", __FUNCTION__, __LINE__, Owner()->cNameSect_str());
    return true;
}

void CCrew::Detach()
{
    if (Owner() == nullptr)
        return;
    Msg("%s:%d %s", __FUNCTION__, __LINE__, Owner()->cNameSect_str());
    m_owner = nullptr;
    m_actor = nullptr;
}

void CCrew::UpdateXform()
{
    if (Owner() == nullptr)
        return;

    /* Update owner position. */
    if (m_crew_bone < BI_NONE)
    {
        Owner()->XFORM().mul_43(m_car->XFORM(), m_car->Visual()->dcast_PKinematics()->LL_GetTransform(m_crew_bone));
    }
    else
    {
        Owner()->XFORM().set(m_car->XFORM());
    }
}

void CCrew::PlayAnimationIdle()
{
    IKinematicsAnimated* A = Owner()->Visual()->dcast_PKinematicsAnimated();
    VERIFY3(A, cName().c_str(), Owner()->cName().c_str());

    if (Owner()->cast_physics_shell_holder()->character_physics_support())
    {
        CPHMovementControl* mvm = Owner()->cast_physics_shell_holder()->character_physics_support()->movement();
        mvm->SetPosition(Owner()->Position());
        mvm->SetVelocity(Fvector().set(0.0F, 0.0F, 0.0F));
    }

    if (OwnerActor())
    {
        if (m_anim_idle.size())
        {
            MotionID mid_body = A->ID_Cycle(m_anim_idle.c_str());
            if (mid_body.idx != OwnerActor()->m_current_torso.idx)
            {
                A->PlayCycle(mid_body);
                OwnerActor()->m_current_torso = mid_body;
                OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }

        if (m_anim_legs.size())
        {
            MotionID mid_legs = A->ID_Cycle(m_anim_legs.c_str());
            if (mid_legs.idx != OwnerActor()->m_current_legs.idx)
            {
                A->PlayCycle(mid_legs);
                OwnerActor()->m_current_legs = mid_legs;
                OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
    }

    CAI_Stalker* stalker = Owner()->cast_stalker();
    if (stalker)
    {
        if (stalker->animation_movement_controlled())
        {
            stalker->destroy_anim_mov_ctrl();
        }

        if (m_anim_idle.size())
        {
            MotionID mid_body = A->ID_Cycle(m_anim_idle.c_str());
            if (mid_body.idx != stalker->animation().torso().animation().idx)
            {
                A->PlayCycle(mid_body);
                stalker->animation().torso().animation(mid_body);
                stalker->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
        if (m_anim_legs.size())
        {
            MotionID mid_legs = A->ID_Cycle(m_anim_legs.c_str());
            if (mid_legs.idx != stalker->animation().legs().animation().idx)
            {
                A->PlayCycle(mid_legs);
                stalker->animation().legs().animation(mid_legs);
                stalker->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
        stalker->movement().set_desired_direction(0);
    }
}

void CCrew::PlayAnimationSteerLS()
{
    IKinematicsAnimated* A = Owner()->Visual()->dcast_PKinematicsAnimated();
    VERIFY3(A, cName().c_str(), Owner()->cName().c_str());

    if (Owner()->cast_physics_shell_holder()->character_physics_support())
    {
        CPHMovementControl* mvm = Owner()->cast_physics_shell_holder()->character_physics_support()->movement();
        mvm->SetPosition(Owner()->Position());
        mvm->SetVelocity(Fvector().set(0.0F, 0.0F, 0.0F));
    }

    if (OwnerActor())
    {
        if (m_anim_ls.size())
        {
            MotionID mid_body = A->ID_Cycle(m_anim_ls.c_str());
            if (mid_body.idx != OwnerActor()->m_current_torso.idx)
            {
                A->PlayCycle(mid_body);
                OwnerActor()->m_current_torso = mid_body;
                OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }

        if (m_anim_legs.size())
        {
            MotionID mid_legs = A->ID_Cycle(m_anim_legs.c_str());
            if (mid_legs.idx != OwnerActor()->m_current_legs.idx)
            {
                A->PlayCycle(mid_legs);
                OwnerActor()->m_current_legs = mid_legs;
                OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
    }

    CAI_Stalker* stalker = Owner()->cast_stalker();
    if (stalker)
    {
        if (stalker->animation_movement_controlled())
        {
            stalker->destroy_anim_mov_ctrl();
        }

        if (m_anim_ls.size())
        {
            MotionID mid_body = A->ID_Cycle(m_anim_ls.c_str());
            if (mid_body.idx != stalker->animation().torso().animation().idx)
            {
                A->PlayCycle(mid_body);
                stalker->animation().torso().animation(mid_body);
                stalker->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
        if (m_anim_legs.size())
        {
            MotionID mid_legs = A->ID_Cycle(m_anim_legs.c_str());
            if (mid_legs.idx != stalker->animation().legs().animation().idx)
            {
                A->PlayCycle(mid_legs);
                stalker->animation().legs().animation(mid_legs);
                stalker->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
        stalker->movement().set_desired_direction(0);
    }
}

void CCrew::PlayAnimationSteerRS()
{
    IKinematicsAnimated* A = Owner()->Visual()->dcast_PKinematicsAnimated();
    VERIFY3(A, cName().c_str(), Owner()->cName().c_str());

    if (Owner()->cast_physics_shell_holder()->character_physics_support())
    {
        CPHMovementControl* mvm = Owner()->cast_physics_shell_holder()->character_physics_support()->movement();
        mvm->SetPosition(Owner()->Position());
        mvm->SetVelocity(Fvector().set(0.0F, 0.0F, 0.0F));
    }

    if (OwnerActor())
    {
        if (m_anim_rs.size())
        {
            MotionID mid_body = A->ID_Cycle(m_anim_rs.c_str());
            if (mid_body.idx != OwnerActor()->m_current_torso.idx)
            {
                A->PlayCycle(mid_body);
                OwnerActor()->m_current_torso = mid_body;
                OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }

        if (m_anim_legs.size())
        {
            MotionID mid_legs = A->ID_Cycle(m_anim_legs.c_str());
            if (mid_legs.idx != OwnerActor()->m_current_legs.idx)
            {
                A->PlayCycle(mid_legs);
                OwnerActor()->m_current_legs = mid_legs;
                OwnerActor()->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
    }

    CAI_Stalker* stalker = Owner()->cast_stalker();
    if (stalker)
    {
        if (stalker->animation_movement_controlled())
        {
            stalker->destroy_anim_mov_ctrl();
        }

        if (m_anim_rs.size())
        {
            MotionID mid_body = A->ID_Cycle(m_anim_rs.c_str());
            if (mid_body.idx != stalker->animation().torso().animation().idx)
            {
                A->PlayCycle(mid_body);
                stalker->animation().torso().animation(mid_body);
                stalker->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
        if (m_anim_legs.size())
        {
            MotionID mid_legs = A->ID_Cycle(m_anim_legs.c_str());
            if (mid_legs.idx != stalker->animation().legs().animation().idx)
            {
                A->PlayCycle(mid_legs);
                stalker->animation().legs().animation(mid_legs);
                stalker->CStepManager::on_animation_start(MotionID(), nullptr);
            }
        }
        stalker->movement().set_desired_direction(0);
    }
}

#endif
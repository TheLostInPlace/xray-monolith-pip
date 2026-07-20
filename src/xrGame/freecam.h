#pragma once

// script-driven free camera, all exports no-op with no actor
// set feeds pos+dir, fov/roll override the live effector only, active queries it
void freecam_set(float px, float py, float pz, float dx, float dy, float dz);
void freecam_release();
bool freecam_active();
void freecam_set_fov(float deg);
void freecam_set_roll(float deg);

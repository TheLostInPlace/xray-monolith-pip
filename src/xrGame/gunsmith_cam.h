#pragma once

// script-driven free camera for the 3DB gunsmith editor
// set feeds view pos+dir from lua, release drops the override; both no-op with no actor
void gunsmith_cam_set(float px, float py, float pz, float dx, float dy, float dz);
void gunsmith_cam_release();

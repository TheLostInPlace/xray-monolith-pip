#pragma once

// script-driven free camera
// set feeds view pos+dir from lua, release drops the override; both no-op with no actor
void freecam_set(float px, float py, float pz, float dx, float dy, float dz);
void freecam_release();

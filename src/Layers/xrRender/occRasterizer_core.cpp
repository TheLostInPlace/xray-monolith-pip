#include "stdafx.h"
#include "occRasterizer.h"

const int BOTTOM = 0, TOP = 1;

void occRasterizer::i_order(const DirectX::XMFLOAT3& V0, const DirectX::XMFLOAT3& V1, const DirectX::XMFLOAT3& V2)
{
    using namespace DirectX;

    // 1. Create a local array of pointers to the vertices
    const XMFLOAT3* sorted[3] = { &V0, &V1, &V2 };

    // 2. Sort by Y-coordinate (index 1)
    // Small fixed-size sorts are often inlined and branch-optimized by the compiler
    if (sorted[0]->y > sorted[1]->y) std::swap(sorted[0], sorted[1]);
    if (sorted[0]->y > sorted[2]->y) std::swap(sorted[0], sorted[2]);
    if (sorted[1]->y > sorted[2]->y) std::swap(sorted[1], sorted[2]);

    const XMFLOAT3& min = *sorted[0];
    const XMFLOAT3& mid = *sorted[1];
    const XMFLOAT3& max = *sorted[2];

    // 3. Update current state
    // Note: The +2 offset is likely for sub-pixel centering or guarding against 
    // negative screen coordinates in the legacy X-Ray rasterizer.

    // Using XMVECTOR here allows us to perform the additions in parallel
    XMVECTOR offset = XMVectorSet(2.0f, 2.0f, 0.0f, 0.0f);

    // Now it's just a direct register-to-register operation
    currentV[0] = XMVectorAdd(XMLoadFloat3(sorted[0]), offset);
    currentV[1] = XMVectorAdd(XMLoadFloat3(sorted[1]), offset);
    currentV[2] = XMVectorAdd(XMLoadFloat3(sorted[2]), offset);
}

// Find the closest min/max pixels of a point
void occRasterizer::Vclamp(int& v, int a, int b)
{
	if (v < a) v = a;
	else if (v >= b) v = b - 1;
}

BOOL occRasterizer::shared(occTri* T1, occTri* T2)
{
	if (T1 == T2) return TRUE;
	if (T1->adjacent[0] == T2) return TRUE;
	if (T1->adjacent[1] == T2) return TRUE;
	if (T1->adjacent[2] == T2) return TRUE;
	return FALSE;
}

const float one_div_3 = 1.f / 3.f;

// Rasterize a scan line between given X point values, corresponding Z values and current color
// Rasterize a scan line with SIMD Z-buffering
void occRasterizer::i_scan(int curY, float leftX, float lhx, float rightX, float rhx, float startZ, float endZ)
{
    using namespace DirectX;

    // --- 1. Span Calculation (Scalar - fast enough) ---
    float start_c = leftX + lhx;
    float end_c = rightX + rhx;
    float startR = leftX - lhx;
    float endR = rightX - rhx;

    float startT = startR, endT = end_c;
    float startX = start_c, endX = endR;

    if (start_c < startR) { startT = start_c; startX = startR; }
    if (end_c < endR) { endT = endR;      endX = end_c; }

    // Clipping
    int minT = iFloor(startT) - 1;
    int maxT = iCeil(endT) + 1;
    Vclamp(minT, 1, occ_dim - 1);
    Vclamp(maxT, 1, occ_dim - 1);
    if (minT >= maxT) return;

    int minX = iCeil(startX);
    int maxX = iFloor(endX);
    Vclamp(minX, 0, occ_dim);
    Vclamp(maxX, 0, occ_dim);

    int limLeft, limRight;
    if (minX > maxX) { limLeft = maxX; limRight = minX; }
    else { limLeft = minX; limRight = maxX; }

    // --- 2. Z Interpolation Setup ---
    float lenR = endR - startR;
    float Zlen = endZ - startZ;
    // Protect against divide-by-zero for 1-pixel wide triangles
    float invLenR = (_abs(lenR) > EPS_S) ? (1.0f / lenR) : 0.0f;

    float Z = startZ + (minT - startR) * invLenR * Zlen;
    float Zend = startZ + (maxT - startR) * invLenR * Zlen;
    float dZ = (Zend - Z) / float(maxT - minT);

    // Bias Z to center of pixel to prevent self-clipping
    Z += 0.5f * _abs(dZ);

    // Access Buffers
    occTri** pFrame = Raster.get_frame();
    float* pDepth = Raster.get_depth();
    int i_base = curY * occ_dim;

    // --- 3. Left Connector (Scalar) ---
    // Handles edge stitching logic
    int i = i_base + minT;
    int limit = i_base + limLeft;

    for (; i < limit; i++, Z += dZ)
    {
        if (shared(currentTri, pFrame[i - 1]))
        {
            if (Z < pDepth[i])
            {
                pFrame[i] = currentTri;
                // Use std::max or a macro for the blend
                pDepth[i] = (Z > pDepth[i - 1]) ? Z : pDepth[i - 1];
                dwPixels++;
            }
        }
    }

    // --- 4. Main Scanline (SIMD Optimized) ---
    // This loops covers the vast majority of pixels
    limit = i_base + maxX;

    // Check if we have enough pixels to justify SIMD (at least 4)
    if (limit - i >= 4)
    {
        // Pre-calculate Z increments: [0, dZ, 2dZ, 3dZ]
        XMVECTOR vZStep = XMVectorSet(0.0f, dZ, dZ * 2.0f, dZ * 3.0f);
        XMVECTOR vZQuad = XMVectorAdd(XMVectorReplicate(Z), vZStep);
        XMVECTOR vDelta = XMVectorReplicate(dZ * 4.0f);

        // Align loop to 4 pixels
        int simd_limit = limit - 3;
        for (; i < simd_limit; i += 4)
        {
            // Load 4 existing depth values from buffer
            XMVECTOR vDestDepth = _mm_loadu_ps(&pDepth[i]);

            // Compare: vMask = (vZQuad < vDestDepth)
            XMVECTOR vMask = XMVectorLess(vZQuad, vDestDepth);

            // Move comparison result to an integer mask (4 bits)
            int mask = _mm_movemask_ps(vMask);

            // If mask is 0, all new Z values are behind existing geometry -> Skip
            if (mask)
            {
                // Update Z-Buffer: Store Min(NewZ, OldZ)
                // This is branchless and safe because we only want to write smaller values
                XMVECTOR vNewDepth = XMVectorMin(vZQuad, vDestDepth);
                _mm_storeu_ps(&pDepth[i], vNewDepth);

                // Update Pointer Buffer (Scalar fallback for pointers)
                // Since pointers are 64-bit, SIMD scatter is complex. 
                // Unrolled scalar writes are fastest here.
                if (mask & 1) { pFrame[i + 0] = currentTri; dwPixels++; }
                if (mask & 2) { pFrame[i + 1] = currentTri; dwPixels++; }
                if (mask & 4) { pFrame[i + 2] = currentTri; dwPixels++; }
                if (mask & 8) { pFrame[i + 3] = currentTri; dwPixels++; }
            }

            // Advance Z to the next block of 4
            vZQuad = XMVectorAdd(vZQuad, vDelta);
        }

        // Recover scalar Z for the remaining pixels
        // We extracted Z from the vector to ensure continuity
        Z = XMVectorGetX(vZQuad);
    }

    // Handle remaining pixels (0 to 3) using scalar loop
    for (; i < limit; i++, Z += dZ)
    {
        if (Z < pDepth[i])
        {
            pFrame[i] = currentTri;
            pDepth[i] = Z;
            dwPixels++;
        }
    }

    // --- 5. Right Connector (Scalar) ---
    // Handles the right edge stitching
    i = i_base + maxT - 1;
    limit = i_base + limRight;

    // Recalculate Z for the right side to avoid accumulated error
    // (Optional: standard floating point drift might be negligible)
    float Z_Right = Zend - dZ;

    for (; i >= limit; i--, Z_Right -= dZ)
    {
        if (shared(currentTri, pFrame[i + 1]))
        {
            if (Z_Right < pDepth[i])
            {
                pFrame[i] = currentTri;
                pDepth[i] = (Z_Right > pDepth[i + 1]) ? Z_Right : pDepth[i + 1];
                dwPixels++;
            }
        }
    }
}

/* 
Rasterises 1 section of the triangle a 'section' of a triangle is the portion of a triangle between 
2 horizontal scan lines corresponding to 2 vertices of a triangle
p2.y >= p1.y, p1, p2 are start/end vertices
E1 E2 are the triangle edge differences of the 2 bounding edges for this section
*/

void occRasterizer::i_section(int Sect, BOOL bMiddle)
{
    using namespace DirectX;
    int startY, endY;
    XMVECTOR vStartP1, vStartP2;
    XMVECTOR E1, E2;

    // 1. Calculate Bounds and Edges using SIMD
    if (Sect == BOTTOM)
    {
        // currentV[0]=A, [1]=B, [2]=C
        startY = iCeil(XMVectorGetY(currentV[0]));
        endY = iFloor(XMVectorGetY(currentV[1])) - 1;
        vStartP1 = vStartP2 = currentV[0];
        if (bMiddle) endY++;

        int test = iFloor(XMVectorGetY(currentV[2]));
        if (endY >= test) endY--;

        // E1 = B - A, E2 = C - A
        E1 = XMVectorSubtract(currentV[1], currentV[0]);
        E2 = XMVectorSubtract(currentV[2], currentV[0]);
    }
    else
    {
        startY = iCeil(XMVectorGetY(currentV[1]));
        endY = iFloor(XMVectorGetY(currentV[2]));
        vStartP1 = currentV[0];
        vStartP2 = currentV[1];
        if (bMiddle) startY--;

        int test = iCeil(XMVectorGetY(currentV[0]));
        if (startY < test) startY++;

        // E1 = C - A, E2 = C - B
        E1 = XMVectorSubtract(currentV[2], currentV[0]);
        E2 = XMVectorSubtract(currentV[2], currentV[1]);
    }

    Vclamp(startY, 0, occ_dim);
    Vclamp(endY, 0, occ_dim);
    if (startY >= endY) return;

    // 2. Compute Inverse Slopes (X/Y and Z/Y)
    // We can calculate all slopes for an edge in one go
    // vSlopes.x = mEx, vSlopes.z = mEz
    XMVECTOR vInvY1 = XMVectorReciprocal(XMVectorSplatY(E1));
    XMVECTOR vInvY2 = XMVectorReciprocal(XMVectorSplatY(E2));
    XMVECTOR vSlopes1 = XMVectorMultiply(E1, vInvY1);
    XMVECTOR vSlopes2 = XMVectorMultiply(E2, vInvY2);

    // Initial Y offset
    float fStartY = (float)startY;
    float e1_init_dY = fStartY - XMVectorGetY(vStartP1);
    float e2_init_dY = fStartY - XMVectorGetY(vStartP2);

    float left_dX, right_dX, left_dZ, right_dZ;
    XMVECTOR vLeft, vRight;

    // 3. Determine Edge Orientation (Left vs Right)
    float mE1 = XMVectorGetX(vSlopes1);
    float mE2 = XMVectorGetX(vSlopes2);

    if (((mE1 < mE2) && (Sect == BOTTOM)) || ((mE1 > mE2) && (Sect == TOP)))
    {
        // E1 is Left, E2 is Right
        vLeft = XMVectorMultiplyAdd(vSlopes1, XMVectorReplicate(e1_init_dY), vStartP1);
        left_dX = mE1;
        left_dZ = XMVectorGetZ(vSlopes1);

        vRight = XMVectorMultiplyAdd(vSlopes2, XMVectorReplicate(e2_init_dY), vStartP2);
        right_dX = mE2;
        right_dZ = XMVectorGetZ(vSlopes2);
    }
    else
    {
        // E2 is Left, E1 is Right
        vLeft = XMVectorMultiplyAdd(vSlopes2, XMVectorReplicate(e2_init_dY), vStartP2);
        left_dX = mE2;
        left_dZ = XMVectorGetZ(vSlopes2);

        vRight = XMVectorMultiplyAdd(vSlopes1, XMVectorReplicate(e1_init_dY), vStartP1);
        right_dX = mE1;
        right_dZ = XMVectorGetZ(vSlopes1);
    }

    // 4. Scanline Loop
    float leftX = XMVectorGetX(vLeft) + (left_dX * 0.5f);
    float rightX = XMVectorGetX(vRight) + (right_dX * 0.5f);
    float leftZ = XMVectorGetZ(vLeft);
    float rightZ = XMVectorGetZ(vRight);

    for (; startY <= endY; startY++)
    {
        // Pass to the horizontal span filler
        i_scan(startY, leftX, left_dX * 0.5f, rightX, right_dX * 0.5f, leftZ, rightZ);

        leftX += left_dX;
        rightX += right_dX;
        leftZ += left_dZ;
        rightZ += right_dZ;
    }
}

void occRasterizer::i_section_b0()
{
    i_section(BOTTOM, 0);
}

void occRasterizer::i_section_b1()
{
    i_section(BOTTOM, 1);
}

void occRasterizer::i_section_t0()
{
    i_section(TOP, 0);
}

void occRasterizer::i_section_t1()
{
	i_section(TOP, 1);
}

u32 occRasterizer::rasterize(occTri* T)
{
    using namespace DirectX;

    // Setup triangle state
    currentTri = T;
    dwPixels = 0;

    // 1. Sort vertices by Y (Top to Bottom)
    // i_order now populates currentV[0] (top), currentV[1] (mid), currentV[2] (low)
    i_order(T->raster[0], T->raster[1], T->raster[2]);

    // 2. Identify the middle-vertex split
    // currentV[1] is our "Middle" vertex (formerly currentB)
    float mid_y = XMVectorGetY(currentV[1]);

    // Quick floor using SSE intrinsics if you want max speed, 
    // or standard cast since we're already on x64
    float mid_y_fraction = mid_y - floorf(mid_y);

    // 3. Dispatch to section rasterizers
    // The sub-pixel bias (0.5f) determines which scanline the middle vertex belongs to
    if (mid_y_fraction > 0.5f)
    {
        i_section_b1(); // Bottom section, variant 1
        i_section_t0(); // Top section, variant 0
    }
    else
    {
        i_section_b0(); // Bottom section, variant 0
        i_section_t1(); // Top section, variant 1
    }

    return dwPixels;
}

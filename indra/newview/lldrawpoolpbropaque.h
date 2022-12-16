/** 
 * @file lldrawpoolpbropaque.h
 * @brief LLDrawPoolPBrOpaque class definition
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLDRAWPOOLPBROPAQUE_H
#define LL_LLDRAWPOOLPBROPAQUE_H

#include "lldrawpool.h"

class LLDrawPoolGLTFPBR final : public LLRenderPass
{
public:
    enum
    {
        // See: DEFERRED_VB_MASK
        VERTEX_DATA_MASK = 0
                         | LLVertexBuffer::MAP_VERTEX
                         | LLVertexBuffer::MAP_NORMAL
                         | LLVertexBuffer::MAP_TEXCOORD0 // Diffuse
                         | LLVertexBuffer::MAP_TEXCOORD1 // Normal
                         | LLVertexBuffer::MAP_TEXCOORD2 // Spec <-- ORM Occlusion Roughness Metal
                         | LLVertexBuffer::MAP_TANGENT
                         | LLVertexBuffer::MAP_COLOR
    };
    U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

    LLDrawPoolGLTFPBR();

    S32 getNumDeferredPasses() override { return 1; }
    void renderDeferred(S32 pass) override;
};

#endif // LL_LLDRAWPOOLPBROPAQUE_H

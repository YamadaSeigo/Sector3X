/*****************************************************************//**
 * @file   Sector11FW.h
 * @brief  外部プロジェクトが読み込む用のヘッダーファイル
 * @author seigo_t03b63m
 * @date   May 2025
 *********************************************************************/

#pragma once

#ifndef A_SECTOR11FW_H_
#define A_SECTOR11FW_H_

#include "Math/sx_math.h"
#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"
#include "Math/aabb_util.h"

#include "Graphics/IGraphicsDevice.hpp"
#include "Core/GameEngine.h"
#include "Core/ECS/Query.h"

#include "Core/Grid2DPartition.h"
#include "Core/Grid3DPartition.h"
#include "Core/QuadTreePartition.h"
#include "Core/OctreePartition.h"
#include "Core/VoidPartition.h"

#include "Core/TimerService.h"

#include "Core/build_order.h"

#include "Graphics/TerrainClustered.h"

#include "Physics/PhysicsService.h"

#include "Audio/AudioService.h"

#include "SIMD/simd_api.h"
#include "SIMD/simd_detect.h"

#endif // A_SECTOR11FW_H_

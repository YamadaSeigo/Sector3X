#pragma once

#include <shared_mutex>

#include "../Math/Vector.hpp"
#include "../Math/AABB.hpp"
#include "../Math/Matrix.hpp"
#include "../Math/Frustum.hpp"
#include "../Math/sx_math.h"
#include "../Math/convert.hpp"

#include "../Core/ECS/ServiceContext.hpp"

namespace SFW
{
	namespace Graphics
	{

        // カスケード数はお好みで
        constexpr std::uint32_t kMaxShadowCascades = 4;

        // -------------------------------------------------------------
        // ライト定義
        // -------------------------------------------------------------
        struct DirectionalLight
        {
            Math::Vec3f directionWS = Math::Vec3f(cos(Math::Deg2Rad(10.0f)), -sin(Math::Deg2Rad(10.0f)), 0.0f); // ワールド空間
            Math::Vec3f color = Math::Vec3f(1.0f, 1.0f, 1.0f);
            float intensity = 1.0f;
            bool  castsShadow = true;
        };

        struct AmbientLight
        {
            Math::Vec3f color = Math::Vec3f(0.1f, 0.1f, 0.1f);
            float intensity = 1.0f;
        };

        // -------------------------------------------------------------
        // カメラ情報（レンダリング側から渡す）
        // -------------------------------------------------------------
        struct CameraParams
        {
            Math::Matrix4x4f view = {};      // カメラの View 行列
            Math::Vec3f position = {};  // カメラ位置（World）
            float nearPlane = 0.1f;
            float farPlane = 1000.0f;
            float fovY = Math::Deg2Rad(60.0f);   // ラジアン
            float aspect = 16.0f / 9.0f;
        };

        // -------------------------------------------------------------
        // カスケード1つ分の情報
        // -------------------------------------------------------------
        template<uint32_t N>
        struct ShadowCascade
        {
            std::array<float, N> splitNear = { 0.0f };  // カメラ視点の距離
            std::array<float, N> splitFar = { 0.0f };

            std::array<Math::Matrix4x4f, N> lightView;     // ライトの View 行列
            std::array<Math::Matrix4x4f, N> lightProj;     // ライトの Ortho 行列
            std::array<Math::Matrix4x4f, N> lightViewProj; // lightProj * lightView（行列の並びはエンジンに合わせて）

            std::array<Math::Frustumf, N> frustumWS;   // カスケードのワールド空間フラスタム（CPU カリング用）

            // 任意: カスケード領域の AABB / 球 など
            std::array<Math::AABB3f, N> boundsWS;
        };

        class LightShadowService
        {
        public:
            struct CascadeConfig
            {
				Math::Vec2f shadowMapResolution = Math::Vec2f(1920.0f, 1080.0f); // シャドウマップ解像度
                std::uint32_t   cascadeCount = 4;   // 1〜kMaxShadowCascades
                float           shadowDistance = 200.0f; // カメラからの最大影距離
                float           lambda = 0.5f;   // 0 = 線形, 1 = 対数, その中間
                float           maxWorldExtent = 1000.0f; // ライト正射影の最大サイズ（安全マージン）
				float           casterExtrusion = 500.0f; // 影キャスターをライト方向に押し出す距離
            };

            LightShadowService() = default;

            // ライト設定
            void SetDirectionalLight(const DirectionalLight& d)
            {
				std::unique_lock lock(m_updateMutex);
                m_directional = d;
            }

            void SetAmbientLight(const AmbientLight& a)
            {
                m_ambient = a;
            }

            const DirectionalLight& GetDirectionalLight() const noexcept {
                std::shared_lock lock(m_updateMutex);
                return m_directional;
            }
            const AmbientLight& GetAmbientLight()     const noexcept {
				std::shared_lock lock(m_updateMutex);
                return m_ambient;
            }

            // カスケード設定
            void SetCascadeConfig(const CascadeConfig& cfg)
            {
				std::unique_lock lock(m_updateMutex);

                m_cascadeCfg = cfg;
                m_cascadeCfg.cascadeCount = (std::max<std::uint32_t>)(1, (std::min<std::uint32_t>)(cfg.cascadeCount, kMaxShadowCascades));
            }

            const CascadeConfig& GetCascadeConfig() const noexcept {
				std::shared_lock lock(m_updateMutex);
                return m_cascadeCfg;
            }

            // カスケード情報へのアクセス
            std::uint32_t GetCascadeCount() const noexcept {
				std::shared_lock lock(m_updateMutex);
                return m_cascadeCount;
            }

            const ShadowCascade<kMaxShadowCascades>& GetCascades() const {
				std::shared_lock lock(m_updateMutex);
                return m_cascades;
            }

			const std::array<float, kMaxShadowCascades>& GetSplitDistances() const {
				std::shared_lock lock(m_updateMutex);
				return m_splitDistances;
			}

			uint32_t GetCascadeIndex(float viewDist) const
            {
				std::shared_lock lock(m_updateMutex);
				for (std::uint32_t i = 0; i < m_cascadeCount; ++i)
				{
                    if (viewDist < m_splitDistances[i]) return i;
				}
				return m_cascadeCount - 1;
            }

			std::pair<uint32_t, uint32_t> GetCascadeIndexRangeUnlock(float min, float max) const
            {
				max = (std::max)(max, min);

				uint32_t first = 0;
				uint32_t last = 0;
                uint32_t i = 0;
                for (i; i < m_cascadeCount; ++i)
                {
                    if (min < m_splitDistances[i])
                    {
                        first = i;
                        break;
                    }
                }
				for (i; i < m_cascadeCount; ++i)
				{
					if (max < m_splitDistances[i])
					{
						last = i;
						break;
					}
				}

				return { first, last };
            }

            // ---------------------------------------------------------
           // カスケード更新
           //  - CameraParams とシーン AABB からカスケードを構築
           //  - AABB は「シーン全体」または「このライトが影響する範囲」
           // ---------------------------------------------------------
            void UpdateCascade(const CameraParams& cam, const Math::AABB3f& sceneBounds)
            {
				std::unique_lock lock(m_updateMutex);

                if (!m_directional.castsShadow)
                {
                    m_cascadeCount = 0;
                    return;
                }

                m_cascadeCount = m_cascadeCfg.cascadeCount;
                if (m_cascadeCount == 0)
                    return;

                ComputeCascadeSplits(cam);
                BuildCascades(cam, sceneBounds);
            }
        private:
            // ---------------------------------------------------------
            // 内部ヘルパ
            // ---------------------------------------------------------

            // カメラ視点でのスプリット (near, far は CameraParams から)
            void ComputeCascadeSplits(const CameraParams& cam)
            {
                const float n = cam.nearPlane;
                const float f = (std::min)(cam.farPlane, m_cascadeCfg.shadowDistance);
                const float lambda = m_cascadeCfg.lambda;
                const std::uint32_t N = m_cascadeCount;

                for (std::uint32_t i = 0; i < N; ++i)
                {
                    float si = (i + 1) / static_cast<float>(N); // 0..1

                    // 対数スプリット
                    float logSplit = n * std::pow(f / n, si);
                    // 線形スプリット
                    float linSplit = n + (f - n) * si;
                    // ミックス
                    float split = Math::lerp(linSplit, logSplit, lambda);

                    m_splitDistances[i] = split;
                }
            }

            // カメラ空間の距離をワールド空間の AABB/フラスタムに変換して
            // ライト空間の正射影を合わせる
            void BuildCascades(const CameraParams& cam, const Math::AABB3f& sceneBounds)
            {
                using namespace Math;

                // ライト方向（正規化）
                Vec3f lightDir = NormalizeSafe(m_directional.directionWS, Vec3f(0.0f, -1.0f, 0.0f));

                // カスケードごとのワールド AABB
                std::array<Math::AABB3f, kMaxShadowCascades> cascadeBoundsWS;

                float prevSplit = cam.nearPlane;

                // View の逆行列を使ってワールドへ
                Math::Matrix4x4f camWorldMtx = Inverse(cam.view);

                // キャスター押し出し量
                float maxCasterDist = m_cascadeCfg.casterExtrusion;
                Vec3f pad = Vec3f(
                    std::fabs(lightDir.x),
                    std::fabs(lightDir.y),
                    std::fabs(lightDir.z)
                ) * maxCasterDist;

                // --------------------------------------------------
                // 1) カスケードごとの WS AABB を計算
                // --------------------------------------------------
                for (std::uint32_t i = 0; i < m_cascadeCount; ++i)
                {
                    float splitDist = m_splitDistances[i];

                    m_cascades.splitNear[i] = prevSplit;
                    m_cascades.splitFar[i] = splitDist;

                    // このカスケードのカメラ視錐台スライスを WS AABB に
                    Math::AABB3f cascadeAABBWS =
                        ComputeCascadeSliceAABBWS(cam, camWorldMtx, prevSplit, splitDist);

                    // シーン AABB との交差で少しタイトに
                    Math::AABB3f tightWS = IntersectAABB(cascadeAABBWS, sceneBounds);

                    // 安全のためワールド最大サイズでクランプ
                    tightWS.shrinkExtent(m_cascadeCfg.maxWorldExtent);

                    cascadeBoundsWS[i] = tightWS;

                    prevSplit = splitDist;
                }

                // --------------------------------------------------
                // 2) カスケードごとに独立したライトビュー / オルソを構築
                // --------------------------------------------------
                for (std::uint32_t i = 0; i < m_cascadeCount; ++i)
                {
                    const Math::AABB3f& recvBoundsWS = cascadeBoundsWS[i];

                    // まずは「受け側（レシーバ）の AABB」からライトビューを決める
                    Math::Matrix4x4f lightView;
                    BuildLightView(lightDir, recvBoundsWS, lightView);

                    // --- ライト空間に変換してテクセルスナップ -----------------
                    Math::AABB3f recvLS = lightView * recvBoundsWS;

                    Math::Vec3f centerLS = recvLS.center();
                    Math::Vec3f extentsLS = recvLS.extent(); // XY はライト空間の幅・高さ

                    float shadowSizeX = (float)m_cascadeCfg.shadowMapResolution.x;
                    float shadowSizeY = (float)m_cascadeCfg.shadowMapResolution.y;

                    float texelSizeX = (extentsLS.x * 2.0f) / shadowSizeX;
                    float texelSizeY = (extentsLS.y * 2.0f) / shadowSizeY;

                    Math::Vec3f snappedCenterLS;
                    snappedCenterLS.x =
                        std::floor(centerLS.x / texelSizeX + 0.5f) * texelSizeX;
                    snappedCenterLS.y =
                        std::floor(centerLS.y / texelSizeY + 0.5f) * texelSizeY;
                    snappedCenterLS.z = centerLS.z; // Z はそのまま

                    //スナップオフ
                    snappedCenterLS = centerLS;

                    // XY だけスナップしたライト空間 AABB（受け側）
                    Math::AABB3f recvSnappedLS;
                    recvSnappedLS.lb =
                        snappedCenterLS - Math::Vec3f(extentsLS.x, extentsLS.y, extentsLS.z);
                    recvSnappedLS.ub =
                        snappedCenterLS + Math::Vec3f(extentsLS.x, extentsLS.y, extentsLS.z);

                    // --- キャスター用に Z だけ押し出す -----------------------
                    Math::AABB3f casterWS = recvBoundsWS;
                    casterWS.lb -= pad;
                    casterWS.ub += pad;

                    Math::AABB3f casterTightWS = IntersectAABB(casterWS, sceneBounds);
                    Math::AABB3f casterLS = lightView * casterTightWS;

                    // 最終ライト空間 AABB:
                    //   XY = スナップ済み受け側、Z = キャスターで決まった near/far
                    Math::AABB3f finalLS;
                    finalLS.lb = Math::Vec3f(
                        recvSnappedLS.lb.x,
                        recvSnappedLS.lb.y,
                        casterLS.lb.z
                    );
                    finalLS.ub = Math::Vec3f(
                        recvSnappedLS.ub.x,
                        recvSnappedLS.ub.y,
                        casterLS.ub.z
                    );

                    // そのライト空間 AABB をちょうど覆う Ortho
                    Math::Matrix4x4f lightProj;
                    BuildLightOrtho(finalLS, lightProj);

                    // 記録
                    m_cascades.lightView[i] = lightView;
                    m_cascades.lightProj[i] = lightProj;
                    m_cascades.lightViewProj[i] = lightProj * lightView;

                    // カリング用フラスタム & WS Bounds を保存
                    m_cascades.frustumWS[i] = BuildFrustumFromMatrix(m_cascades.lightViewProj[i]);
                    m_cascades.boundsWS[i] = recvBoundsWS;
                }
            }

            // ---------------------------------------------------------
           // カスケードスライス AABB（ワールド）の計算
           //  - カメラの view/proj/fov/aspect/near/far から
           //  - [sliceNear, sliceFar] の範囲のフラスタム8頂点を計算し、
           //  - それをワールドに戻して AABB を作る
           // ---------------------------------------------------------
            Math::AABB3f ComputeCascadeSliceAABBWS(const CameraParams& cam, const Math::Matrix4x4f& camWorldMtx, float sliceNear, float sliceFar) const
            {
                using namespace Math;

                // カメラの視錐台コーナー（カメラ空間）
                std::array<Vec3f, 8> cornersVS;
                BuildCameraFrustumCornersVS(cam, sliceNear, sliceFar, cornersVS);

                AABB3f result;
                result.invalidate();

                for (const Vec3f& pVS : cornersVS)
                {
                    Vec3f pWS = TransformPoint(camWorldMtx, pVS);
                    result.expandToInclude(pWS); // AABB に点を追加
                }
                return result;
            }

            // カメラ空間のフラスタム8頂点（Near/Far を sliceNear/sliceFar に差し替え）
            template<Math::Handedness Hand = Math::Handedness::LH>
            static void BuildCameraFrustumCornersVS(const CameraParams& cam,
                float sliceNear, float sliceFar,
                std::array<Math::Vec3f, 8>& outCornersVS)
            {
				using namespace Math;

                const float fov = cam.fovY;
                const float aspect = cam.aspect;

                float tanHalfFov = std::tan(fov * 0.5f);

                float nh = sliceNear * tanHalfFov;
                float nw = nh * aspect;
                float fh = sliceFar * tanHalfFov;
                float fw = fh * aspect;

				constexpr float zSign = (Hand == Handedness::LH) ? 1.0f : -1.0f;

                sliceNear *= zSign;
				sliceFar *= zSign;

                // カメラ空間でのフラスタムコーナー
                // Near
                outCornersVS[0] = Vec3f(-nw, nh, sliceNear); // left-top
                outCornersVS[1] = Vec3f(nw, nh, sliceNear); // right-top
                outCornersVS[2] = Vec3f(nw, -nh, sliceNear); // right-bottom
                outCornersVS[3] = Vec3f(-nw, -nh, sliceNear); // left-bottom
                // Far
                outCornersVS[4] = Vec3f(-fw, fh, sliceFar);
                outCornersVS[5] = Vec3f(fw, fh, sliceFar);
                outCornersVS[6] = Vec3f(fw, -fh, sliceFar);
                outCornersVS[7] = Vec3f(-fw, -fh, sliceFar);
            }

            // ---------------- カメラ方向の抽出 ----------------
            // Mat4f がどういうレイアウトかに合わせて書き換えてください。
            // ここでは「行ベクトルが right/up/forward の transposed view」と仮定した仮実装です。
            static Math::Vec3f ExtractCameraForward(const Math::Matrix4x4f& view)
            {
                // view = inverse(世界→カメラ) なので、forward は -Z の列/行など
                // ここでは「第3行の xyz を -forward」と仮定した例：
                Math::Vec3f forward(
                    -view.m[2][0],
                    -view.m[2][1],
                    -view.m[2][2]
                );
                return NormalizeSafe(forward, Math::Vec3f(0, 0, 1));
            }

            static Math::Vec3f ExtractCameraRight(const Math::Matrix4x4f& view)
            {
                Math::Vec3f right(
                    view.m[0][0],
                    view.m[0][1],
                    view.m[0][2]
                );
                return NormalizeSafe(right, Math::Vec3f(1, 0, 0));
            }

            static Math::Vec3f ExtractCameraUp(const Math::Matrix4x4f& view)
            {
                Math::Vec3f up(
                    view.m[1][0],
                    view.m[1][1],
                    view.m[1][2]
                );
                return NormalizeSafe(up, Math::Vec3f(0, 1, 0));
            }

            // ---------------------------------------------------------
            // ライトビュー行列の構築
            //  - ライト方向 + AABB 中心から「逆方向に少し離れた位置」から見る
            // ---------------------------------------------------------
            static void BuildLightView(const Math::Vec3f& lightDirWS, const Math::AABB3f& boundsWS, Math::Matrix4x4f& outView)
            {
                Math::Vec3f center = boundsWS.center(); // AABB.hpp に合わせて

                Math::Vec3f eye = center - lightDirWS;
                Math::Vec3f target = center;
                Math::Vec3f up(0.0f, 1.0f, 0.0f);

				if (lightDirWS.dot(up) > 0.99f)
				{
					up = Math::Vec3f(1.0f, 0.0f, 0.0f); // ライトが真上なら別の Up を使う
				}

                // 左手系 / 右手系に合わせて LookAt 関数を呼んでください
                outView = Math::MakeLookAtMatrixLH(eye, target, up);
            }

            // ---------------------------------------------------------
            // ライト正射影行列の構築
            //  - ライト空間の AABB をちょうど覆う OrthoOffCenter を作る
            // ---------------------------------------------------------
            static void BuildLightOrtho(const Math::AABB3f& lightSpaceAABB, Math::Matrix4x4f& outProj)
            {
                Math::Vec3f min = lightSpaceAABB.lb;
                Math::Vec3f max = lightSpaceAABB.ub;

                // 左手系 / 右手系に合わせて Ortho 関数を呼んでください
                // outProj = Mat4f::CreateOrthoOffCenterLH(min.x, max.x, min.y, max.y, -max.z, -min.z);
                outProj = Math::MakeOrthographicT<Math::Handedness::LH, Math::ClipZRange::ZeroToOne>(min.x, max.x, min.y, max.y, min.z, max.z);
            }

            // ---------------------------------------------------------
            // ViewProj から Frustum を作る
            // ---------------------------------------------------------
            static Math::Frustumf BuildFrustumFromMatrix(const Math::Matrix4x4f& viewProj)
            {
				Math::Frustumf fr = Math::Frustumf::FromRowMajorMatrix(viewProj, Math::ClipZRange::ZeroToOne, true);
                return fr;
            }

        private:
            mutable std::shared_mutex m_updateMutex;

            DirectionalLight m_directional{};
            AmbientLight     m_ambient{};

            CascadeConfig m_cascadeCfg{};
            std::uint32_t m_cascadeCount = 0;

            ShadowCascade<kMaxShadowCascades> m_cascades{};
            std::array<float, kMaxShadowCascades>         m_splitDistances{};
        public:
            STATIC_SERVICE_TAG
        };
	}
}

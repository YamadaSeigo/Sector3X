#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>

// polyline の弧長サンプル結果
struct Sample
{
	Math::Vec3f  pos;     // 中心線上
	Math::Vec3f  tangent; // 進行方向
	float yawDeg;  // 水平面のyaw（必要なら）
};

// 折れ線を弧長パラメータ s (0..totalLen) でサンプル
class PolylineSampler
{
public:
	explicit PolylineSampler(std::vector< Math::Vec3f> points)
		: m_pts(std::move(points))
	{
		Build();
	}

	float TotalLength() const { return m_totalLen; }

	Sample SampleByArcLength(float s) const
	{
		Sample out{};
		if (m_pts.size() < 2 || m_totalLen <= 0.0f) return out;

		s = Math::clamp(s, 0.0f, m_totalLen);

		// 二分探索で区間を特定
		auto it = std::upper_bound(m_prefixLen.begin(), m_prefixLen.end(), s);
		size_t seg = (it == m_prefixLen.begin()) ? 0 : (size_t)(it - m_prefixLen.begin() - 1);
		seg = (std::min)(seg, m_segLen.size() - 1);

		float segStart = m_prefixLen[seg];
		float segLen = m_segLen[seg];
		float t = (segLen > 1e-8f) ? (s - segStart) / segLen : 0.0f;

		Math::Vec3f a = m_pts[seg];
		Math::Vec3f b = m_pts[seg + 1];
		Math::Vec3f dir = Normalize(b - a);

		out.pos = a * (1.0f - t) + b * t;
		out.tangent = dir;

		// yaw: XZ平面で計算（Y-up想定）
		out.yawDeg = Math::Rad2Deg(std::atan2(dir.x, dir.z)); // z前方, x右 ならこの形が扱いやすい
		return out;
	}

private:
	void Build()
	{
		m_prefixLen.clear();
		m_segLen.clear();

		if (m_pts.size() < 2) { m_totalLen = 0; return; }

		m_prefixLen.resize(m_pts.size() - 1);
		m_segLen.resize(m_pts.size() - 1);

		float acc = 0.0f;
		for (size_t i = 0; i + 1 < m_pts.size(); ++i)
		{
			m_prefixLen[i] = acc;
			float len = Math::Length(m_pts[i + 1], m_pts[i]);
			m_segLen[i] = len;
			acc += len;
		}
		m_totalLen = acc;
	}

	std::vector<Math::Vec3f> m_pts;
	std::vector<float> m_prefixLen; // 各セグメント開始までの累積長
	std::vector<float> m_segLen;
	float m_totalLen{};
};

struct FenceParams
{
	float spacing = 2.0f;          // 基本間隔
	float spacingJitter = 0.2f;    // 間隔のランダム（±）

	float baseWidth = 1.5f;        // 線からの基本オフセット
	float widthRand = 0.5f;        // 追加のランダム (0..widthRand)

	float baseHeight = 0.0f;       // 高さオフセット
	float heightRand = 0.2f;       // 高さランダム (±)

	float yawJitterDeg = 5.0f;     // yawランダム (±deg)

	bool bothSides = false;        // trueなら左右両側に出す
	bool randomSide = true;        // bothSides=falseのとき、左右をランダムにする
};

struct FenceInstance
{
	Math::Vec3f  position;
	float yawDeg;
	Math::Vec3f tangent;
	// 必要なら scale/tint/seed/id なども追加
};

static inline float RandRange(std::mt19937& rng, float a, float b)
{
	std::uniform_real_distribution<float> dist(a, b);
	return dist(rng);
}

// up は基本 {0,1,0}。地形法線があるなら SampleByArcLength から別途取って差し替え推奨。
static inline  Math::Vec3f ComputeRight(const  Math::Vec3f& tangent, const  Math::Vec3f& up)
{
	// 右 = up x forward（座標系に応じて逆でもOK）
	Math::Vec3f r = Math::Cross(up, tangent);
	return Normalize(r);
}

std::vector<FenceInstance> GenerateFenceAlongPolyline(
	const std::vector< Math::Vec3f>& polylinePoints,
	const FenceParams& p,
	uint32_t seed,
	Math::Vec3f worldUp = { 0,1,0 },
	float startS = 0.0f,
	float endS = -1.0f
)
{
	std::vector<FenceInstance> out;
	PolylineSampler sampler(polylinePoints);

	float total = sampler.TotalLength();
	if (total <= 0.0f) return out;

	if (endS < 0.0f) endS = total;
	startS = Math::clamp(startS, 0.0f, total);
	endS = Math::clamp(endS, 0.0f, total);
	if (endS <= startS) return out;

	std::mt19937 rng(seed);

	float s = startS;
	while (s <= endS)
	{
		// サンプル
		Sample smp = sampler.SampleByArcLength(s);
		Math::Vec3f right = ComputeRight(smp.tangent, worldUp);

		// 次の間隔（ジッター）
		float ds = p.spacing;
		if (p.spacingJitter > 0.0f)
			ds += RandRange(rng, -p.spacingJitter, +p.spacingJitter);
		ds = (std::max)(0.05f, ds);

		// どちら側に置くか
		auto emitOneSide = [&](float sideSign)
			{
				float w = p.baseWidth + RandRange(rng, 0.0f, p.widthRand);
				float h = p.baseHeight + RandRange(rng, -p.heightRand, +p.heightRand);

				Math::Vec3f pos = smp.pos + right * (w * sideSign) + worldUp * h;

				float yaw = smp.yawDeg + RandRange(rng, -p.yawJitterDeg, +p.yawJitterDeg);

				out.push_back(FenceInstance{ pos, yaw, smp.tangent });
			};

		if (p.bothSides)
		{
			emitOneSide(+1.0f);
			emitOneSide(-1.0f);
		}
		else
		{
			float sign = +1.0f;
			if (p.randomSide)
				sign = (RandRange(rng, 0.0f, 1.0f) < 0.5f) ? -1.0f : +1.0f;

			emitOneSide(sign);
		}

		s += ds;
	}

	return out;
}

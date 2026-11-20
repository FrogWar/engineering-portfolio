// =========================================================================
// Hydrogenic Orbitals & Shallow Donors: 3D Electron Cloud Simulator
//
// PHYSICS
//   Samples |psi_nlm|^2 for a hydrogen-like electron and renders it as a 3D
//   point cloud. Through the effective-mass approximation the same model
//   describes a shallow donor in a semiconductor:
//       a*  = a0 * eps_r / (m*/m0)
//       E_n = -13.606 eV * (m*/m0) / (n^2 * eps_r^2)
//   The samples are checked on screen against exact results:
//       <r> = (a*/2)[3n^2 - l(l+1)],  P(r) = r^2 |R_nl(r)|^2,  integral of P(r) dr = 1
//
// DATA STRUCTURES:
//   1. Structure of arrays: positions, velocities and anchors are stored as
//      separate contiguous float arrays, so every per-frame loop walks memory linearly.
//   2. Inverse-CDF sampler: |psi|^2 factors into r, cos(theta) and phi parts.
//      Each part is tabulated as a prefix-sum array (a discrete CDF), and
//      a sample is drawn by binary search: O(log M) per value, no rejections.
//   3. LRU cache: a hash map from orbital key to a node in a doubly linked
//      list holds recently built clouds. Revisiting an orbital is an O(1)
//      lookup plus an O(N) copy instead of rebuilding.
//   4. Uniform spatial grid: a 3D array of cell counts estimates the local
//      density around each sample, which sets its color.
//   5. Counting sort: points are drawn back to front for correct alpha
//      blending. Depth keys are bounded, so a counting sort orders them in
//      O(N + B). Press [K] to compare against std::sort, O(N log N).
//
// Requires C++17 and raylib 
// =========================================================================
#include "raylib.h"
#include "rlgl.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <numeric>
#include <cmath>
#include <cstdint>
#include <random>
#include <algorithm>
#include <string>

// =========================================================================
// 1. QUANTUM MECHANICS
//    All radii in this namespace are in units of the effective Bohr radius a*,
//    so the wavefunctions are those of hydrogen with a = 1.
// =========================================================================
namespace QuantumMath {
    constexpr double PI_D = 3.14159265358979323846;
    constexpr double RYDBERG_EV = 13.605693;  // hydrogen ground-state binding energy (infinite nuclear mass)
    constexpr double BOHR_NM = 0.0529177;     // a0 in nanometers

    double effectiveBohrNm(double epsR, double mStar) {
        return BOHR_NM * epsR / mStar;
    }

    double bindingEnergyEv(int n, double epsR, double mStar) {
        return -RYDBERG_EV * mStar / (static_cast<double>(n) * n * epsR * epsR);
    }

    double factorial(int k) {
        double res = 1.0;
        for (int i = 2; i <= k; ++i) res *= i;
        return res;
    }

    // Associated Laguerre polynomial L_k^alpha(x), modern convention.
    double assocLaguerre(int k, int alpha, double x) {
        if (k == 0) return 1.0;
        double L_prev2 = 1.0;
        double L_prev1 = 1.0 + alpha - x;
        if (k == 1) return L_prev1;

        double L_k = 0.0;
        for (int i = 2; i <= k; ++i) {
            L_k = ((2.0 * i - 1.0 + alpha - x) * L_prev1 - (i - 1.0 + alpha) * L_prev2) / i;
            L_prev2 = L_prev1;
            L_prev1 = L_k;
        }
        return L_k;
    }

    // Associated Legendre function P_l^m(x), m >= 0, without the Condon-Shortley phase
    // (only |P|^2 is used). Hand-written because libc++ (macOS) lacks std::assoc_legendre.
    double assocLegendre(int l, int m, double x) {
        double pmm = 1.0;
        if (m > 0) {
            double s = std::sqrt(std::max(0.0, (1.0 - x) * (1.0 + x)));
            double oddFactor = 1.0;
            for (int i = 1; i <= m; ++i) {
                pmm *= oddFactor * s;
                oddFactor += 2.0;
            }
        }
        if (l == m) return pmm;

        double pmmp1 = x * (2.0 * m + 1.0) * pmm;
        if (l == m + 1) return pmmp1;

        double pll = 0.0;
        for (int ll = m + 2; ll <= l; ++ll) {
            pll = ((2.0 * ll - 1.0) * x * pmmp1 - (ll + m - 1.0) * pmm) / (ll - m);
            pmm = pmmp1;
            pmmp1 = pll;
        }
        return pll;
    }

    // Normalized radial function R_nl(r), r in units of a*
    double radialWavefunction(int n, int l, double r) {
        double rho = 2.0 * r / n;
        double prefactor = std::sqrt(
            std::pow(2.0 / n, 3.0) * factorial(n - l - 1) / (2.0 * n * factorial(n + l))
        );
        return prefactor * std::exp(-rho / 2.0) * std::pow(rho, l) * assocLaguerre(n - l - 1, 2 * l + 1, rho);
    }

    // Radial probability density P(r) = r^2 R_nl^2, normalized so its integral is 1
    double radialProbability(int n, int l, double r) {
        double R = radialWavefunction(n, l, r);
        return r * r * R * R;
    }

    // |Y_lm|^2 as a function of u = cos(theta). Per unit solid angle, dOmega = du dphi.
    double sphericalHarmonicSqCos(int l, int m, double u) {
        int absM = std::abs(m);
        double P = assocLegendre(l, absM, std::clamp(u, -1.0, 1.0));
        double norm = ((2.0 * l + 1.0) / (4.0 * PI_D)) * (factorial(l - absM) / factorial(l + absM));
        return norm * P * P;
    }

    // Extra azimuthal factor for real orbitals (averages to 1 over phi)
    double azimuthalFactor(int m, double phi, bool realOrbital) {
        if (!realOrbital || m == 0) return 1.0;
        double c = (m > 0) ? std::cos(m * phi) : std::sin(-m * phi);
        return 2.0 * c * c;
    }

    double expectationR(int n, int l) { return 0.5 * (3.0 * n * n - l * (l + 1.0)); }

    // Sampling cutoff: the probability beyond this radius is negligible for n <= 5
    double samplingRadius(int n) { return 2.0 * n * n + 8.0 * n; }
}

// =========================================================================
// 2. INVERSE-CDF TABLE (prefix sums + binary search)
//    The PDF is tabulated on M points and integrated with the trapezoid rule
//    into a running prefix sum, the CDF. To draw a sample, take u ~ U(0,1),
//    binary-search the CDF for the bin containing u, then interpolate within
//    the bin. Build O(M), each sample O(log M), and no samples are rejected.
// =========================================================================
class InverseCdfTable {
    std::vector<double> x_;
    std::vector<double> cdf_;
    double total_ = 0.0;

    // Binary search: returns the first index j with cdf_[j] > u
    size_t upperBound(double u) const {
        size_t lo = 0, hi = cdf_.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (cdf_[mid] <= u) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

public:
    template <class Pdf>
    void build(double a, double b, int intervals, Pdf pdf) {
        x_.resize(intervals + 1);
        cdf_.resize(intervals + 1);
        const double h = (b - a) / intervals;

        x_[0] = a;
        cdf_[0] = 0.0;
        double prev = pdf(a);
        for (int i = 1; i <= intervals; ++i) {
            x_[i] = a + i * h;
            double cur = pdf(x_[i]);
            cdf_[i] = cdf_[i - 1] + 0.5 * (prev + cur) * h;   // prefix sum of trapezoid areas
            prev = cur;
        }
        total_ = cdf_.back();
        if (total_ > 0.0) for (double& c : cdf_) c /= total_;
    }

    double sample(double u) const {
        size_t j = std::clamp<size_t>(upperBound(u), 1, cdf_.size() - 1);
        double c0 = cdf_[j - 1], c1 = cdf_[j];
        double t = (c1 > c0) ? (u - c0) / (c1 - c0) : 0.5;
        return x_[j - 1] + t * (x_[j] - x_[j - 1]);
    }

    double total() const { return total_; }  // integral of the unnormalized PDF
};

// =========================================================================
// 3. SAMPLED CLOUD + VALIDATION STATISTICS
// =========================================================================
struct CloudStats {
    static constexpr int BINS = 60;
    double rMax = 1.0;
    double meanR = 0.0, stdErrR = 0.0;
    double theoryMeanR = 0.0;
    double normIntegral = 0.0;
    int radialNodes = 0;
    size_t samples = 0;
    float hist[BINS] = {};
    float theory[BINS] = {};
};

// A built cloud as stored in the cache: sampled positions plus their statistics
struct CloudSnapshot {
    std::vector<float> x, y, z;
    CloudStats stats;
    float worldScale = 1.0f;
};

struct ParticleSystem {
    size_t activeCount = 0;
    size_t maxCapacity;
    float radius;
    float worldScale = 1.0f;                // display units per a*
    CloudStats stats;

    // Structure of arrays: one contiguous array per field
    std::vector<float> posX, posY, posZ;
    std::vector<float> homeX, homeY, homeZ;
    std::vector<float> velX, velY, velZ;
    std::vector<Color> tint;                // density-based color per sample

    ParticleSystem(size_t capacity, float r) : maxCapacity(capacity), radius(r) {
        posX.resize(capacity);  posY.resize(capacity);  posZ.resize(capacity);
        homeX.resize(capacity); homeY.resize(capacity); homeZ.resize(capacity);
        velX.resize(capacity);  velY.resize(capacity);  velZ.resize(capacity);
        tint.resize(capacity);
    }

    void emit(float x, float y, float z) {
        if (activeCount >= maxCapacity) return;
        posX[activeCount] = homeX[activeCount] = x;
        posY[activeCount] = homeY[activeCount] = y;
        posZ[activeCount] = homeZ[activeCount] = z;
        velX[activeCount] = velY[activeCount] = velZ[activeCount] = 0.0f;
        ++activeCount;
    }

    void clear() { activeCount = 0; }

    // Draw samples from |psi_nlm|^2. In (r, u = cos theta, phi) the density
    // r^2 R^2 |Y|^2 * azimuthal(phi) is a product of three independent 1D
    // densities, so each coordinate is drawn from its own inverse-CDF table.
    void spawnQuantumCloud(int n, int l, int m, bool realOrbital, size_t count) {
        using namespace QuantumMath;
        clear();
        size_t target = std::min(count, maxCapacity);
        // seed_seq mixes all quantum numbers into the full generator state
        std::seed_seq seed{ 1337u, static_cast<unsigned>(n), static_cast<unsigned>(l),
                            static_cast<unsigned>(m + 8), static_cast<unsigned>(realOrbital) };
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> uni(0.0, 1.0);

        const double rMax = samplingRadius(n);
        worldScale = static_cast<float>(130.0 / expectationR(n, l));

        InverseCdfTable radial, polar, azimuth;
        radial.build(0.0, rMax, 4096, [&](double r) { return radialProbability(n, l, r); });
        polar.build(-1.0, 1.0, 2048, [&](double u) { return sphericalHarmonicSqCos(l, m, u); });
        const bool needAzimuthTable = realOrbital && m != 0;  // complex orbitals: phi is uniform
        if (needAzimuthTable) {
            azimuth.build(0.0, 2.0 * PI_D, 2048, [&](double phi) { return azimuthalFactor(m, phi, true); });
        }

        CloudStats s;
        s.rMax = rMax;
        const double binWidth = rMax / CloudStats::BINS;
        double sumR = 0.0, sumR2 = 0.0;

        for (size_t k = 0; k < target; ++k) {
            double r = radial.sample(uni(rng));
            double u = polar.sample(uni(rng));
            double phi = needAzimuthTable ? azimuth.sample(uni(rng)) : 2.0 * PI_D * uni(rng);
            double sinTheta = std::sqrt(std::max(0.0, 1.0 - u * u));

            // Y is the polar (quantization) axis
            emit(static_cast<float>(r * sinTheta * std::cos(phi) * worldScale),
                 static_cast<float>(r * u * worldScale),
                 static_cast<float>(r * sinTheta * std::sin(phi) * worldScale));

            sumR += r;
            sumR2 += r * r;
            int b = std::min(CloudStats::BINS - 1, static_cast<int>(r / binWidth));
            s.hist[b] += 1.0f;
        }

        s.samples = activeCount;
        s.theoryMeanR = expectationR(n, l);
        s.radialNodes = n - l - 1;
        s.normIntegral = radial.total();      // the CDF's last prefix sum is the normalization integral
        if (activeCount > 1) {
            double N = static_cast<double>(activeCount);
            s.meanR = sumR / N;
            s.stdErrR = std::sqrt(std::max(0.0, sumR2 / N - s.meanR * s.meanR) / N);
        }
        for (int b = 0; b < CloudStats::BINS; ++b) {
            double acc = 0.0;
            for (int k = 0; k < 8; ++k) acc += radialProbability(n, l, (b + (k + 0.5) / 8.0) * binWidth);
            s.theory[b] = static_cast<float>(acc / 8.0 * binWidth * activeCount);
        }
        stats = s;
    }

    CloudSnapshot snapshot() const {
        CloudSnapshot snap;
        snap.x.assign(homeX.begin(), homeX.begin() + activeCount);
        snap.y.assign(homeY.begin(), homeY.begin() + activeCount);
        snap.z.assign(homeZ.begin(), homeZ.begin() + activeCount);
        snap.stats = stats;
        snap.worldScale = worldScale;
        return snap;
    }

    void loadSnapshot(const CloudSnapshot& snap) {
        clear();
        for (size_t i = 0; i < snap.x.size() && i < maxCapacity; ++i) emit(snap.x[i], snap.y[i], snap.z[i]);
        stats = snap.stats;
        worldScale = snap.worldScale;
    }

    // Uniform spatial grid: a G^3 array of cell counts. Each sample's local
    // density is the count over its 3x3x3 block of cells; log density is
    // mapped onto a blue -> cyan -> white color ramp.
    void computeDensityTint() {
        constexpr int G = 40;
        if (activeCount == 0) return;

        float L = 1e-3f;
        for (size_t i = 0; i < activeCount; ++i) {
            L = std::max({ L, std::abs(homeX[i]), std::abs(homeY[i]), std::abs(homeZ[i]) });
        }
        L *= 1.0001f;
        const float toCell = G / (2.0f * L);

        std::vector<uint32_t> cellCount(static_cast<size_t>(G) * G * G, 0);
        std::vector<uint32_t> cellOf(activeCount);
        auto cellCoord = [&](float v) { return std::clamp(static_cast<int>((v + L) * toCell), 0, G - 1); };
        for (size_t i = 0; i < activeCount; ++i) {
            int cx = cellCoord(homeX[i]), cy = cellCoord(homeY[i]), cz = cellCoord(homeZ[i]);
            cellOf[i] = static_cast<uint32_t>((cz * G + cy) * G + cx);
            ++cellCount[cellOf[i]];
        }

        std::vector<float> logDensity(activeCount);
        float maxLog = 1e-6f;
        for (size_t i = 0; i < activeCount; ++i) {
            int c = static_cast<int>(cellOf[i]);
            int cx = c % G, cy = (c / G) % G, cz = c / (G * G);
            uint32_t sum = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int x = cx + dx, y = cy + dy, z = cz + dz;
                        if (x < 0 || y < 0 || z < 0 || x >= G || y >= G || z >= G) continue;
                        sum += cellCount[(z * G + y) * G + x];
                    }
            logDensity[i] = std::log1p(static_cast<float>(sum));
            maxLog = std::max(maxLog, logDensity[i]);
        }

        auto lerp = [](unsigned char a, unsigned char b, float t) {
            return static_cast<unsigned char>(a + (b - a) * t);
        };
        for (size_t i = 0; i < activeCount; ++i) {
            float t = logDensity[i] / maxLog;
            Color c;
            if (t < 0.6f) {
                float k = t / 0.6f;                                 // deep blue -> cyan
                c = { lerp(40, 70, k), lerp(80, 190, k), lerp(210, 255, k), 255 };
            } else {
                float k = (t - 0.6f) / 0.4f;                        // cyan -> near white
                c = { lerp(70, 225, k), lerp(190, 245, k), 255, 255 };
            }
            tint[i] = c;
        }
    }
};

// =========================================================================
// 4. LRU CLOUD CACHE (hash map + doubly linked list)
//    The list keeps clouds in recency order (front = most recent). The hash
//    map points from a key to its list node, so lookup, move-to-front and
//    eviction of the least recently used entry are all O(1).
// =========================================================================
class CloudCache {
    using Entry = std::pair<uint64_t, CloudSnapshot>;
    size_t capacity_;
    std::list<Entry> recency_;
    std::unordered_map<uint64_t, std::list<Entry>::iterator> index_;

public:
    size_t hits = 0, misses = 0;

    explicit CloudCache(size_t capacity) : capacity_(capacity) {}

    static uint64_t makeKey(int n, int l, int m, bool real, size_t count) {
        return static_cast<uint64_t>(n)
             | (static_cast<uint64_t>(l) << 4)
             | (static_cast<uint64_t>(m + 8) << 8)
             | (static_cast<uint64_t>(real ? 1 : 0) << 12)
             | (static_cast<uint64_t>(count) << 13);
    }

    const CloudSnapshot* get(uint64_t key) {
        auto it = index_.find(key);
        if (it == index_.end()) { ++misses; return nullptr; }
        recency_.splice(recency_.begin(), recency_, it->second);   // move to front in O(1)
        ++hits;
        return &it->second->second;
    }

    void put(uint64_t key, CloudSnapshot&& snap) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(snap);
            recency_.splice(recency_.begin(), recency_, it->second);
            return;
        }
        recency_.emplace_front(key, std::move(snap));
        index_[key] = recency_.begin();
        if (recency_.size() > capacity_) {                         // evict least recently used
            index_.erase(recency_.back().first);
            recency_.pop_back();
        }
    }

    size_t size() const { return recency_.size(); }
    size_t capacity() const { return capacity_; }
};

// =========================================================================
// 5. CLOUD DYNAMICS
//    A stationary state has a time-independent |psi|^2. Two things move:
//      (a) Probability current. For complex psi_nlm the flow velocity is
//          v_phi ~ m / rho. Moving samples along it leaves |psi|^2 unchanged
//          (div j = 0). Real orbitals carry no current.
//      (b) The right-click probe. A classical illustration only: points are
//          pushed by a Coulomb-like force and spring back. (A quantum
//          treatment would be the Stark effect.)
// =========================================================================
struct FieldPerturber3D {
    bool active = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float charge = 45.0f;
};

class CloudDynamics {
private:
    ParticleSystem& ps;

    void advectAlongCurrent(float dt, int m) {
        const size_t count = ps.activeCount;
        float* px = ps.posX.data(); float* pz = ps.posZ.data();
        float* hx = ps.homeX.data(); float* hz = ps.homeZ.data();
        float* vx = ps.velX.data(); float* vz = ps.velZ.data();

        const float kCurrent = 6000.0f * static_cast<float>(m);
        const float soft = 300.0f;

        for (size_t i = 0; i < count; ++i) {
            float rho2 = hx[i] * hx[i] + hz[i] * hz[i];
            float angle = kCurrent * dt / (rho2 + soft);
            float c = std::cos(angle), s = std::sin(angle);

            float nhx = hx[i] * c - hz[i] * s, nhz = hx[i] * s + hz[i] * c;
            float npx = px[i] * c - pz[i] * s, npz = px[i] * s + pz[i] * c;
            float nvx = vx[i] * c - vz[i] * s, nvz = vx[i] * s + vz[i] * c;
            hx[i] = nhx; hz[i] = nhz;
            px[i] = npx; pz[i] = npz;
            vx[i] = nvx; vz[i] = nvz;
        }
    }

    template <bool WithProbe>
    void relax(float dt, const FieldPerturber3D& probe) {
        const size_t count = ps.activeCount;
        float* px = ps.posX.data(); float* py = ps.posY.data(); float* pz = ps.posZ.data();
        const float* hx = ps.homeX.data(); const float* hy = ps.homeY.data(); const float* hz = ps.homeZ.data();
        float* vx = ps.velX.data(); float* vy = ps.velY.data(); float* vz = ps.velZ.data();

        const float springK = 16.0f;
        const float damping = std::pow(0.88f, dt * 60.0f);
        const float probeStrength = -8500.0f * probe.charge;

        for (size_t i = 0; i < count; ++i) {
            float fx = (hx[i] - px[i]) * springK;
            float fy = (hy[i] - py[i]) * springK;
            float fz = (hz[i] - pz[i]) * springK;

            if constexpr (WithProbe) {
                float ax = px[i] - probe.x, ay = py[i] - probe.y, az = pz[i] - probe.z;
                float d2 = ax * ax + ay * ay + az * az + 250.0f;
                float f = probeStrength / (d2 * std::sqrt(d2));
                fx += f * ax; fy += f * ay; fz += f * az;
            }

            vx[i] = (vx[i] + fx * dt) * damping;
            vy[i] = (vy[i] + fy * dt) * damping;
            vz[i] = (vz[i] + fz * dt) * damping;
            px[i] += vx[i] * dt;
            py[i] += vy[i] * dt;
            pz[i] += vz[i] * dt;
        }
    }

public:
    explicit CloudDynamics(ParticleSystem& system) : ps(system) {}

    void step(float dt, int m, bool realOrbital, const FieldPerturber3D& probe) {
        if (ps.activeCount == 0) return;
        if (!realOrbital && m != 0) advectAlongCurrent(dt, m);
        if (probe.active) relax<true>(dt, probe);
        else              relax<false>(dt, probe);
    }
};

// =========================================================================
// 6. RENDERER
//    Pass 1 projects every sample into screen-space arrays (SoA).
//    Pass 2 orders them back to front (counting sort or std::sort).
//    Pass 3 submits one textured quad per sample in a single batched draw.
//    A quad is 4 vertices; DrawCircle builds a 36-segment fan per point.
// =========================================================================
struct ViewTransform {
    float cosY, sinY, cosP, sinP, zoom;
    Vector2 center;

    Vector2 project(float x, float y, float z, float& depth) const {
        float x1 = x * cosY + z * sinY;
        float z1 = -x * sinY + z * cosY;
        float y2 = y * cosP - z1 * sinP;
        depth = y * sinP + z1 * cosP;
        float persp = 650.0f / std::max(150.0f, 650.0f + depth);
        return { center.x + x1 * zoom * persp, center.y - y2 * zoom * persp };
    }
};

class CloudRenderer {
public:
    enum class SortMode { Counting, Comparison };
    SortMode sortMode = SortMode::Counting;
    bool glow = false;
    double projectMs = 0.0, sortMs = 0.0, submitMs = 0.0;   // smoothed timings

    void init() {
        Image img = GenImageGradientRadial(32, 32, 0.15f, WHITE, BLANK);
        dot_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(dot_, TEXTURE_FILTER_BILINEAR);
    }

    void shutdown() { UnloadTexture(dot_); }

    void draw(const ParticleSystem& ps, const ViewTransform& view) {
        const size_t n = ps.activeCount;
        if (n == 0) return;

        // Pass 1: projection into screen-space arrays
        double t0 = GetTime();
        sx_.resize(n); sy_.resize(n); depth_.resize(n);
        float dMin = 1e30f, dMax = -1e30f;
        for (size_t i = 0; i < n; ++i) {
            float d;
            Vector2 p = view.project(ps.posX[i], ps.posY[i], ps.posZ[i], d);
            sx_[i] = p.x; sy_[i] = p.y; depth_[i] = d;
            dMin = std::min(dMin, d);
            dMax = std::max(dMax, d);
        }

        // Pass 2: back-to-front order (additive glow is order-independent, so it skips this)
        double t1 = GetTime();
        if (!glow) {
            if (sortMode == SortMode::Counting) countingSortFarToNear(n, dMin, dMax);
            else                                comparisonSortFarToNear(n);
        }

        // Pass 3: one batched quad per sample
        double t2 = GetTime();
        const float radius = ps.radius * std::clamp(view.zoom, 0.6f, 1.6f);
        const float depthRange = 300.0f;

        if (glow) BeginBlendMode(BLEND_ADDITIVE);
        rlSetTexture(dot_.id);
        rlBegin(RL_QUADS);
        for (size_t k = 0; k < n; ++k) {
            size_t i = glow ? k : order_[k];
            float depthNorm = std::clamp((depthRange - depth_[i]) / (2.0f * depthRange), 0.2f, 1.0f);
            float half = 1.6f * radius * (0.75f + 0.4f * depthNorm);
            Color c = ps.tint[i];
            c.a = static_cast<unsigned char>((glow ? 25.0f + 95.0f * depthNorm : 45.0f + 150.0f * depthNorm));

            rlCheckRenderBatchLimit(4);   // flushes and continues if the batch buffer is full
            rlColor4ub(c.r, c.g, c.b, c.a);
            rlTexCoord2f(0.0f, 0.0f); rlVertex2f(sx_[i] - half, sy_[i] - half);
            rlTexCoord2f(0.0f, 1.0f); rlVertex2f(sx_[i] - half, sy_[i] + half);
            rlTexCoord2f(1.0f, 1.0f); rlVertex2f(sx_[i] + half, sy_[i] + half);
            rlTexCoord2f(1.0f, 0.0f); rlVertex2f(sx_[i] + half, sy_[i] - half);
        }
        rlEnd();
        rlSetTexture(0);
        if (glow) EndBlendMode();
        double t3 = GetTime();

        smooth(projectMs, (t1 - t0) * 1000.0);
        smooth(sortMs, glow ? 0.0 : (t2 - t1) * 1000.0);
        smooth(submitMs, (t3 - t2) * 1000.0);
    }

private:
    static constexpr int BUCKETS = 4096;
    Texture2D dot_{};
    std::vector<float> sx_, sy_, depth_;
    std::vector<uint32_t> order_, counts_;
    std::vector<uint16_t> keys_;

    static void smooth(double& acc, double sample) { acc = 0.92 * acc + 0.08 * sample; }

    // Counting sort on quantized depth: O(N + B) time, O(N + B) extra space, stable.
    // Larger depth is farther away, so key 0 holds the farthest points and is drawn first.
    void countingSortFarToNear(size_t n, float dMin, float dMax) {
        keys_.resize(n);
        order_.resize(n);
        counts_.assign(BUCKETS + 1, 0);

        const float k = (BUCKETS - 1) / std::max(dMax - dMin, 1e-3f);
        for (size_t i = 0; i < n; ++i) {
            uint16_t key = static_cast<uint16_t>((dMax - depth_[i]) * k);
            keys_[i] = key;
            ++counts_[key + 1];                                  // histogram
        }
        for (int b = 1; b <= BUCKETS; ++b) counts_[b] += counts_[b - 1];   // prefix sums = bucket start offsets
        for (size_t i = 0; i < n; ++i) order_[counts_[keys_[i]]++] = static_cast<uint32_t>(i);  // scatter
    }

    // Comparison sort for reference: O(N log N)
    void comparisonSortFarToNear(size_t n) {
        order_.resize(n);
        std::iota(order_.begin(), order_.end(), 0u);
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) { return depth_[a] > depth_[b]; });
    }
};

// =========================================================================
// 7. UI WIDGETS
// =========================================================================
bool DrawIntSlider(Rectangle bounds, const char* label, int& value, int minVal, int maxVal, int step = 1) {
    bool changed = false;
    Vector2 mousePos = GetMousePosition();
    Rectangle hitArea = { bounds.x - 10, bounds.y - 6, bounds.width + 20, bounds.height + 12 };

    if (CheckCollisionPointRec(mousePos, hitArea) && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float t = std::clamp((mousePos.x - bounds.x) / bounds.width, 0.0f, 1.0f);
        int raw = minVal + static_cast<int>(std::round(t * (maxVal - minVal)));
        int snapped = minVal + ((raw - minVal + step / 2) / step) * step;
        snapped = std::clamp(snapped, minVal, maxVal);
        if (snapped != value) {
            value = snapped;
            changed = true;
        }
    }

    value = std::clamp(value, minVal, maxVal);
    float progress = (maxVal > minVal) ? static_cast<float>(value - minVal) / (maxVal - minVal) : 0.0f;
    float handleX = bounds.x + progress * bounds.width;
    float handleY = bounds.y + bounds.height * 0.5f;

    DrawRectangleRounded(bounds, 0.5f, 4, Color{ 45, 50, 65, 255 });
    DrawRectangleRounded({ bounds.x, bounds.y, progress * bounds.width, bounds.height }, 0.5f, 4, Color{ 60, 140, 245, 255 });
    DrawCircle(static_cast<int>(handleX), static_cast<int>(handleY), bounds.height * 0.85f, Color{ 235, 240, 250, 255 });
    DrawCircleLines(static_cast<int>(handleX), static_cast<int>(handleY), bounds.height * 0.85f, Color{ 20, 20, 30, 255 });

    DrawText(label, static_cast<int>(bounds.x), static_cast<int>(bounds.y - 18), 14, LIGHTGRAY);
    DrawText(TextFormat("%d", value), static_cast<int>(bounds.x + bounds.width + 12), static_cast<int>(bounds.y - 1), 15, RAYWHITE);
    return changed;
}

bool DrawFloatSlider(Rectangle bounds, const char* label, float& value, float minVal, float maxVal,
                     float step = 0.1f, const char* fmt = "%.1f") {
    bool changed = false;
    Vector2 mousePos = GetMousePosition();
    Rectangle hitArea = { bounds.x - 10, bounds.y - 6, bounds.width + 20, bounds.height + 12 };

    if (CheckCollisionPointRec(mousePos, hitArea) && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float t = std::clamp((mousePos.x - bounds.x) / bounds.width, 0.0f, 1.0f);
        float raw = minVal + t * (maxVal - minVal);
        float snapped = std::clamp(std::round(raw / step) * step, minVal, maxVal);
        if (std::abs(snapped - value) > 1e-4f) {
            value = snapped;
            changed = true;
        }
    }

    value = std::clamp(value, minVal, maxVal);
    float progress = (maxVal > minVal) ? (value - minVal) / (maxVal - minVal) : 0.0f;
    float handleX = bounds.x + progress * bounds.width;
    float handleY = bounds.y + bounds.height * 0.5f;

    DrawRectangleRounded(bounds, 0.5f, 4, Color{ 45, 50, 65, 255 });
    DrawRectangleRounded({ bounds.x, bounds.y, progress * bounds.width, bounds.height }, 0.5f, 4, Color{ 80, 200, 160, 255 });
    DrawCircle(static_cast<int>(handleX), static_cast<int>(handleY), bounds.height * 0.85f, Color{ 235, 240, 250, 255 });
    DrawCircleLines(static_cast<int>(handleX), static_cast<int>(handleY), bounds.height * 0.85f, Color{ 20, 20, 30, 255 });

    DrawText(label, static_cast<int>(bounds.x), static_cast<int>(bounds.y - 18), 14, LIGHTGRAY);
    DrawText(TextFormat(fmt, value), static_cast<int>(bounds.x + bounds.width + 12), static_cast<int>(bounds.y - 1), 15, RAYWHITE);
    return changed;
}

void DrawRadialHistogram(Rectangle panel, const CloudStats& s) {
    DrawRectangleRec(panel, Fade(BLACK, 0.82f));
    DrawRectangleLinesEx(panel, 1.0f, Color{ 70, 75, 95, 255 });
    DrawText("Radial distribution: samples vs. exact P(r)", static_cast<int>(panel.x + 10), static_cast<int>(panel.y + 8), 13, LIGHTGRAY);

    Rectangle plot = { panel.x + 14, panel.y + 30, panel.width - 28, panel.height - 58 };
    float maxV = 1.0f;
    for (int b = 0; b < CloudStats::BINS; ++b) maxV = std::max({ maxV, s.hist[b], s.theory[b] });
    maxV *= 1.08f;

    const float barW = plot.width / CloudStats::BINS;
    const float baseY = plot.y + plot.height;
    for (int b = 0; b < CloudStats::BINS; ++b) {
        float h = s.hist[b] / maxV * plot.height;
        DrawRectangleRec({ plot.x + b * barW, baseY - h, std::max(1.0f, barW - 1.0f), h }, Color{ 65, 185, 255, 150 });
    }

    Vector2 prev = { 0, 0 };
    for (int b = 0; b < CloudStats::BINS; ++b) {
        Vector2 pt = { plot.x + (b + 0.5f) * barW, baseY - s.theory[b] / maxV * plot.height };
        if (b > 0) DrawLineEx(prev, pt, 2.0f, Color{ 255, 190, 90, 255 });
        prev = pt;
    }

    DrawLineEx({ plot.x, baseY }, { plot.x + plot.width, baseY }, 1.0f, GRAY);
    DrawText("0", static_cast<int>(plot.x), static_cast<int>(baseY + 4), 12, GRAY);
    const char* rLabel = TextFormat("r = %.0f a*", s.rMax);
    DrawText(rLabel, static_cast<int>(plot.x + plot.width - MeasureText(rLabel, 12)), static_cast<int>(baseY + 4), 12, GRAY);
    DrawText("bars: sampled", static_cast<int>(plot.x + 60), static_cast<int>(baseY + 4), 12, Color{ 65, 185, 255, 255 });
    DrawText("line: r^2 |R_nl|^2", static_cast<int>(plot.x + 160), static_cast<int>(baseY + 4), 12, Color{ 255, 190, 90, 255 });
}

void DrawScaleBar(float pxPerNm, Vector2 anchorRight) {
    if (pxPerNm <= 0.0f) return;
    double targetNm = 140.0 / pxPerNm;
    double decade = std::pow(10.0, std::floor(std::log10(targetNm)));
    double niceNm = decade;
    for (double c : { 1.0, 2.0, 5.0 }) if (c * decade <= targetNm) niceNm = c * decade;

    float len = static_cast<float>(niceNm * pxPerNm);
    Vector2 a = { anchorRight.x - len, anchorRight.y };
    Vector2 b = anchorRight;
    DrawLineEx(a, b, 3.0f, RAYWHITE);
    DrawLineEx({ a.x, a.y - 6 }, { a.x, a.y + 6 }, 2.0f, RAYWHITE);
    DrawLineEx({ b.x, b.y - 6 }, { b.x, b.y + 6 }, 2.0f, RAYWHITE);
    const char* label = (niceNm < 1.0) ? TextFormat("%g pm", niceNm * 1000.0) : TextFormat("%g nm", niceNm);
    DrawText(label, static_cast<int>(a.x + len * 0.5f - MeasureText(label, 15) * 0.5f), static_cast<int>(a.y - 24), 15, RAYWHITE);
}

// =========================================================================
// 8. MAIN APPLICATION LOOP
// =========================================================================
int main() {
    constexpr int SCREEN_WIDTH = 1280;
    constexpr int SCREEN_HEIGHT = 800;
    constexpr size_t MAX_CAPACITY = 50000;
    constexpr float PARTICLE_RADIUS = 1.35f;

    // Hydrogenic donor model for silicon (conductivity effective mass 0.26 m0)
    constexpr float SI_EPS = 11.7f;
    constexpr float SI_MSTAR = 0.26f;

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Hydrogenic Orbitals & Shallow Donors - Electron Cloud Simulator");
    SetTargetFPS(60);

    ParticleSystem ps(MAX_CAPACITY, PARTICLE_RADIUS);
    CloudDynamics sim(ps);
    CloudRenderer renderer;
    renderer.init();
    CloudCache cache(12);
    FieldPerturber3D perturber;

    int q_n = 4;
    int q_l = 3;
    int q_m = 0;
    float eps_r = 1.0f;
    float mStar = 1.0f;
    int targetDensity = 25000;
    bool realOrbital = false;
    bool physicsActive = true;

    float camYaw = 0.35f;
    float camPitch = 0.25f;
    float camZoom = 1.15f;
    bool isRotating = false;
    Vector2 lastMousePos = { 0.0f, 0.0f };

    Vector2 screenCenter = { SCREEN_WIDTH * 0.62f, SCREEN_HEIGHT * 0.50f };

    double lastBuildMs = 0.0;
    bool lastBuildWasHit = false;
    double physicsMs = 0.0;

    // Look the orbital up in the cache, build it on a miss, then recolor by density
    auto respawn = [&]() {
        double t0 = GetTime();
        uint64_t key = CloudCache::makeKey(q_n, q_l, q_m, realOrbital, static_cast<size_t>(targetDensity));
        if (const CloudSnapshot* hit = cache.get(key)) {
            ps.loadSnapshot(*hit);
            lastBuildWasHit = true;
        } else {
            ps.spawnQuantumCloud(q_n, q_l, q_m, realOrbital, static_cast<size_t>(targetDensity));
            cache.put(key, ps.snapshot());
            lastBuildWasHit = false;
        }
        ps.computeDensityTint();
        lastBuildMs = (GetTime() - t0) * 1000.0;
    };
    respawn();

    Rectangle uiPanel   = { 15, 15, 390, 575 };
    Rectangle histPanel = { 15, 600, 390, 185 };
    Rectangle dsPanel   = { static_cast<float>(SCREEN_WIDTH - 420), 15, 405, 136 };

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.033f);

        Vector2 mousePos = GetMousePosition();
        bool mouseOverUI = CheckCollisionPointRec(mousePos, uiPanel) ||
                           CheckCollisionPointRec(mousePos, histPanel) ||
                           CheckCollisionPointRec(mousePos, dsPanel);

        if (!mouseOverUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            isRotating = true;
            lastMousePos = mousePos;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) isRotating = false;
        if (isRotating && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            camYaw += (mousePos.x - lastMousePos.x) * 0.008f;
            camPitch = std::clamp(camPitch + (mousePos.y - lastMousePos.y) * 0.008f, -1.45f, 1.45f);
            lastMousePos = mousePos;
        }

        float wheel = GetMouseWheelMove();
        if (!mouseOverUI && wheel != 0.0f) camZoom = std::clamp(camZoom + wheel * 0.12f, 0.35f, 3.5f);

        ViewTransform view{ std::cos(camYaw), std::sin(camYaw), std::cos(camPitch), std::sin(camPitch), camZoom, screenCenter };

        // Right-click probe (mapped onto the plane through the nucleus)
        if (!mouseOverUI && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            perturber.active = true;
            float sx = (mousePos.x - screenCenter.x) / camZoom;
            float sy = -(mousePos.y - screenCenter.y) / camZoom;
            float y1 = sy * view.cosP;
            float z1 = -sy * view.sinP;
            perturber.x = sx * view.cosY - z1 * view.sinY;
            perturber.y = y1;
            perturber.z = sx * view.sinY + z1 * view.cosY;
        } else {
            perturber.active = false;
        }

        // Shortcuts
        if (IsKeyPressed(KEY_SPACE)) perturber.charge *= -1.0f;
        if (IsKeyPressed(KEY_P)) physicsActive = !physicsActive;
        if (IsKeyPressed(KEY_O)) { realOrbital = !realOrbital; respawn(); }
        if (IsKeyPressed(KEY_G)) renderer.glow = !renderer.glow;
        if (IsKeyPressed(KEY_K)) {
            renderer.sortMode = (renderer.sortMode == CloudRenderer::SortMode::Counting)
                              ? CloudRenderer::SortMode::Comparison : CloudRenderer::SortMode::Counting;
        }
        if (IsKeyPressed(KEY_S)) {
            bool isVacuum = (eps_r < 1.05f && std::abs(mStar - 1.0f) < 0.01f);
            eps_r = isVacuum ? SI_EPS : 1.0f;
            mStar = isVacuum ? SI_MSTAR : 1.0f;
        }
        if (IsKeyPressed(KEY_R)) {
            camYaw = 0.35f; camPitch = 0.25f; camZoom = 1.15f;
            respawn();
        }

        if (physicsActive) {
            double t0 = GetTime();
            sim.step(dt, q_m, realOrbital, perturber);
            physicsMs = 0.92 * physicsMs + 0.08 * (GetTime() - t0) * 1000.0;
        }

        // ----------------------------------------------------------------
        // Render
        // ----------------------------------------------------------------
        BeginDrawing();
        ClearBackground(Color{ 10, 12, 18, 255 });

        float dDummy;
        Vector2 origin2D = view.project(0, 0, 0, dDummy);
        DrawLineV(origin2D, view.project(140, 0, 0, dDummy), Fade(RED, 0.25f));
        DrawLineV(origin2D, view.project(0, 140, 0, dDummy), Fade(GREEN, 0.25f));    // polar (quantization) axis
        DrawLineV(origin2D, view.project(0, 0, 140, dDummy), Fade(SKYBLUE, 0.25f));

        DrawCircleGradient(static_cast<int>(origin2D.x), static_cast<int>(origin2D.y), 20.0f * camZoom, Fade(RED, 0.35f), BLANK);
        DrawCircleV(origin2D, 4.5f * std::clamp(camZoom, 0.7f, 1.5f), Color{ 255, 80, 80, 255 });

        if (perturber.active) {
            Color glowC = (perturber.charge > 0.0f) ? Color{ 255, 200, 50, 95 } : Color{ 180, 60, 255, 95 };
            Color core = (perturber.charge > 0.0f) ? Color{ 255, 220, 90, 255 } : Color{ 210, 120, 255, 255 };
            DrawCircleGradient(static_cast<int>(mousePos.x), static_cast<int>(mousePos.y), 60.0f, glowC, BLANK);
            DrawCircle(static_cast<int>(mousePos.x), static_cast<int>(mousePos.y), 14.0f, core);
            DrawCircleLines(static_cast<int>(mousePos.x), static_cast<int>(mousePos.y), 18.0f, RAYWHITE);
        }

        renderer.draw(ps, view);

        const double aStarNm = QuantumMath::effectiveBohrNm(eps_r, mStar);
        DrawScaleBar(static_cast<float>(ps.worldScale * camZoom / aStarNm),
                     { static_cast<float>(SCREEN_WIDTH - 40), static_cast<float>(SCREEN_HEIGHT - 40) });

        // ----------------------------------------------------------------
        // Left panel: physics controls and validation
        // ----------------------------------------------------------------
        DrawRectangleRec(uiPanel, Fade(BLACK, 0.82f));
        DrawRectangleLinesEx(uiPanel, 1.0f, Color{ 70, 75, 95, 255 });

        DrawFPS(28, 24);
        DrawText(TextFormat("Samples: %zu | Zoom: %.2fx", ps.activeCount, camZoom), 120, 26, 15, RAYWHITE);

        bool cloudChanged = false;
        cloudChanged |= DrawIntSlider({ 30, 80, 235, 12 }, "Principal (n): Shell Energy", q_n, 1, 5);
        if (q_l >= q_n) q_l = q_n - 1;
        cloudChanged |= DrawIntSlider({ 30, 128, 235, 12 }, "Azimuthal (l): Orbital Shape", q_l, 0, q_n - 1);
        q_m = std::clamp(q_m, -q_l, q_l);
        cloudChanged |= DrawIntSlider({ 30, 176, 235, 12 }, "Magnetic (m_l): Orientation", q_m, -q_l, q_l);
        DrawFloatSlider({ 30, 224, 235, 12 }, "Relative permittivity (eps_r)", eps_r, 1.0f, 15.0f, 0.1f, "%.1f");
        DrawFloatSlider({ 30, 272, 235, 12 }, "Effective mass (m*/m0)", mStar, 0.05f, 1.5f, 0.01f, "%.2f");
        cloudChanged |= DrawIntSlider({ 30, 320, 235, 12 }, "Sample count", targetDensity, 2000, 50000, 2000);
        if (cloudChanged) respawn();

        const char* orbitalLetters[] = { "s", "p", "d", "f", "g" };
        const bool isVacuum = (eps_r < 1.05f && std::abs(mStar - 1.0f) < 0.01f);
        const bool isSilicon = (std::abs(eps_r - SI_EPS) < 0.05f && std::abs(mStar - SI_MSTAR) < 0.005f);
        const char* mediumTag = isVacuum ? "Hydrogen (vacuum)" : (isSilicon ? "Si donor model" : "Custom medium");

        const double energyEv = QuantumMath::bindingEnergyEv(q_n, eps_r, mStar);
        const CloudStats& st = ps.stats;

        DrawText(TextFormat("Orbital: %d%s (%s, m = %d) | %s", q_n, orbitalLetters[q_l],
                            realOrbital ? "real" : "complex", q_m, mediumTag),
                 30, 348, 14, Color{ 100, 210, 255, 255 });

        if (std::abs(energyEv) >= 1.0) DrawText(TextFormat("Binding energy E_n: %.3f eV", energyEv), 30, 368, 14, Color{ 110, 235, 175, 255 });
        else                           DrawText(TextFormat("Binding energy E_n: %.2f meV", energyEv * 1000.0), 30, 368, 14, Color{ 110, 235, 175, 255 });

        DrawText(TextFormat("Effective Bohr radius a*: %.3f nm", aStarNm), 30, 388, 14, Color{ 110, 235, 175, 255 });

        if (isSilicon && q_n == 1)
            DrawText("Measured P donor in Si: ~45 meV (central-cell corr.)", 30, 408, 13, Color{ 255, 190, 90, 255 });
        else
            DrawText("Hydrogenic model: E ~ m*/eps_r^2,  a* ~ eps_r/m*", 30, 408, 13, GRAY);

        double sigmaOff = (st.stdErrR > 0.0) ? std::abs(st.meanR - st.theoryMeanR) / st.stdErrR : 0.0;
        DrawText(TextFormat("<r> sampled: %.3f +/- %.3f a*   exact: %.3f a*", st.meanR, st.stdErrR, st.theoryMeanR),
                 30, 432, 13, RAYWHITE);
        DrawText(TextFormat("Deviation: %.1f sigma | Radial nodes: %d | Norm: %.4f", sigmaOff, st.radialNodes, st.normIntegral),
                 30, 450, 13, (sigmaOff < 3.0) ? Color{ 110, 235, 175, 255 } : Color{ 255, 120, 120, 255 });

        DrawLine(25, 474, 395, 474, Color{ 60, 65, 80, 255 });
        DrawText("LMB drag: rotate | Wheel: zoom", 28, 484, 13, SKYBLUE);
        DrawText(TextFormat("RMB drag: point-charge probe, classical illustration [Q: %+.0f]", perturber.charge), 28, 502, 12, YELLOW);
        DrawText(realOrbital ? "[O] Real orbital: no probability current (static)"
                             : (q_m != 0 ? "[O] Complex orbital: flowing along probability current"
                                         : "[O] Complex orbital, m = 0: no probability current"),
                 28, 520, 13, GREEN);
        DrawText("[S] Hydrogen <-> Si donor (eps_r 11.7, m* 0.26)", 28, 538, 13, Color{ 110, 235, 175, 255 });
        DrawText("[Space] Flip probe | [P] Pause | [R] Reset", 28, 556, 13, LIGHTGRAY);

        DrawRadialHistogram(histPanel, st);

        // ----------------------------------------------------------------
        // Right panel: data structures and timings
        // ----------------------------------------------------------------
        DrawRectangleRec(dsPanel, Fade(BLACK, 0.82f));
        DrawRectangleLinesEx(dsPanel, 1.0f, Color{ 70, 75, 95, 255 });
        const int dx = static_cast<int>(dsPanel.x) + 12;
        const int dy = static_cast<int>(dsPanel.y);
        const Color dsColor = Color{ 200, 170, 255, 255 };

        DrawText("DATA STRUCTURES & TIMING", dx, dy + 10, 13, LIGHTGRAY);
        DrawText(TextFormat("Cloud %s in %.1f ms (inverse CDF: prefix sums + binary search)",
                            lastBuildWasHit ? "loaded" : "built", lastBuildMs), dx, dy + 30, 12, dsColor);
        DrawText(TextFormat("LRU cache (hash map + linked list): %zu/%zu stored | %zu hits, %zu misses",
                            cache.size(), cache.capacity(), cache.hits, cache.misses), dx, dy + 48, 12, dsColor);
        if (renderer.glow) {
            DrawText("Depth sort: not needed (additive glow is order-independent)", dx, dy + 66, 12, dsColor);
        } else {
            DrawText(TextFormat("[K] Depth sort: %s | %.2f ms",
                                renderer.sortMode == CloudRenderer::SortMode::Counting ? "counting sort O(N+B)"
                                                                                       : "std::sort O(N log N)",
                                renderer.sortMs), dx, dy + 66, 12, dsColor);
        }
        DrawText(TextFormat("Physics %.2f ms | Project %.2f ms | Submit %.2f ms",
                            physicsMs, renderer.projectMs, renderer.submitMs), dx, dy + 84, 12, RAYWHITE);
        DrawText("Color = local density from a 40^3 uniform grid", dx, dy + 102, 12, GRAY);
        DrawText(TextFormat("[G] Additive glow: %s", renderer.glow ? "on" : "off"), dx, dy + 118, 12, LIGHTGRAY);

        EndDrawing();
    }

    renderer.shutdown();
    CloseWindow();
    return 0;
}
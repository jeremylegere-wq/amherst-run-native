// Unit tests for src/core/math.h (no test framework: a failed check prints and returns 1).
#include <cmath>
#include <cstdio>

#include "core/math.h"

static int failures = 0;
#define CHECK_NEAR(a, b, tol)                                                                     \
    do {                                                                                          \
        double va = (a), vb = (b);                                                                \
        if (std::fabs(va - vb) > (tol)) {                                                         \
            std::printf("%s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, va, vb);          \
            ++failures;                                                                           \
        }                                                                                         \
    } while (0)

int main() {
    using namespace amr;
    // the camera looks along +x from the origin, z up: a point straight ahead lands in the middle of the screen
    Mat4 vp = perspective(1.0f, 16.0f / 9.0f, 0.5f, 400.0f) * look_at({0, 0, 0}, {1, 0, 0});
    Vec3 ahead = transform_point(vp, {10, 0, 0});
    CHECK_NEAR(ahead.x, 0, 1e-5);
    CHECK_NEAR(ahead.y, 0, 1e-5);
    // north (+y) is to the left when facing east; up (+z) is up on screen
    CHECK_NEAR(transform_point(vp, {10, 2, 0}).x < 0, 1, 0);
    CHECK_NEAR(transform_point(vp, {10, 0, 2}).y > 0, 1, 0);
    // near and far planes map to -1 and 1
    CHECK_NEAR(transform_point(vp, {0.5f, 0, 0}).z, -1, 1e-4);
    CHECK_NEAR(transform_point(vp, {400, 0, 0}).z, 1, 1e-4);
    // a quarter turn takes east to north; translate then rotate composes as expected
    Vec3 n = transform_point(rotate_z(1.5707963f), {1, 0, 0});
    CHECK_NEAR(n.x, 0, 1e-6);
    CHECK_NEAR(n.y, 1, 1e-6);
    Vec3 p = transform_point(translate({5, 0, 0}) * rotate_z(3.1415927f), {1, 0, 0});
    CHECK_NEAR(p.x, 4, 1e-5);
    if (failures == 0) std::printf("math: all checks passed\n");
    return failures ? 1 : 0;
}

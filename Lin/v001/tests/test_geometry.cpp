// File: tests/test_geometry.cpp
// Purpose: Test 1, at run time: geometry at INT32_MIN and INT32_MAX. The same edge cases are also
//          proven at compile time in test_constexpr.cpp; this checks the code as actually built.
#include <cstdint>
#include <limits>

#include "check.hpp"
#include "jCppGUI/v001/geometry.hpp"

int main() {
    using namespace jgui;
    jgui_test::Checker check;
    // volatile, so the compiler cannot fold these and the instructions really run
    volatile std::int32_t max_value = std::numeric_limits<std::int32_t>::max();
    volatile std::int32_t min_value = std::numeric_limits<std::int32_t>::min();
    const std::int32_t hi = max_value;
    const std::int32_t lo = min_value;

    check(sat_add(hi, 1) == hi, "add saturates high");
    check(sat_add(lo, -1) == lo, "add saturates low");
    check(sat_sub(lo, 1) == lo, "sub saturates low");
    check(sat_sub(hi, -1) == hi, "sub saturates high");
    check(sat_mul(hi, hi) == hi, "mul saturates high");
    check(sat_mul(lo, hi) == lo, "mul saturates low");
    check((Point{hi, lo} + Point{hi, lo}) == Point{hi, lo}, "point add saturates");
    check(Size{lo, lo} == Size{}, "size is never negative");
    check(Rect{hi, hi, hi, hi}.right() == hi, "right saturates");
    check(Rect{hi - 1, 0, 10, 10}.contains(Point{hi, 5}), "contains at the top limit");
    check(!Rect{lo, lo, hi, hi}.contains(Point{-1, -1}), "contains stops short of far edge");
    check(Rect{lo, lo, hi, hi}.contains(Point{lo, lo}), "contains at the bottom limit");
    check(Rect{hi, hi, hi, hi}.intersect(Rect{lo, lo, hi, hi}).empty(), "far apart do not intersect");
    check(Rect{lo, lo, hi, hi}.intersect(Rect{lo, lo, hi, hi}) == Rect{lo, lo, hi, hi}, "self intersect");
    check(Rect{hi - 2, 0, hi, 4}.intersect(Rect{0, 0, hi, 4}) == Rect{hi - 2, 0, 2, 4}, "overlap at the limit");
    return check.finish();
}

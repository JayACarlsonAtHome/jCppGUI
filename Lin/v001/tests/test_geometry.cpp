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
    check(Left{hi} + Width{hi} == Right{hi}, "an edge plus a length saturates");
    check(Right{lo} - Width{hi} == Left{lo}, "an edge minus a length saturates");
    check(Right{hi} - Left{lo} == Width{hi}, "the length between two edges saturates");
    check(Right{lo} - Left{hi} == Width{}, "and is never negative");
    check(Size{Width{lo}, Height{lo}} == Size{}, "size is never negative");
    check(Rect{Point{Left{hi}, Top{hi}}, Size{Width{hi}, Height{hi}}}.right() == Right{hi}, "right saturates");
    check(Rect{Point{Left{hi - 1}, Top{0}}, Size{Width{10}, Height{10}}}.contains(Point{Left{hi}, Top{5}}), "contains at the top limit");
    check(!Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}}.contains(Point{Left{-1}, Top{-1}}), "contains stops short of far edge");
    check(Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}}.contains(Point{Left{lo}, Top{lo}}), "contains at the bottom limit");
    check(Rect{Point{Left{hi}, Top{hi}}, Size{Width{hi}, Height{hi}}}.intersect(Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}}).empty(), "far apart do not intersect");
    check(Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}}.intersect(Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}}) == Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}}, "self intersect");
    check(Rect{Point{Left{hi - 2}, Top{0}}, Size{Width{hi}, Height{4}}}.intersect(Rect{Point{Left{0}, Top{0}}, Size{Width{hi}, Height{4}}}) == Rect{Point{Left{hi - 2}, Top{0}}, Size{Width{2}, Height{4}}}, "overlap at the limit");
    return check.finish();
}

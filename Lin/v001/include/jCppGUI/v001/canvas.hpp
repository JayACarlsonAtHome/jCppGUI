// File: include/jCppGUI/v001/canvas.hpp
// Purpose: PixelBuffer, a validated view of ARGB8888 pixels, and Canvas, whose drawing calls are
//          clipped so that no input can write outside the buffer. Neither can be copied or moved, so a widget
//          cannot store what it was given, and a canvas for a frame refuses to draw once that frame is
//          over. Physical pixels, scale 1.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

#include "error.hpp"
#include "geometry.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

inline constexpr std::int32_t max_buffer_side = 8192;  // 256 MiB at most, for one buffer of this many pixels a side

struct Color {
    std::uint32_t argb{};
    friend constexpr bool operator==(const Color&, const Color&) = default;
};

namespace detail {
// Which frame is the current one. A new frame invalidates every older one, even if it then fails, and ending the
// current frame invalidates it too. Generation 0 is never current. Both backends count their frames with this
// rule, and the application counts what it is painting.
class FrameLedger {
public:
    [[nodiscard]] constexpr std::uint64_t begin() noexcept { return ++latest_; }
    [[nodiscard]] constexpr bool current(std::uint64_t generation) const noexcept {
        return generation != 0 && generation == latest_;
    }
    constexpr void finish() noexcept { ++latest_; }

private:
    std::uint64_t latest_{};
};
}  // namespace detail

class PixelBuffer {
    class Validated {
        friend class PixelBuffer;
        friend class Canvas;
        constexpr Validated() noexcept = default;
    };

public:
    // The byte count for a size, after checking its limit. Every size from outside goes through
    // here before it is used in any other arithmetic.
    [[nodiscard]] static constexpr std::expected<std::size_t, Error> byte_size(Size size) {
        if (size.width().value_ > max_buffer_side || size.height().value_ > max_buffer_side)
            return make_error(ErrorCode::SizeLimit, "buffer side is over the limit of 8192");
        return static_cast<std::size_t>(size.width().value_) * static_cast<std::size_t>(size.height().value_) * 4U;
    }

    // stride_bytes is the distance between rows: a whole number of pixels, at least one row wide.
    [[nodiscard]] static constexpr std::expected<PixelBuffer, Error> create(std::span<std::uint32_t> pixels, Size size,
                                                                  std::int32_t stride_bytes) {
        if (auto bytes = byte_size(size); !bytes) return std::unexpected(std::move(bytes.error()));
        if (stride_bytes % 4 != 0 || stride_bytes / 4 < size.width().value_)
            return make_error(ErrorCode::SizeLimit, "buffer stride does not fit the width");
        const auto stride_pixels = static_cast<std::size_t>(stride_bytes / 4);
        const auto rows = static_cast<std::size_t>(size.height().value_);
        if (rows != 0 && stride_pixels > (std::size_t{1} << 40U) / rows)
            return make_error(ErrorCode::SizeLimit, "buffer stride is over the limit");
        if (pixels.size() < stride_pixels * rows)
            return make_error(ErrorCode::SizeLimit, "buffer storage is smaller than its size");
        return std::expected<PixelBuffer, Error>{std::in_place, Validated{}, pixels, size, stride_bytes};
    }

    // Only create can make what this takes, so every buffer has passed its checks.
    constexpr PixelBuffer(Validated, std::span<std::uint32_t> pixels, Size size, std::int32_t stride_bytes) noexcept
        : pixels_{pixels}, size_{size}, stride_bytes_{stride_bytes} {}
    PixelBuffer(const PixelBuffer&) = delete;
    PixelBuffer& operator=(const PixelBuffer&) = delete;
    constexpr ~PixelBuffer() = default;

    // The distance between rows when rows follow each other with no gap.
    [[nodiscard]] static constexpr std::int32_t tight_stride(Size size) noexcept {
        return saturate_to<std::int32_t>(std::int64_t{size.width().value_} * 4);
    }

    // Where a point is, in pixels from the start, when rows follow each other with no gap.
    [[nodiscard]] static constexpr std::size_t tight_index(Point at, Size size) noexcept {
        return static_cast<std::size_t>(at.top().value_) * static_cast<std::size_t>(size.width().value_) + static_cast<std::size_t>(at.left().value_);
    }

    [[nodiscard]] constexpr Size size() const noexcept { return size_; }

private:
    friend class Canvas;

    // One pixel, at a position the caller has clipped to size().
    [[nodiscard]] constexpr std::uint32_t& at(std::int32_t x, std::int32_t y) const {
        const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride_bytes_ / 4);
        return pixels_[row + static_cast<std::size_t>(x)];
    }

    std::span<std::uint32_t> pixels_;
    Size size_;
    std::int32_t stride_bytes_;
};

class Canvas {
public:
    // A canvas on memory the caller owns for as long as it draws.
    constexpr explicit Canvas(const PixelBuffer& buffer) noexcept
        : buffer_{PixelBuffer::Validated{}, buffer.pixels_, buffer.size_, buffer.stride_bytes_}, clip_{Point{}, buffer.size_} {}

    // A canvas for one frame: from when that generation ends, every draw call on it aborts.
    constexpr Canvas(const PixelBuffer& buffer, const detail::FrameLedger& ledger, std::uint64_t generation) noexcept
        : buffer_{PixelBuffer::Validated{}, buffer.pixels_, buffer.size_, buffer.stride_bytes_},
          clip_{Point{}, buffer.size_},
          ledger_{&ledger},
          generation_{generation} {}

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // Leaves nothing behind to draw with, for a pointer kept past the end of its life.
    constexpr ~Canvas() {
        buffer_.pixels_ = {};
        clip_ = Rect{};
        generation_ = 0;
    }

    // A canvas on the same pixels with a clip that can only be narrower than this one's, for the same frame. The
    // result is a new object and not a copy, so it can be used only where it is made.
    [[nodiscard]] constexpr Canvas clipped(const Rect& area) const noexcept { return Canvas{*this, clip_.intersect(area)}; }

    constexpr void fill_rect(const Rect& area, Color color) const {
        require_current();
        const Rect visible = clip_.intersect(area);
        if (visible.empty()) return;
        for (std::int32_t y = visible.top().value_; y < visible.bottom().value_; ++y)
            for (std::int32_t x = visible.left().value_; x < visible.right().value_; ++x)
                buffer_.at(x, y) = color.argb;
    }

    // A one-pixel outline just inside the rectangle.
    constexpr void stroke_rect(const Rect& area, Color color) const {
        if (area.empty()) return;
        constexpr Width one_across{1};
        constexpr Height one_down{1};
        fill_rect(Rect{area.origin(), Size{area.width(), one_down}}, color);
        fill_rect(Rect{Point{area.left(), area.bottom() - one_down}, Size{area.width(), one_down}}, color);
        fill_rect(Rect{area.origin(), Size{one_across, area.height()}}, color);
        fill_rect(Rect{Point{area.right() - one_across, area.top()}, Size{one_across, area.height()}}, color);
    }

    // Blends a shaped line of text with its top-left corner at the given point.
    constexpr void draw_text(Point at, const TextRun& run, Color color) const {
        require_current();
        const Rect visible = clip_.intersect(Rect{at, run.size()});
        if (visible.empty()) return;
        for (std::int32_t y = visible.top().value_; y < visible.bottom().value_; ++y) {
            for (std::int32_t x = visible.left().value_; x < visible.right().value_; ++x) {
                const std::uint8_t coverage = run.coverage_at(Point{Left{sat_sub(x, at.left().value_)}, Top{sat_sub(y, at.top().value_)}});
                if (coverage != 0) buffer_.at(x, y) = blend(buffer_.at(x, y), color.argb, coverage);
            }
        }
    }

private:
    constexpr Canvas(const Canvas& parent, const Rect& clip) noexcept
        : buffer_{PixelBuffer::Validated{}, parent.buffer_.pixels_, parent.buffer_.size_, parent.buffer_.stride_bytes_},
          clip_{clip},
          ledger_{parent.ledger_},
          generation_{parent.generation_} {}

    // Checked on each call, because a canvas can still be made to outlive its frame: see docs/SECURITY.md.
    constexpr void require_current() const noexcept {
        require(ledger_ == nullptr || ledger_->current(generation_), "a Canvas used after its frame ended");
    }

    [[nodiscard]] static constexpr std::uint32_t blend(std::uint32_t under, std::uint32_t over,
                                                       std::uint32_t coverage) noexcept {
        std::uint32_t mixed = 0xFF000000U;
        for (const unsigned shift : {0U, 8U, 16U}) {
            const std::uint32_t a = (under >> shift) & 0xFFU;
            const std::uint32_t b = (over >> shift) & 0xFFU;
            mixed |= ((a * (255U - coverage) + b * coverage + 127U) / 255U) << shift;
        }
        return mixed;
    }

    PixelBuffer buffer_;
    Rect clip_;
    const detail::FrameLedger* ledger_{};
    std::uint64_t generation_{};
};

}  // namespace jgui::inline v001

#include <gtest/gtest.h>

#include <QImage>
#include <QPainter>

#include <algorithm>

#include "AgentOverlay.hpp"

using namespace ad;
using namespace ad::ui;
using namespace std::chrono_literals;

namespace
{

constexpr auto kStart = AgentOverlay::Clock::time_point{} + 1h;

struct Frame
{
    QImage                                   image;
    std::optional<std::chrono::milliseconds> next;

    bool Drawn() const
    {
        for (int y = 0; y < image.height(); ++y)
        {
            const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            if (std::any_of(row, row + image.width(),
                            [](QRgb pixel)
                            {
                                return qAlpha(pixel) > 0;
                            }))
            {
                return true;
            }
        }
        return false;
    }
};

Frame Paint(AgentOverlay &overlay, AgentOverlay::Clock::time_point now, const View &view = {})
{
    QImage image(400, 240, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    auto     next = overlay.Paint(painter, image.rect(), view, now);
    painter.end();
    return {std::move(image), next};
}

} // namespace

TEST(AgentOverlayTest, AssemblesMovesWithConfiguredDurationAndGlitchesOnIdle)
{
    AgentOverlay overlay(400ms);
    const auto   document = &overlay;
    overlay.Apply({"1", document, document, {}, kStart}, document, {}, kStart);
    EXPECT_FALSE(Paint(overlay, kStart).Drawn());

    overlay.Apply({"1", document, document, Vec2{55, 70}, kStart}, document, {}, kStart);
    const auto particles = Paint(overlay, kStart);
    EXPECT_TRUE(particles.Drawn());
    EXPECT_EQ(particles.next, 16ms);
    const auto visible = Paint(overlay, kStart + 250ms);
    EXPECT_TRUE(visible.Drawn());
    EXPECT_NE(particles.image, visible.image);
    EXPECT_EQ(visible.next, 30s - 250ms);

    const auto edit = kStart + 1s;
    overlay.Apply({"1", document, document, Vec2{245, 70}, edit}, document, {}, edit);
    EXPECT_EQ(Paint(overlay, edit).image, visible.image);
    const auto moving = Paint(overlay, edit + 180ms);
    EXPECT_EQ(moving.next, 16ms);
    const auto arrived = Paint(overlay, edit + 400ms);
    EXPECT_NE(moving.image, arrived.image);
    EXPECT_EQ(arrived.next, 30s - 400ms);
    EXPECT_NE(Paint(overlay, edit + 400ms, View{1, 40, 50}).image, arrived.image);

    EXPECT_NE(Paint(overlay, edit + 30s + 80ms).image, arrived.image);
    EXPECT_FALSE(Paint(overlay, edit + 30s + 200ms).Drawn());
    const auto resume = edit + 31s;
    overlay.Apply({"1", document, document, {}, resume}, document, {}, resume);
    EXPECT_FALSE(Paint(overlay, resume).Drawn());
    EXPECT_TRUE(Paint(overlay, resume + 80ms).Drawn());
    EXPECT_EQ(Paint(overlay, resume + 250ms).image, arrived.image);
    const auto expired = Paint(overlay, resume + 15min);
    EXPECT_FALSE(expired.Drawn());
    EXPECT_FALSE(expired.next.has_value());
}

TEST(AgentOverlayTest, PortalsOnBothCanvasesAndDisassemblesOnlyTheDepartingAgent)
{
    AgentOverlay source;
    AgentOverlay destination;
    source.Apply({"1", &source, &source, Vec2{100, 90}, kStart}, &source, {}, kStart);
    source.Apply({"2", &source, &source, Vec2{165, 115}, kStart}, &source, {}, kStart);
    Paint(source, kStart);
    const auto both = Paint(source, kStart + 250ms);

    const auto                             transfer = kStart + 1s;
    const mcp::DocumentHost::AgentActivity change{"1", &source, &destination, {}, transfer};
    source.Apply(change, &source, {100, 100}, transfer);
    destination.Apply(change, &destination, {200, 90}, transfer);
    Paint(source, transfer);
    // The portal starts on the first paint when a previously hidden tab is shown.
    const auto opened = transfer + 1s;
    Paint(destination, opened);
    const auto exit_portal  = Paint(source, transfer + 150ms);
    const auto entry_portal = Paint(destination, opened + 150ms);
    EXPECT_TRUE(exit_portal.Drawn());
    EXPECT_TRUE(entry_portal.Drawn());
    EXPECT_EQ(exit_portal.next, 16ms);
    EXPECT_EQ(entry_portal.next, 16ms);
    EXPECT_NE(exit_portal.image, both.image);
    const auto remaining = Paint(source, transfer + 300ms);
    EXPECT_TRUE(remaining.Drawn());
    EXPECT_NE(remaining.image, both.image);

    const auto leave = opened + 500ms;
    Paint(destination, leave);
    destination.Apply({"1", &destination, nullptr, {}, leave}, &destination, {}, leave);
    EXPECT_TRUE(Paint(destination, leave + 100ms).Drawn());
    EXPECT_FALSE(Paint(destination, leave + 250ms).Drawn());
    EXPECT_EQ(Paint(source, leave + 250ms).image, remaining.image);
}

TEST(AgentOverlayTest, SilentCloseDiscardsPendingPortalOnOtherTab)
{
    AgentOverlay source;
    AgentOverlay destination;
    source.Apply({"1", &source, &source, Vec2{100, 90}, kStart}, &source, {}, kStart);
    Paint(source, kStart);

    const auto                             move = kStart + 1s;
    const mcp::DocumentHost::AgentActivity transfer{"1", &source, &destination, {}, move};
    source.Apply(transfer, &source, {100, 100}, move);
    destination.Apply(transfer, &destination, {200, 90}, move);

    const auto                             close = move + 100ms;
    const mcp::DocumentHost::AgentActivity removal{"1", nullptr, nullptr, {}, close};
    source.Apply(removal, &source, {}, close);
    destination.Apply(removal, &destination, {}, close);
    EXPECT_FALSE(Paint(source, close).Drawn());
    EXPECT_FALSE(Paint(destination, close).Drawn());
}

TEST(AgentOverlayTest, BurstFinishesEachMoveBeforeManualDisassembly)
{
    AgentOverlay overlay;
    auto         edit = [&](Vec2 point, auto offset)
    {
        overlay.Apply({"1", &overlay, &overlay, point, kStart + offset}, &overlay, {}, kStart + offset);
    };
    edit({30, 75}, 0ms);
    const auto assembled_at_first_point = Paint(overlay, kStart).image;
    edit({130, 75}, 50ms);
    edit({230, 75}, 110ms);
    overlay.Apply({"1", &overlay, nullptr, {}, kStart + 110ms}, &overlay, {}, kStart + 110ms);

    AgentOverlay reference;
    auto         resting = [&](Vec2 point)
    {
        reference.Clear();
        reference.Apply({"1", &reference, &reference, point, kStart}, &reference, {}, kStart);
        Paint(reference, kStart);
        return Paint(reference, kStart + 250ms).image;
    };

    EXPECT_TRUE(Paint(overlay, kStart + 150ms).Drawn());
    EXPECT_NE(Paint(overlay, kStart + 150ms).image, assembled_at_first_point);
    EXPECT_EQ(Paint(overlay, kStart + 250ms).image, resting({30, 75}));
    EXPECT_EQ(Paint(overlay, kStart + 430ms).image, resting({130, 75}));
    EXPECT_EQ(Paint(overlay, kStart + 610ms).image, resting({230, 75}));
    EXPECT_TRUE(Paint(overlay, kStart + 730ms).Drawn());
    EXPECT_FALSE(Paint(overlay, kStart + 860ms).Drawn());
}

TEST(AgentOverlayTest, TenAgentsAdvanceTheirQueuesIndependently)
{
    AgentOverlay overlay(400ms);
    AgentOverlay reference;
    const auto   populate = [](AgentOverlay &target, int stage, auto at)
    {
        for (int index = 0; index < 10; ++index)
        {
            const bool   even = index % 2 == 0;
            const double x    = stage == 0 ? 30 : (stage == 1 || !even ? 130 : 230);
            target.Apply({std::to_string(index + 1), &target, &target, Vec2{x, 15.0 + index * 20.0}, at}, &target, {}, at);
        }
    };
    populate(overlay, 0, kStart);
    Paint(overlay, kStart);
    populate(overlay, 1, kStart + 50ms);
    for (int index = 0; index < 10; index += 2)
    {
        const auto now = kStart + 110ms;
        overlay.Apply({std::to_string(index + 1), &overlay, &overlay, Vec2{230, 15.0 + index * 20.0}, now}, &overlay, {}, now);
    }

    populate(reference, 1, kStart);
    Paint(reference, kStart);
    EXPECT_EQ(Paint(overlay, kStart + 650ms).image, Paint(reference, kStart + 250ms).image);
    reference.Clear();
    populate(reference, 2, kStart);
    Paint(reference, kStart);
    EXPECT_EQ(Paint(overlay, kStart + 1050ms).image, Paint(reference, kStart + 250ms).image);
}

TEST(AgentOverlayTest, TransferQueuesPortalBehindSourceMoveAndBeforeDestinationMove)
{
    AgentOverlay source;
    AgentOverlay destination;
    source.Apply({"1", &source, &source, Vec2{40, 80}, kStart}, &source, {}, kStart);
    Paint(source, kStart);
    const auto move = kStart + 300ms;
    source.Apply({"1", &source, &source, Vec2{140, 80}, move}, &source, {}, move);
    const auto                             transfer = move + 50ms;
    const auto                             before   = Paint(source, transfer).image;
    const mcp::DocumentHost::AgentActivity change{"1", &source, &destination, {}, transfer};
    source.Apply(change, &source, {}, transfer);
    destination.Apply(change, &destination, {50, 80}, transfer);
    EXPECT_EQ(Paint(source, transfer).image, before);
    Paint(destination, transfer);
    destination.Apply({"1", &destination, &destination, Vec2{220, 80}, transfer + 20ms}, &destination, {}, transfer + 20ms);
    EXPECT_TRUE(Paint(source, move + 479ms).Drawn());
    EXPECT_FALSE(Paint(source, move + 480ms).Drawn());

    AgentOverlay reference;
    reference.Apply({"1", &reference, &reference, Vec2{50, 80}, kStart}, &reference, {}, kStart);
    Paint(reference, kStart);
    EXPECT_EQ(Paint(destination, transfer + 300ms).image, Paint(reference, kStart + 250ms).image);
    EXPECT_NE(Paint(destination, transfer + 400ms).image, Paint(reference, kStart + 250ms).image);
}

TEST(AgentOverlayTest, FourPaletteDragsReprojectWithLivePanelAndCanvasGeometry)
{
    AgentOverlay overlay;
    using Anchor = AgentOverlay::Anchor;
    using Route  = mcp::DocumentHost::AgentRoute;
    for (int i = 0; i < 4; ++i)
    {
        overlay.Apply({std::to_string(i + 1), &overlay, &overlay, Vec2{200, 45.0 + i * 45}, kStart, Route::CreateNode, "service"}, &overlay,
                      {}, kStart);
    }
    View   view;
    double panel_x = 30;
    auto   paint   = [&](auto elapsed)
    {
        QImage image(500, 430, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        overlay.PaintProjected(
            painter, image.rect(),
            [&](const AgentOverlay::Target &target)
            {
                if (target.anchor == Anchor::Palette)
                {
                    EXPECT_EQ(target.element_type, "service");
                    return QPointF(panel_x, target.world.y());
                }
                return QPointF(target.world.x() * view.zoom + view.pan_x, target.world.y() * view.zoom + view.pan_y);
            },
            kStart + elapsed);
        return image;
    };

    paint(0ms);
    const auto grab = paint(300ms);
    // Rings reach to the left of the palette anchor, where an ordinary cursor has no pixels.
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_GT(grab.pixelColor(20, 45 + i * 45).alpha(), 0);
    }

    const auto before = paint(440ms); // halfway through the 180 ms carry
    view              = View{1.5, 40, 23};
    panel_x           = 60;
    const auto after  = paint(440ms);
    for (int i = 0; i < 4; ++i)
    {
        const int y = 45 + i * 45;
        EXPECT_GT(before.pixelColor(180, y + 10).alpha(), 0);
        const int projected_y = static_cast<int>(y + (y * 0.5 + 23) * 0.875);
        EXPECT_GT(after.pixelColor(309, projected_y + 10).alpha(), 0);
    }
    const auto drop    = paint(580ms);
    const auto settled = paint(630ms);
    for (int i = 0; i < 4; ++i)
    {
        const int y = static_cast<int>((45 + i * 45) * 1.5 + 23);
        EXPECT_GT(drop.pixelColor(330, y).alpha(), 0);
        EXPECT_EQ(settled.pixelColor(330, y).alpha(), 0);
    }
}

TEST(AgentOverlayTest, LinkRouteClicksCanvasThenBothTimelineControls)
{
    AgentOverlay overlay;
    using Anchor = AgentOverlay::Anchor;
    using Route  = mcp::DocumentHost::AgentRoute;
    overlay.Apply({"1", &overlay, &overlay, Vec2{70, 40}, kStart, Route::AddLinkStep}, &overlay, {}, kStart);
    auto paint = [&](auto elapsed)
    {
        QImage image(400, 250, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        overlay.PaintProjected(
            painter, image.rect(),
            [](const AgentOverlay::Target &target)
            {
                if (target.anchor == Anchor::StepType)
                {
                    return QPointF(170, 120);
                }
                if (target.anchor == Anchor::AddStep)
                {
                    return QPointF(280, 120);
                }
                return target.world;
            },
            kStart + elapsed);
        return image;
    };
    paint(0ms);
    EXPECT_GT(paint(300ms).pixelColor(60, 40).alpha(), 0);
    EXPECT_GT(paint(580ms).pixelColor(160, 120).alpha(), 0);
    EXPECT_GT(paint(860ms).pixelColor(270, 120).alpha(), 0);
    EXPECT_EQ(paint(910ms).pixelColor(270, 120).alpha(), 0);
}

TEST(AgentOverlayTest, SharedPaletteShowsFourConcurrentGrabsAndRevealsOnlyTheNodesBeingPlaced)
{
    using Route = mcp::DocumentHost::AgentRoute;
    AgentOverlay overlay;
    const auto   queue = [&](int i, int batch)
    {
        const std::string id = std::to_string(i + 1);
        const auto        at = kStart + std::chrono::milliseconds(batch * 40 + i * 2);
        overlay.Apply({id, &overlay, &overlay, Vec2{180.0 + i * 50, 100.0 + batch * 65}, at, Route::CreateNode, "service",
                       "node-" + id + "-" + std::to_string(batch)},
                      &overlay, {}, at);
    };
    for (int batch = 0; batch < 2; ++batch)
    {
        for (int i = 0; i < 4; ++i)
        {
            queue(i, batch);
        }
    }
    const auto paint = [&](auto offset)
    {
        QImage image(450, 300, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        overlay.PaintProjected(
            painter, image.rect(),
            [](const AgentOverlay::Target &anchor)
            {
                return anchor.anchor == AgentOverlay::Anchor::Palette ? QPointF(90, 70) : anchor.world;
            },
            kStart + 100ms + offset);
        return image;
    };
    paint(0ms);
    const auto grab = paint(300ms);
    for (const int x : {58, 79, 100, 121})
    {
        EXPECT_GT(grab.pixelColor(x, 60).alpha(), 0);
    }
    paint(440ms);
    EXPECT_EQ(overlay.PendingNodes().size(), 8);
    const auto first_drop = paint(580ms);
    EXPECT_EQ(overlay.PendingNodes().size(), 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(overlay.PendingNodes().contains("node-" + std::to_string(i + 1) + "-1"));
        EXPECT_GT(first_drop.pixelColor(170 + i * 50, 100).alpha(), 0);
    }
    paint(1100ms);
    EXPECT_TRUE(overlay.PendingNodes().empty());
}

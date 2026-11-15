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

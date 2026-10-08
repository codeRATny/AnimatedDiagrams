#include <gtest/gtest.h>

#include "Import/DrawioImporter.hpp"

using namespace ad;

namespace
{

// Plain (uncompressed) two-page file with a group, an edge label cell, a dangling edge and free text.
constexpr std::string_view kPlain = R"(<mxfile host="app.diagrams.net">
  <diagram id="p1" name="Архитектура">
    <mxGraphModel dx="800" dy="600" grid="1">
      <root>
        <mxCell id="0"/>
        <mxCell id="1" parent="0"/>
        <mxCell id="api" value="API&lt;br&gt;gateway" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;" vertex="1" parent="1">
          <mxGeometry x="40" y="80" width="120" height="60" as="geometry"/>
        </mxCell>
        <mxCell id="grp" value="" style="group" vertex="1" connectable="0" parent="1">
          <mxGeometry x="300" y="40" width="200" height="200" as="geometry"/>
        </mxCell>
        <mxCell id="db" value="Orders DB" style="shape=cylinder3;whiteSpace=wrap;html=1;dashed=1;" vertex="1" parent="grp">
          <mxGeometry x="20" y="30" width="80" height="80" as="geometry"/>
        </mxCell>
        <object label="Decide" id="dec">
          <mxCell style="rhombus;whiteSpace=wrap;html=1;" vertex="1" parent="1">
            <mxGeometry x="40" y="240" width="100" height="80" as="geometry"/>
          </mxCell>
        </object>
        <mxCell id="e1" style="edgeStyle=orthogonalEdgeStyle;endArrow=open;startArrow=oval;strokeColor=#ff0000;" edge="1" parent="1" source="api" target="db">
          <mxGeometry relative="1" as="geometry">
            <Array as="points"><mxPoint x="230" y="110"/></Array>
          </mxGeometry>
        </mxCell>
        <mxCell id="lbl" value="SQL" style="edgeLabel;html=1;" vertex="1" connectable="0" parent="e1">
          <mxGeometry x="-0.1" relative="1" as="geometry"/>
        </mxCell>
        <mxCell id="e2" value="dangling" edge="1" parent="1" source="api">
          <mxGeometry relative="1" as="geometry"><mxPoint x="10" y="10" as="targetPoint"/></mxGeometry>
        </mxCell>
        <mxCell id="t" value="Free text" style="text;html=1;" vertex="1" parent="1">
          <mxGeometry x="500" y="300" width="80" height="20" as="geometry"/>
        </mxCell>
      </root>
    </mxGraphModel>
  </diagram>
  <diagram id="p2" name="Second"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel></diagram>
</mxfile>)";

// Compressed page (deflate + base64 + URI encoding), produced by Python zlib.
constexpr std::string_view kCompressed =
    R"(<mxfile><diagram name="Packed">rVPLboMwEPwa3w1WK6480lzSQ5UvcPEGIxmMFpPA39fYTigi6UPKAbQ7u7PrGTBheTPukXfyXQtQhO0Iy1Fr46NmzEEpEtNaEFaQOKb2IfHbg2rkqrTjCK35C4F7wpmrATxCCkrSaH5nL6SISEJd7PAscYjdQlNP7M2kAhH10AqY50aEZadaqVwrja7IBIfkVFo87AM0MD48s4PCgfegGzA42ZYrwUuik0+DQnqphZGh44pJqCsZhr4GjPc+r26DF39sECy6b9fnxq4i2xjRS97NYTmp2jqC7DmyGf1Vd3JHdvIE2bCRffw4bHSDqOAYUo1G6kq3XO0WNINWpIj6Mjd00N58mZk/u2IX6QFLWP21hmMF5tuX2XqHoLipz+vp/7DCpstldLXVXf0C</diagram></mxfile>)";

} // namespace

TEST(DrawioImporterTest, StyleParser)
{
    const auto s = ParseDrawioStyle("ellipse;whiteSpace=wrap;fillColor=#fff;;rounded=1");
    EXPECT_EQ(s.at("shape"), "ellipse");
    EXPECT_EQ(s.at("fillColor"), "#fff");
    EXPECT_EQ(s.at("rounded"), "1");
    EXPECT_TRUE(ParseDrawioStyle("").empty());
}

TEST(DrawioImporterTest, PageNames) { EXPECT_EQ(DrawioPageNames(kPlain), (std::vector<std::string>{"Архитектура", "Second"})); }

TEST(DrawioImporterTest, ImportsPlainPage)
{
    DrawioImportReport rep;
    const auto         m = ImportDrawio(kPlain, {}, &rep);
    ASSERT_TRUE(m.has_value()) << m.error();
    EXPECT_EQ(m->meta.name, "Архитектура");
    EXPECT_EQ(rep.nodes, 3);
    EXPECT_EQ(rep.edges, 1);
    EXPECT_EQ(rep.notes, 1);
    EXPECT_GE(rep.skipped, 2); // group + dangling edge

    const Node *api = m->FindNode("api");
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->label, "API");
    EXPECT_EQ(api->subtitle, "gateway");
    EXPECT_EQ(api->style.shape, "rounded");
    EXPECT_EQ(api->style.fill, "#dae8fc");
    EXPECT_EQ(api->style.text_color, "#1f2937"); // light fill keeps dark text

    const Node *db = m->FindNode("db");
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(db->type, "db");
    EXPECT_DOUBLE_EQ(db->x, 320); // group offset 300 + 20
    EXPECT_DOUBLE_EQ(db->y, 70);
    EXPECT_EQ(db->style.stroke_style, "dashed");

    EXPECT_EQ(m->FindNode("dec")->type, "decision");
    EXPECT_EQ(m->FindNode("dec")->label, "Decide");

    const Edge *e = m->FindEdge("e1");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->label, "SQL");
    EXPECT_EQ(e->style.routing, "orthogonal");
    EXPECT_EQ(e->style.arrow_end, "open");
    EXPECT_EQ(e->style.arrow_start, "circle");
    EXPECT_EQ(e->style.color, "#ff0000");
    ASSERT_EQ(e->waypoints.size(), 1U);
    EXPECT_EQ(e->waypoints[0], (Vec2{230, 110}));

    ASSERT_EQ(m->scenario.steps.size(), 1U);
    EXPECT_EQ(m->scenario.steps[0].type, StepType::Note);
    EXPECT_EQ(m->scenario.steps[0].text, "Free text");
}

TEST(DrawioImporterTest, ImportsCompressedPage)
{
    const auto m = ImportDrawio(kCompressed);
    ASSERT_TRUE(m.has_value()) << m.error();
    ASSERT_EQ(m->nodes.size(), 2U);
    EXPECT_EQ(m->FindNode("a")->label, "Сервис A");
    EXPECT_EQ(m->FindNode("b")->type, "db");
    EXPECT_EQ(m->FindEdge("e")->label, "SQL");
}

TEST(DrawioImporterTest, OptionsAndErrors)
{
    DrawioImportOptions opt;
    opt.keep_colors = false;
    const auto m    = ImportDrawio(kPlain, opt);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->FindNode("api")->style.fill.has_value());

    opt.page = 1;
    EXPECT_TRUE(ImportDrawio(kPlain, opt)->nodes.empty());
    opt.page = 5;
    EXPECT_FALSE(ImportDrawio(kPlain, opt).has_value());
    EXPECT_FALSE(ImportDrawio("<svg/>").has_value());
    EXPECT_FALSE(ImportDrawio("not xml").has_value());
    EXPECT_FALSE(ImportDrawio("<mxfile><diagram>!!!</diagram></mxfile>").has_value());
}

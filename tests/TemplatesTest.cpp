#include <gtest/gtest.h>

#include "Common/Exceptions.hpp"
#include "Engine/Templates.hpp"
#include "Model/Sample.hpp"

using namespace ad;

TEST(TemplatesTest, ApplyMapsRolesAndEdges)
{
    Document                 d(SampleModel());
    const AnimationTemplate &tpl = *BuiltinLibrary().Animation("request-response");
    const auto               ids = ApplyTemplate(d, tpl, {{"client", "svcA"}, {"server", "svcC"}}, 20000);
    ASSERT_EQ(ids.size(), tpl.steps.size());
    const Step *first = d.Get().FindStep(ids[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->from, "svcA");
    EXPECT_EQ(first->to, "svcC");
    EXPECT_EQ(first->edge_id, "eAC");
    EXPECT_DOUBLE_EQ(first->start, 20000);
    EXPECT_GT(d.Get().scenario.duration, 20000);
    ASSERT_TRUE(d.Undo()); // one history entry
    EXPECT_EQ(d.Get().FindStep(ids[0]), nullptr);
}

TEST(TemplatesTest, UnmappedRoleThrows)
{
    Document d(SampleModel());
    EXPECT_THROW(ApplyTemplate(d, *BuiltinLibrary().Animation("request-response"), {{"client", "svcA"}}, 0), InvalidArgument);
    EXPECT_THROW(ApplyTemplate(d, *BuiltinLibrary().Animation("request-response"), {{"client", "svcA"}, {"server", "ghost"}}, 0),
                 InvalidArgument);
}

TEST(TemplatesTest, MakeTemplateRoundTrip)
{
    const Model m   = SampleModel();
    const auto  ids = StepsInRange(m, 0, 2000); // s1, s2, s2b, s3
    ASSERT_FALSE(ids.empty());
    const AnimationTemplate tpl = MakeTemplate(m, ids, "my", "Мой сценарий");
    EXPECT_EQ(tpl.id, "my");
    EXPECT_EQ(tpl.steps.front().start, 0); // shifted
    ASSERT_EQ(tpl.roles.size(), 2U);
    EXPECT_EQ(tpl.roles[0].label, "Service A");

    Document    d(SampleModel());
    const auto  inserted = ApplyTemplate(d, tpl, {{"role1", "svcC"}, {"role2", "svcA"}}, 15000);
    const Step *s1       = d.Get().FindStep(inserted[0]);
    EXPECT_EQ(s1->from, "svcC");
    EXPECT_EQ(s1->to, "svcA");
}

TEST(TemplatesTest, LinkStepsUseRolePairs)
{
    Model m = SampleModel();
    Step  link;
    link.id      = "lk";
    link.type    = StepType::Link;
    link.edge_id = "eAB";
    link.start   = 100;
    m.scenario.steps.push_back(link);
    const AnimationTemplate tpl = MakeTemplate(m, {"lk"}, "t", "t");
    ASSERT_EQ(tpl.steps.size(), 1U);
    EXPECT_EQ(tpl.steps[0].edge_id, "role1>role2");

    Document   d(SampleModel());
    const auto ids = ApplyTemplate(d, tpl, {{"role1", "svcA"}, {"role2", "svcC"}}, 0);
    EXPECT_EQ(d.Get().FindStep(ids[0])->edge_id, "eAC");
    const auto none = ApplyTemplate(d, tpl, {{"role1", "svcB"}, {"role2", "svcC"}}, 0); // no edge B-C
    EXPECT_TRUE(none.empty());
}

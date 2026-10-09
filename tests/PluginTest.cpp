#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <set>

#include "Plugins/Plugin.hpp"
#include "Plugins/PluginManager.hpp"
#include "Utils/File.hpp"

using namespace ad;
namespace fs = std::filesystem;

namespace
{

constexpr std::string_view kManifest = R"({
  "format": "animated-diagrams-plugin", "formatVersion": 1,
  "id": "com.example.k8s", "name": "Kubernetes", "version": "1.2.0", "author": "me",
  "elements": [{"id": "k8s-pod", "label": "Pod", "icon": "⬡", "style": {"shape": "hexagon"}}],
  "effects": [{"id": "heartbeat", "label": "Heartbeat", "tracks": [{"property": "scale", "keys": [{"t": 0, "value": 1}, {"t": 0.5, "value": 1.1}, {"t": 1, "value": 1}]}]}],
  "animations": [{"id": "rollout", "label": "Rollout", "roles": [{"id": "svc", "label": "Service"}],
                  "steps": [{"type": "effect", "nodeId": "svc", "effect": "heartbeat", "start": 0, "duration": 500}]}]
})";

class TempDir
{
public:
    TempDir() : _path(fs::temp_directory_path() / ("ad-plugin-test-" + std::to_string(std::random_device{}())))
    {
        fs::create_directories(_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(_path, ec);
    }
    TempDir(const TempDir &)                                 = delete;
    TempDir                      &operator=(const TempDir &) = delete;
    [[nodiscard]] const fs::path &Path() const { return _path; }

private:
    fs::path _path;
};

} // namespace

TEST(PluginTest, ParseValidManifest)
{
    std::vector<std::string> warnings;
    const auto               p = ParsePlugin(kManifest, &warnings);
    ASSERT_TRUE(p.has_value()) << p.error();
    EXPECT_EQ(p->info.id, "com.example.k8s");
    EXPECT_EQ(p->info.version, "1.2.0");
    EXPECT_NE(p->library.Element("k8s-pod"), nullptr);
    EXPECT_NE(p->library.Effect("heartbeat"), nullptr);
    EXPECT_NE(p->library.Animation("rollout"), nullptr);
    EXPECT_TRUE(warnings.empty());
}

TEST(PluginTest, RejectsInvalidManifests)
{
    EXPECT_FALSE(ParsePlugin("{}").has_value());
    EXPECT_FALSE(ParsePlugin("nope").has_value());
    EXPECT_FALSE(ParsePlugin(R"({"format": "animated-diagrams-plugin", "id": "Bad Id"})").has_value());
    EXPECT_FALSE(ParsePlugin(R"({"format": "animated-diagrams-plugin", "formatVersion": 99, "id": "x"})").has_value());
    EXPECT_TRUE(IsValidPluginId("a.b-c_d9"));
    EXPECT_FALSE(IsValidPluginId("-a"));
    EXPECT_FALSE(IsValidPluginId("A"));
}

TEST(PluginTest, SerializeRoundTripAndMake)
{
    const auto p = ParsePlugin(kManifest);
    ASSERT_TRUE(p.has_value());
    const auto back = ParsePlugin(SerializePlugin(*p));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, *p);

    PluginInfo info;
    info.id           = "my.pack";
    info.name         = "Mine";
    const Plugin made = MakePlugin(info, p->library, {"k8s-pod", "rollout", "missing"});
    EXPECT_EQ(made.library.elements.size(), 1U);
    EXPECT_EQ(made.library.animations.size(), 1U);
    EXPECT_TRUE(made.library.effects.empty());
}

TEST(PluginTest, ManagerScanInstallUninstall)
{
    TempDir user;
    TempDir system;
    WriteFile(system.Path() / "bundled" / "plugin.json", std::string(kManifest));
    WriteFile(system.Path() / "broken.json", "{oops");

    PluginManager mgr;
    mgr.SetDirectories(user.Path(), {system.Path()});
    EXPECT_EQ(mgr.Scan(), 1U);
    EXPECT_EQ(mgr.Errors().size(), 1U);
    ASSERT_NE(mgr.Find("com.example.k8s"), nullptr);
    EXPECT_FALSE(mgr.Find("com.example.k8s")->writable);
    EXPECT_FALSE(mgr.Uninstall("com.example.k8s")); // bundled

    Registry reg;
    mgr.ApplyTo(reg);
    EXPECT_NE(reg.FindElement("k8s-pod"), nullptr);
    mgr.SetEnabled("com.example.k8s", false);
    mgr.ApplyTo(reg);
    EXPECT_EQ(reg.FindElement("k8s-pod"), nullptr);
    EXPECT_TRUE(mgr.DisabledIds().contains("com.example.k8s"));

    Plugin mine;
    mine.info.id   = "my.effects";
    mine.info.name = "Mine";
    EffectDef fx;
    fx.id = "zap";
    mine.library.Upsert(fx);
    const auto installed = mgr.Install(mine);
    ASSERT_TRUE(installed.has_value()) << installed.error();
    EXPECT_TRUE(fs::exists(*installed));
    ASSERT_NE(mgr.Find("my.effects"), nullptr);
    EXPECT_TRUE(mgr.Find("my.effects")->writable);
    EXPECT_TRUE(mgr.Uninstall("my.effects"));
    EXPECT_EQ(mgr.Find("my.effects"), nullptr);
}

TEST(PluginTest, InstallFromFileValidates)
{
    TempDir dir;
    WriteFile(dir.Path() / "in.json", std::string(kManifest));
    WriteFile(dir.Path() / "bad.json", "{}");
    PluginManager mgr;
    mgr.SetDirectories(dir.Path() / "user", {});
    EXPECT_TRUE(mgr.Install(dir.Path() / "in.json").has_value());
    EXPECT_FALSE(mgr.Install(dir.Path() / "bad.json").has_value());
    EXPECT_FALSE(mgr.Install(dir.Path() / "missing.json").has_value());
    EXPECT_TRUE(fs::exists(dir.Path() / "user" / "com.example.k8s.json"));
}

TEST(PluginTest, BundledPluginsAreValid)
{
    PluginManager mgr;
    mgr.SetDirectories({}, {fs::path(AD_PLUGINS_DIR)});
    ASSERT_GE(mgr.Scan(), 1U);
    EXPECT_TRUE(mgr.Errors().empty());
    for (const auto &rec : mgr.Plugins())
    {
        EXPECT_TRUE(rec.warnings.empty()) << rec.plugin.info.id << ": " << rec.warnings.front();
        EXPECT_FALSE(rec.writable);
    }
    Registry reg;
    mgr.ApplyTo(reg);
    ASSERT_NE(reg.FindAnimation("circuit-breaker"), nullptr);
    ASSERT_NE(reg.FindEffect("heartbeat"), nullptr);
    EXPECT_EQ(reg.Element("firewall").style.shape, "custom");
}

TEST(PluginTest, BundledTemplatesReferenceKnownDefinitions)
{
    PluginManager mgr;
    mgr.SetDirectories({}, {fs::path(AD_PLUGINS_DIR)});
    mgr.Scan();
    Registry reg;
    mgr.ApplyTo(reg);
    ASSERT_GE(mgr.Plugins().size(), 3U);
    for (const auto &rec : mgr.Plugins())
    {
        for (const auto &a : rec.plugin.library.animations)
        {
            std::set<std::string> roles;
            for (const auto &r : a.roles)
            {
                roles.insert(r.id);
            }
            for (const auto &s : a.steps)
            {
                if (s.type == StepType::Effect)
                {
                    EXPECT_NE(reg.FindEffect(s.effect), nullptr) << a.id << ": " << s.effect;
                }
                for (const std::string *ref : {&s.from, &s.to, &s.node_id})
                {
                    EXPECT_TRUE(ref->empty() || roles.contains(*ref)) << a.id << ": unknown role " << *ref;
                }
            }
        }
    }
    for (const char *ds : {"clickhouse", "terminal"})
    {
        EXPECT_NE(reg.FindDesignSystem(ds), nullptr) << ds;
    }
}

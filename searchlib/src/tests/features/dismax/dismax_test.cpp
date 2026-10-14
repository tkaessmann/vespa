// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/searchlib/features/dismax_feature.h>
#include <vespa/searchlib/features/setup.h>
#include <vespa/searchlib/fef/fef.h>
#include <vespa/searchlib/fef/test/dummy_dependency_handler.h>
#include <vespa/searchlib/fef/test/ftlib.h>
#include <vespa/searchlib/fef/test/indexenvironment.h>
#include <vespa/searchlib/fef/test/indexenvironmentbuilder.h>
#include <vespa/vespalib/gtest/gtest.h>

using namespace search::fef;
using namespace search::fef::test;
using namespace search::features;
using CollectionType = FieldInfo::CollectionType;

namespace {

TEST(DismaxBlueprintTest, setup_succeeds_without_parameters_and_fails_with_parameters) {
    IndexEnvironment        index_env;
    IndexEnvironmentBuilder builder(index_env);
    builder.addField(FieldType::INDEX, CollectionType::SINGLE, "foo");
    {
        DismaxBlueprint        bp;
        DummyDependencyHandler deps(bp);
        bp.setName("dismax");
        EXPECT_TRUE(((Blueprint&)bp).setup(index_env, Blueprint::StringVector()));
        EXPECT_EQ(0, deps.input.size());
        EXPECT_EQ(std::vector<std::string>{"score"}, deps.output);
    }
    {
        DismaxBlueprint        bp;
        DummyDependencyHandler deps(bp);
        bp.setName("dismax(foo)");
        EXPECT_FALSE(((Blueprint&)bp).setup(index_env, {"foo"}));
    }
}

// Fields: a (id 0, boost 10000), b (id 1, boost 10000), c (id 2, boost 1000), d (id 3, no boost).
struct DismaxFixture {
    BlueprintFactory           factory;
    FtFeatureTest              test;
    test::MatchDataBuilder::UP match_data;

    explicit DismaxFixture(bool with_boosts = true, const std::string& tie = "0.2")
        : factory(), test(factory, "dismax"), match_data() {
        setup_search_features(factory);
        auto& builder = test.getIndexEnv().getBuilder();
        builder.addField(FieldType::INDEX, CollectionType::SINGLE, "a");
        builder.addField(FieldType::INDEX, CollectionType::SINGLE, "b");
        builder.addField(FieldType::INDEX, CollectionType::SINGLE, "c");
        builder.addField(FieldType::INDEX, CollectionType::SINGLE, "d");
        auto& props = test.getIndexEnv().getProperties();
        if (!tie.empty()) {
            props.add("dismax.tie", tie);
        }
        if (with_boosts) {
            props.add("dismax.boost.a", "10000");
            props.add("dismax.boost.b", "10000");
            props.add("dismax.boost.c", "1000");
        }
    }
    ~DismaxFixture();

    SimpleTermData* add_term(const std::vector<std::string>& fields) {
        return test.getQueryEnv().getBuilder().addIndexNode(fields);
    }
    void setup() {
        ASSERT_TRUE(test.setup());
        match_data = test.createMatchDataBuilder();
    }
    TermFieldMatchData* tfmd(uint32_t term_id, uint32_t field_id) {
        return match_data->getTermFieldMatchData(term_id, field_id);
    }
    void match(uint32_t term_id, uint32_t field_id, uint32_t doc_id = 1) {
        auto* md = tfmd(term_id, field_id);
        ASSERT_TRUE(md != nullptr);
        md->reset(doc_id);
    }
    bool expect(feature_t expected, uint32_t doc_id = 1) {
        return test.execute(RankResult().addScore("dismax", expected).setEpsilon(1e-6), doc_id);
    }
};

DismaxFixture::~DismaxFixture() = default;

TEST(DismaxExecutorTest, single_term_single_field) {
    DismaxFixture f;
    f.add_term({"a", "b", "c"});
    f.setup();
    f.match(0, 0);
    EXPECT_TRUE(f.expect(10000));
}

TEST(DismaxExecutorTest, single_term_several_fields_uses_max_plus_tie) {
    DismaxFixture f;
    f.add_term({"a", "b", "c"});
    f.setup();
    f.match(0, 0);
    f.match(0, 1);
    f.match(0, 2);
    EXPECT_TRUE(f.expect(10000 + 0.2 * (10000 + 1000)));
}

TEST(DismaxExecutorTest, low_boost_field_only) {
    DismaxFixture f;
    f.add_term({"a", "b", "c"});
    f.setup();
    f.match(0, 2);
    EXPECT_TRUE(f.expect(1000));
}

TEST(DismaxExecutorTest, several_terms_are_summed) {
    DismaxFixture f;
    f.add_term({"a", "b", "c"});
    f.add_term({"a", "b", "c"});
    f.setup();
    f.match(0, 0);
    f.match(0, 2);
    f.match(1, 2);
    EXPECT_TRUE(f.expect((10000 + 0.2 * 1000) + 1000));
}

TEST(DismaxExecutorTest, more_than_ten_terms_all_score) {
    DismaxFixture f;
    for (uint32_t i = 0; i < 12; ++i) {
        f.add_term({"a"});
    }
    f.setup();
    for (uint32_t i = 0; i < 12; ++i) {
        f.match(i, 0);
    }
    EXPECT_TRUE(f.expect(12 * 10000));
}

TEST(DismaxExecutorTest, no_match_gives_zero) {
    DismaxFixture f;
    f.add_term({"a", "b", "c"});
    f.setup();
    EXPECT_TRUE(f.expect(0));
}

TEST(DismaxExecutorTest, match_for_another_document_is_ignored) {
    DismaxFixture f;
    f.add_term({"a"});
    f.setup();
    f.match(0, 0, 2);
    EXPECT_TRUE(f.expect(0, 1));
}

TEST(DismaxExecutorTest, terms_on_unconfigured_fields_contribute_nothing) {
    DismaxFixture f;
    f.add_term({"d"});
    f.add_term({"a", "d"});
    f.setup();
    f.match(0, 3);
    f.match(1, 3);
    f.match(1, 0);
    EXPECT_TRUE(f.expect(10000));
}

TEST(DismaxExecutorTest, no_boosts_configured_gives_zero) {
    DismaxFixture f(false);
    f.add_term({"a", "b"});
    f.setup();
    f.match(0, 0);
    f.match(0, 1);
    EXPECT_TRUE(f.expect(0));
}

TEST(DismaxExecutorTest, tie_defaults_to_zero) {
    DismaxFixture f(true, "");
    f.add_term({"a", "b", "c"});
    f.setup();
    f.match(0, 0);
    f.match(0, 1);
    f.match(0, 2);
    EXPECT_TRUE(f.expect(10000));
}

TEST(DismaxExecutorTest, tie_one_gives_sum) {
    DismaxFixture f(true, "1.0");
    f.add_term({"a", "c"});
    f.setup();
    f.match(0, 0);
    f.match(0, 2);
    EXPECT_TRUE(f.expect(11000));
}

// A phrase is a single term data, so it counts as one term like any other.
TEST(DismaxExecutorTest, phrase_counts_as_one_term) {
    DismaxFixture f;
    auto*         phrase = f.add_term({"a", "c"});
    phrase->setPhraseLength(2);
    f.setup();
    f.match(0, 0);
    f.match(0, 2);
    EXPECT_TRUE(f.expect(10000 + 0.2 * 1000));
}

// Terms hidden from ranking (e.g. in a non-contributing branch) must not score.
TEST(DismaxExecutorTest, hidden_from_ranking_is_skipped) {
    DismaxFixture f;
    f.add_term({"a", "c"});
    f.setup();
    f.match(0, 0);
    f.match(0, 2);
    f.tfmd(0, 0)->set_hidden_from_ranking();
    EXPECT_TRUE(f.expect(1000));
}

// Reproduces the catena per-term score: 8000 + 2000h + 200l with tie 0.2,
// high boost 10000 and low boost 1000, for h high and l low matching fields.
TEST(DismaxExecutorTest, matches_unrolled_slot_formula) {
    DismaxFixture f;
    f.add_term({"a", "b", "c"});
    f.setup();
    f.match(0, 0);
    f.match(0, 1);
    f.match(0, 2);
    EXPECT_TRUE(f.expect(8000 + 2000 * 2 + 200 * 1));
}

} // namespace

GTEST_MAIN_RUN_ALL_TESTS()

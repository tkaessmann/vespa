// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include <vespa/searchlib/fef/blueprint.h>

#include <vector>

namespace search::features {

/**
 * Blueprint for the dismax feature.
 *
 * Computes the sum over all query terms of a per-term disjunction max score:
 *
 *   w_f(t)   = boost(f) if term t matched field f, else 0
 *   score(t) = max_f w_f(t) + tie * (sum_f w_f(t) - max_f w_f(t))
 *   dismax   = sum_t score(t)
 *
 * Field boosts are given as rank properties "dismax.boost.<field>", the tie
 * breaker as "dismax.tie" (default 0). Fields without a positive boost are
 * ignored, so terms only searching such fields contribute nothing.
 */
class DismaxBlueprint : public fef::Blueprint {
public:
    struct FieldBoost {
        uint32_t field_id;
        feature_t boost;
    };

private:
    std::vector<FieldBoost> _fields;
    feature_t               _tie;

public:
    DismaxBlueprint();
    ~DismaxBlueprint() override;

    void visitDumpFeatures(const fef::IIndexEnvironment& env, fef::IDumpFeatureVisitor& visitor) const override;
    fef::Blueprint::UP createInstance() const override;
    fef::ParameterDescriptions getDescriptions() const override;
    bool setup(const fef::IIndexEnvironment& env, const fef::ParameterList& params) override;
    fef::FeatureExecutor& createExecutor(const fef::IQueryEnvironment& env, vespalib::Stash& stash) const override;
};

} // namespace search::features

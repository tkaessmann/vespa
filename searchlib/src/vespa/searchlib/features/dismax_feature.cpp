// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "dismax_feature.h"

#include "utils.h"
#include "valuefeature.h"

#include <vespa/searchlib/fef/fieldinfo.h>
#include <vespa/searchlib/fef/iqueryenvironment.h>
#include <vespa/searchlib/fef/itermdata.h>
#include <vespa/searchlib/fef/itermfielddata.h>
#include <vespa/searchlib/fef/match_data_details.h>
#include <vespa/searchlib/fef/matchdata.h>
#include <vespa/searchlib/fef/properties.h>
#include <vespa/vespalib/util/stash.h>

#include <algorithm>

namespace search::features {

using fef::Blueprint;
using fef::FeatureExecutor;
using fef::IllegalHandle;
using fef::ITermFieldData;
using fef::MatchDataDetails;
using fef::TermFieldHandle;

namespace {

struct BoostedHandle {
    TermFieldHandle handle;
    feature_t       boost;
};

/**
 * Executor summing per-term dismax scores. Handles are stored flat, with
 * _term_ends[i] marking the end of the handles belonging to term i.
 */
class DismaxExecutor : public fef::FeatureExecutor {
    std::vector<BoostedHandle> _handles;
    std::vector<uint32_t>      _term_ends;
    feature_t                  _tie;
    const fef::MatchData*      _md;

    void handle_bind_match_data(const fef::MatchData& md) override { _md = &md; }

public:
    DismaxExecutor(std::vector<BoostedHandle> handles, std::vector<uint32_t> term_ends, feature_t tie)
        : _handles(std::move(handles)), _term_ends(std::move(term_ends)), _tie(tie), _md(nullptr) {}

    void execute(uint32_t doc_id) override {
        feature_t total = 0.0;
        uint32_t  begin = 0;
        for (uint32_t end : _term_ends) {
            feature_t max = 0.0;
            feature_t sum = 0.0;
            for (uint32_t i = begin; i < end; ++i) {
                if (_md->resolveTermField(_handles[i].handle)->has_ranking_data(doc_id)) {
                    max = std::max(max, _handles[i].boost);
                    sum += _handles[i].boost;
                }
            }
            total += max + _tie * (sum - max);
            begin = end;
        }
        outputs().set_number(0, total);
    }
};

} // namespace

DismaxBlueprint::DismaxBlueprint() : Blueprint("dismax"), _fields(), _tie(0.0) {
}

DismaxBlueprint::~DismaxBlueprint() = default;

void DismaxBlueprint::visitDumpFeatures(const fef::IIndexEnvironment&, fef::IDumpFeatureVisitor&) const {
}

Blueprint::UP DismaxBlueprint::createInstance() const {
    return std::make_unique<DismaxBlueprint>();
}

fef::ParameterDescriptions DismaxBlueprint::getDescriptions() const {
    return fef::ParameterDescriptions().desc();
}

bool DismaxBlueprint::setup(const fef::IIndexEnvironment& env, const fef::ParameterList&) {
    const auto& props = env.getProperties();
    _tie = util::strToNum<feature_t>(props.lookup(getBaseName(), "tie").get("0"));
    _fields.clear();
    for (uint32_t i = 0; i < env.getNumFields(); ++i) {
        const fef::FieldInfo* info = env.getField(i);
        fef::Property         prop = props.lookup(getBaseName(), "boost", info->name());
        if (!prop.found()) {
            continue;
        }
        feature_t boost = util::strToNum<feature_t>(prop.get());
        if (boost > 0.0) {
            _fields.push_back({info->id(), boost});
        }
    }
    describeOutput("score", "The sum over query terms of the best matching field boost plus tie times the boosts "
                            "of the other matching fields.");
    return true;
}

FeatureExecutor& DismaxBlueprint::createExecutor(const fef::IQueryEnvironment& env, vespalib::Stash& stash) const {
    std::vector<BoostedHandle> handles;
    std::vector<uint32_t>      term_ends;
    for (uint32_t term_id = 0; term_id < env.getNumTerms(); ++term_id) {
        size_t before = handles.size();
        for (const FieldBoost& field : _fields) {
            const ITermFieldData* tfd = util::getTermFieldData(env, term_id, field.field_id);
            if (tfd == nullptr) {
                continue;
            }
            // Only the document id is needed, so request the cheaper Interleaved details.
            TermFieldHandle handle = tfd->getHandle(MatchDataDetails::Interleaved);
            if (handle != IllegalHandle) {
                handles.push_back({handle, field.boost});
            }
        }
        if (handles.size() > before) {
            term_ends.push_back(handles.size());
        }
    }
    if (term_ends.empty()) {
        return stash.create<SingleZeroValueExecutor>();
    }
    return stash.create<DismaxExecutor>(std::move(handles), std::move(term_ends), _tie);
}

} // namespace search::features

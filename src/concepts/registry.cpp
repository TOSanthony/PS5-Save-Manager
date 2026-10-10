// MacheteSVG - Concept registry
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/concepts.hpp"

namespace hui::app
{

std::span<const ConceptFactory> concept_registry()
{
    static constexpr ConceptFactory kFactories[] = {
        concepts::make_machete,
    };
    return kFactories;
}

} // namespace hui::app

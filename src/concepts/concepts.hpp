// MacheteSVG - Concept declarations
// Copyright (C) 2026 MacheteSVG Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"
#include <memory>

namespace hui::concepts
{

std::unique_ptr<app::Concept> make_machete(app::Context &context);

} // namespace hui::concepts

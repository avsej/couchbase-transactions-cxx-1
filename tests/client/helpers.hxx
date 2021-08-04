/*
 *     Copyright 2021 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#pragma once

#include <couchbase/internal/nlohmann/json.hpp>
#include <string>

struct SimpleObject {
    std::string name;
    uint64_t number;
};

bool
operator==(const SimpleObject& lhs, const SimpleObject& rhs);

void
to_json(nlohmann::json& j, const SimpleObject& o);

void
from_json(const nlohmann::json& j, SimpleObject& o);

struct AnotherSimpleObject {
    std::string foo;
};

bool
operator==(const AnotherSimpleObject& lhs, const AnotherSimpleObject& rhs);

void
to_json(nlohmann::json& j, const AnotherSimpleObject& o);

void
from_json(const nlohmann::json& j, AnotherSimpleObject& o);

/*
 *     Copyright 2020 Couchbase, Inc.
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
#include <couchbase/support.hxx>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#define TXN_LOG "transactions"
#define ATTEMPT_CLEANUP_LOG "attempt_cleanup"
#define LOST_ATTEMPT_CLEANUP_LOG "lost_attempt_cleanup"

namespace couchbase
{
namespace transactions
{
    static const std::string attempt_format_string("[{}/{}]:");

    static std::shared_ptr<spdlog::logger> txn_log = spdlog::get(TXN_LOG) ? spdlog::get(TXN_LOG) : spdlog::stdout_logger_mt(TXN_LOG);

    static std::shared_ptr<spdlog::logger> attempt_cleanup_log =
      spdlog::get(ATTEMPT_CLEANUP_LOG) ? spdlog::get(ATTEMPT_CLEANUP_LOG) : spdlog::stdout_logger_mt(ATTEMPT_CLEANUP_LOG);

    static std::shared_ptr<spdlog::logger> lost_attempts_cleanup_log =
      spdlog::get(LOST_ATTEMPT_CLEANUP_LOG) ? spdlog::get(LOST_ATTEMPT_CLEANUP_LOG) : spdlog::stdout_logger_mt(LOST_ATTEMPT_CLEANUP_LOG);

} // namespace transactions
} // namespace couchbase

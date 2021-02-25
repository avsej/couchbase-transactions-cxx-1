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

#include "atr_entry.hxx"
#include <boost/optional/optional.hpp>
#include <chrono>
#include <couchbase/client/collection.hxx>
#include <couchbase/transactions/transaction_document.hxx>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <mutex>

#include "logging.hxx"

namespace couchbase
{
namespace transactions
{

    // need forward declaration to avoid circular dep.
    class transactions_cleanup;
    class transactions_cleanup_attempt;
    class attempt_context;

    // need forward declaration for compare
    class atr_cleanup_entry;

    // comparator class for ordering queue
    class compare_atr_entries
    {
      public:
        bool operator()(atr_cleanup_entry& lhs, atr_cleanup_entry& rhs);
    };

    // represents an atr entry we would like to clean
    class atr_cleanup_entry
    {
        friend class transactions_cleanup_attempt;

      private:
        std::string atr_id_;
        std::string attempt_id_;
        std::shared_ptr<couchbase::collection> atr_collection_;
        std::chrono::time_point<std::chrono::steady_clock> min_start_time_;
        bool check_if_expired_;
        const transactions_cleanup* cleanup_;
        static const uint32_t safety_margin_ms_;

        // we may construct from an atr_entry -- if so hold on to it and avoid lookup
        // later.
        const atr_entry* atr_entry_;

        friend class compare_atr_entries;

        void check_atr_and_cleanup(std::shared_ptr<spdlog::logger> logger, transactions_cleanup_attempt* result);
        void cleanup_docs(std::shared_ptr<spdlog::logger> logger);
        void cleanup_entry(std::shared_ptr<spdlog::logger> logger);
        void commit_docs(std::shared_ptr<spdlog::logger> logger, boost::optional<std::vector<doc_record>> docs);
        void remove_docs(std::shared_ptr<spdlog::logger> logger, boost::optional<std::vector<doc_record>> docs);
        void remove_docs_staged_for_removal(std::shared_ptr<spdlog::logger> logger, boost::optional<std::vector<doc_record>> docs);
        void remove_txn_links(std::shared_ptr<spdlog::logger> logger, boost::optional<std::vector<doc_record>> docs);
        void do_per_doc(std::shared_ptr<spdlog::logger> logger,
                        std::vector<doc_record> docs,
                        bool require_crc_to_match,
                        const std::function<void(std::shared_ptr<spdlog::logger>, transaction_document&, bool)>& call);

      public:
        explicit atr_cleanup_entry(attempt_context& ctx);
        explicit atr_cleanup_entry(const atr_entry& entry,
                                   std::shared_ptr<couchbase::collection> atr_coll,
                                   const transactions_cleanup& cleanup,
                                   bool check_if_expired = true);

        explicit atr_cleanup_entry(const std::string& atr_id,
                                   const std::string& attempt_id,
                                   std::shared_ptr<couchbase::collection> atr_collection,
                                   const transactions_cleanup& cleanup);

        void clean(std::shared_ptr<spdlog::logger> logger, transactions_cleanup_attempt* result = nullptr);
        bool ready() const;

        template<typename OStream>
        friend OStream& operator<<(OStream& os, const atr_cleanup_entry& e)
        {
            os << "atr_cleanup_entry{";
            os << "atr_id:" << e.atr_id_ << ",";
            os << "attempt_id:" << e.attempt_id_ << ",";
            os << "atr_collection:" << e.atr_collection_->name() << ",";
            os << "check_if_expired:" << e.check_if_expired_;
            os << "min_start_time:" << std::chrono::duration_cast<std::chrono::milliseconds>(e.min_start_time_.time_since_epoch()).count();
            os << "}";
            return os;
        }
        void min_start_time(std::chrono::time_point<std::chrono::steady_clock> new_time)
        {
            min_start_time_ = new_time;
        }
    };

    // holds sorted atr entries for cleaning
    class atr_cleanup_queue
    {
      private:
        mutable std::mutex mutex_;
        std::priority_queue<atr_cleanup_entry, std::vector<atr_cleanup_entry>, compare_atr_entries> queue_;

      public:
        // pop, but only if the front entry's min_start_time_ is before now
        boost::optional<atr_cleanup_entry> pop(bool check_time = true);
        void push(attempt_context& ctx);
        void push(const atr_cleanup_entry& entry);
        int size() const;
    };

} // namespace transactions
} // namespace couchbase

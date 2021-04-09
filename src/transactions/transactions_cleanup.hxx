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

#include <couchbase/client/cluster.hxx>
#include <couchbase/transactions/transaction_config.hxx>

#include <atomic>
#include <condition_variable>
#include <thread>

#include "atr_cleanup_entry.hxx"
#include "client_record.hxx"

namespace couchbase
{
class cluster;

namespace transactions
{
    // only really used when we force cleanup, in tests
    class transactions_cleanup_attempt
    {
      private:
        const std::string atr_id_;
        const std::string attempt_id_;
        const std::string atr_bucket_name_;
        bool success_;
        attempt_state state_;

      public:
        transactions_cleanup_attempt(const atr_cleanup_entry&);

        CB_NODISCARD bool success() const
        {
            return success_;
        }
        void success(bool success)
        {
            success_ = success;
        }
        CB_NODISCARD const std::string atr_id() const
        {
            return atr_id_;
        }
        CB_NODISCARD const std::string attempt_id() const
        {
            return attempt_id_;
        }
        CB_NODISCARD const std::string atr_bucket_name() const
        {
            return atr_bucket_name_;
        }
        CB_NODISCARD attempt_state state() const
        {
            return state_;
        }
        void state(attempt_state state)
        {
            state_ = state;
        }
    };

    struct atr_cleanup_stats {
        bool exists;
        size_t num_entries;

        atr_cleanup_stats()
          : exists(false)
          , num_entries(0)
        {
        }
    };

    class transactions_cleanup
    {
      public:
        transactions_cleanup(couchbase::cluster& cluster, const transaction_config& config);
        ~transactions_cleanup();

        CB_NODISCARD couchbase::cluster& cluster() const
        {
            return cluster_;
        };

        CB_NODISCARD const transaction_config& config() const
        {
            return config_;
        }

        // Add an attempt to cleanup later.
        void add_attempt(attempt_context& ctx);

        int cleanup_queue_length() const
        {
            return atr_queue_.size();
        }

        // only used for testing.
        void force_cleanup_attempts(std::vector<transactions_cleanup_attempt>& results);
        // only used for testing
        void force_cleanup_entry(atr_cleanup_entry& entry, transactions_cleanup_attempt& attempt);
        // only used for testing
        const atr_cleanup_stats force_cleanup_atr(std::shared_ptr<couchbase::collection> coll,
                                                  const std::string& atr_id,
                                                  std::vector<transactions_cleanup_attempt>& results);
        const client_record_details get_active_clients(std::shared_ptr<couchbase::collection> coll, const std::string& uuid);
        void remove_client_record_from_all_buckets(const std::string& uuid);
        void close();

      private:
        couchbase::cluster& cluster_;
        const transaction_config& config_;
        const std::chrono::milliseconds cleanup_loop_delay_{ 100 };

        std::thread lost_attempts_thr_;
        std::thread cleanup_thr_;
        atr_cleanup_queue atr_queue_;
        mutable std::condition_variable cv_;
        mutable std::mutex mutex_;

        const std::string client_uuid_;

        void attempts_loop();

        template<class R, class P>
        bool interruptable_wait(std::chrono::duration<R, P> time);

        void lost_attempts_loop();
        void clean_lost_attempts_in_bucket(const std::string& bucket_name);
        void create_client_record(std::shared_ptr<couchbase::collection> coll);
        const atr_cleanup_stats handle_atr_cleanup(std::shared_ptr<couchbase::collection> coll,
                                                   const std::string& atr_id,
                                                   std::vector<transactions_cleanup_attempt>* result = nullptr);

        std::atomic<bool> running_{ false };
    };
} // namespace transactions
} // namespace couchbase

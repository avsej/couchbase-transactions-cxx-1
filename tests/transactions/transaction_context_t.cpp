/*
 *     Copyright 2022 Couchbase, Inc.
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

#include "../../src/transactions/attempt_context_impl.hxx"
#include "helpers.hxx"
#include "transactions_env.h"
#include <couchbase/errors.hxx>
#include <couchbase/transactions.hxx>
#include <couchbase/transactions/internal/transaction_context.hxx>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <future>
#include <stdexcept>

using namespace couchbase::transactions;
auto tx_content = nlohmann::json::parse("{\"some\":\"thing\"}");

void
txn_completed(std::exception_ptr err, std::shared_ptr<std::promise<void>> barrier)
{
    if (err) {
        barrier->set_exception(err);
    } else {
        barrier->set_value();
    }
};

TEST(SimpleTxnContext, CanDoSimpleTxn)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    ASSERT_TRUE(TransactionsTestEnvironment::upsert_doc(id, tx_content.dump()));
    transaction_context tx(txns);
    tx.new_attempt_context();
    auto new_content = nlohmann::json::parse("{\"some\":\"thing else\"}");
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    tx.get(id, [&](std::exception_ptr err, std::optional<transaction_get_result> res) {
        EXPECT_TRUE(res);
        EXPECT_FALSE(err);
        tx.replace(*res, new_content.dump(), [&](std::exception_ptr err, std::optional<transaction_get_result> replaced) {
            EXPECT_TRUE(replaced);
            EXPECT_FALSE(err);
            tx.commit([&](std::exception_ptr err) {
                EXPECT_FALSE(err);
                txn_completed(err, barrier);
            });
        });
    });
    f.get();
    ASSERT_EQ(TransactionsTestEnvironment::get_doc(id).content_as<nlohmann::json>(), new_content);
}

TEST(SimpleTxnContext, CanRollbackSimpleTxn)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    ASSERT_TRUE(TransactionsTestEnvironment::upsert_doc(id, tx_content.dump()));
    transaction_context tx(txns);
    tx.new_attempt_context();
    auto new_content = nlohmann::json::parse("{\"some\":\"thing else\"}");
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    tx.get(id, [&](std::exception_ptr err, std::optional<transaction_get_result> res) {
        EXPECT_TRUE(res);
        EXPECT_FALSE(err);
        tx.replace(*res, new_content.dump(), [&](std::exception_ptr err, std::optional<transaction_get_result> replaced) {
            EXPECT_TRUE(replaced);
            EXPECT_FALSE(err);
            // now rollback
            tx.rollback([&](std::exception_ptr err) {
                EXPECT_FALSE(err); // no error rolling back
                barrier->set_value();
            });
        });
    });
    f.get();
    // this should not throw, as errors should be empty.
    tx.existing_error();
}

TEST(SimpleTxnContext, CanGetInsertErrors)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    ASSERT_TRUE(TransactionsTestEnvironment::upsert_doc(id, tx_content.dump()));
    transaction_context tx(txns);
    tx.new_attempt_context();
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    tx.insert(id, tx_content.dump(), [&](std::exception_ptr err, std::optional<transaction_get_result> result) {
        // this should result in a transaction_operation_failed exception since it already exists, so lets check it
        EXPECT_TRUE(err);
        EXPECT_FALSE(result);
        if (err) {
            barrier->set_exception(err);
        } else {
            barrier->set_value();
        }
    });
    EXPECT_THROW(f.get(), transaction_operation_failed);
    EXPECT_THROW(tx.existing_error(), transaction_operation_failed);
}

TEST(SimpleTxnContext, CanGetRemoveErrors)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    ASSERT_TRUE(TransactionsTestEnvironment::upsert_doc(id, tx_content.dump()));
    transaction_context tx(txns);
    tx.new_attempt_context();
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    tx.get(id, [&](std::exception_ptr err, std::optional<transaction_get_result> result) {
        // this should result in a transaction_operation_failed exception since it already exists, so lets check it
        EXPECT_FALSE(err);
        EXPECT_TRUE(result);
        // make a cas mismatch error
        result->cas(100);
        tx.remove(*result, [&](std::exception_ptr err) {
            EXPECT_TRUE(err);
            if (err) {
                barrier->set_exception(err);
            } else {
                barrier->set_value();
            }
        });
    });
    EXPECT_THROW(f.get(), transaction_operation_failed);
    EXPECT_THROW(tx.existing_error(), transaction_operation_failed);
}

TEST(SimpleTxnContext, CanGetReplaceErrors)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    ASSERT_TRUE(TransactionsTestEnvironment::upsert_doc(id, tx_content.dump()));
    transaction_context tx(txns);
    tx.new_attempt_context();
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    tx.get(id, [&](std::exception_ptr err, std::optional<transaction_get_result> result) {
        // this should result in a transaction_operation_failed exception since it already exists, so lets check it
        EXPECT_FALSE(err);
        EXPECT_TRUE(result);
        // make a cas mismatch error
        result->cas(100);
        tx.replace(*result, tx_content.dump(), [&](std::exception_ptr err, std::optional<transaction_get_result> result) {
            EXPECT_TRUE(err);
            EXPECT_FALSE(result);
            if (err) {
                barrier->set_exception(err);
            } else {
                barrier->set_value();
            }
        });
    });
    EXPECT_THROW(f.get(), transaction_operation_failed);
    EXPECT_THROW(tx.existing_error(), transaction_operation_failed);
}

TEST(SimpleTxnContext, CanGetGetErrors)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    transaction_context tx(txns);
    tx.new_attempt_context();
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    tx.get(id, [&](std::exception_ptr err, std::optional<transaction_get_result> result) {
        // this should result in a transaction_operation_failed exception since it already exists, so lets check it
        EXPECT_TRUE(err);
        EXPECT_FALSE(result);
        if (err) {
            barrier->set_exception(err);
        } else {
            barrier->set_value();
        }
    });
    EXPECT_THROW(f.get(), transaction_operation_failed);
    EXPECT_THROW(tx.existing_error(), transaction_operation_failed);
}

TEST(SimpleTxnContext, CanDoQuery)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    transaction_context tx(txns);
    tx.new_attempt_context();
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    ASSERT_TRUE(TransactionsTestEnvironment::upsert_doc(id, tx_content.dump()));
    auto query = fmt::format("SELECT * FROM `{}` USE KEYS '{}'", id.bucket(), id.key());
    transaction_query_options opts;
    tx.query(query, opts, [&](std::exception_ptr err, std::optional<couchbase::operations::query_response_payload> payload) {
        // this should result in a transaction_operation_failed exception since the doc isn't there
        EXPECT_TRUE(payload);
        EXPECT_FALSE(err);
        if (err) {
            barrier->set_exception(err);
        } else {
            barrier->set_value();
        }
    });
    ASSERT_NO_THROW(f.get());
    ASSERT_NO_THROW(tx.existing_error());
}

TEST(SimpleTxnContext, CanSeeSomeQueryErrorsButNoTxnFailed)
{
    auto& cluster = TransactionsTestEnvironment::get_cluster();
    auto txns = TransactionsTestEnvironment::get_transactions();
    auto id = TransactionsTestEnvironment::get_document_id();

    transaction_context tx(txns);
    tx.new_attempt_context();
    auto barrier = std::make_shared<std::promise<void>>();
    auto f = barrier->get_future();
    transaction_query_options opts;
    tx.query(
      "jkjkjl;kjlk;  jfjjffjfj", opts, [&](std::exception_ptr err, std::optional<couchbase::operations::query_response_payload> payload) {
          // this should result in a query_exception since the query isn't parseable.
          EXPECT_TRUE(err);
          EXPECT_FALSE(payload);
          if (err) {
              barrier->set_exception(err);
          } else {
              barrier->set_value();
          }
      });
    try {
        f.get();
        FAIL() << "expected future to throw exception";
    } catch (const query_exception& e) {

    } catch (...) {
        auto e = std::current_exception();
        std::cout << "got " << typeid(e).name() << std::endl;
        FAIL() << "expected query_exception to be thrown from the future";
    }
    EXPECT_NO_THROW(tx.existing_error());
}

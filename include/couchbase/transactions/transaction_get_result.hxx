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

#include <ostream>

#include <boost/algorithm/string/split.hpp>

#include <couchbase/internal/nlohmann/json.hpp>

#include <couchbase/client/collection.hxx>
#include <couchbase/transactions/document_metadata.hxx>
#include <couchbase/transactions/transaction_links.hxx>
#include <utility>

namespace couchbase
{
namespace transactions
{
    /**
     * @brief Encapsulates results of an individual transaction operation
     *
     */
    class transaction_get_result
    {
      private:
        collection& collection_;
        std::string value_;
        std::string id_;
        uint64_t cas_;
        transaction_links links_;

        /** This is needed for provide {BACKUP-FIELDS}.  It is only needed from the get to the staged mutation, hence Optional. */
        const boost::optional<document_metadata> metadata_;

      public:
        /** @internal */
        transaction_get_result(const transaction_get_result& doc)
          : collection_(doc.collection_)
          , value_(doc.value_)
          , id_(doc.id_)
          , links_(doc.links_)
          , metadata_(doc.metadata_)
          , cas_(doc.cas_)
        {
        }

        /** @internal */
        template<typename Content>
        transaction_get_result(std::string id,
                               Content content,
                               uint64_t cas,
                               collection& collection,
                               transaction_links links,
                               boost::optional<document_metadata> metadata)
          : id_(std::move(id))
          , cas_(cas)
          , collection_(collection)
          , links_(std::move(links))
          , metadata_(std::move(metadata))
          , value_(std::move(content))
        {
        }

        /** @internal */
        template<typename Content>
        static transaction_get_result create_from(transaction_get_result& document, Content content)
        {
            transaction_links links(document.links().atr_id(),
                                    document.links().atr_bucket_name(),
                                    document.links().atr_scope_name(),
                                    document.links().atr_collection_name(),
                                    document.links().staged_transaction_id(),
                                    document.links().staged_attempt_id(),
                                    document.links().staged_content(),
                                    document.links().cas_pre_txn(),
                                    document.links().revid_pre_txn(),
                                    document.links().exptime_pre_txn(),
                                    document.links().crc32_of_staging(),
                                    document.links().op(),
                                    document.links().forward_compat(),
                                    document.links().is_deleted());

            return transaction_get_result(document.id(), content, document.cas(), document.collection_ref(), links, document.metadata());
        }

        /** @internal */
        static transaction_get_result create_from(collection& collection, std::string id, result res)
        {
            boost::optional<std::string> atr_id;
            boost::optional<std::string> transaction_id;
            boost::optional<std::string> attempt_id;
            boost::optional<std::string> staged_content;
            boost::optional<std::string> atr_bucket_name;
            boost::optional<std::string> atr_scope_name;
            boost::optional<std::string> atr_collection_name;
            boost::optional<nlohmann::json> forward_compat;

            // read from xattrs.txn.restore
            boost::optional<std::string> cas_pre_txn;
            boost::optional<std::string> revid_pre_txn;
            boost::optional<uint32_t> exptime_pre_txn;
            boost::optional<std::string> crc32_of_staging;

            // read from $document
            boost::optional<std::string> cas_from_doc;
            boost::optional<std::string> revid_from_doc;
            boost::optional<uint32_t> exptime_from_doc;
            boost::optional<std::string> crc32_from_doc;

            boost::optional<std::string> op;
            std::string content;

            if (res.values[0].has_value()) {
                atr_id = res.values[0].content_as<std::string>();
            }
            if (res.values[1].has_value()) {
                transaction_id = res.values[1].content_as<std::string>();
            }
            if (res.values[2].has_value()) {
                attempt_id = res.values[2].content_as<std::string>();
            }
            if (res.values[3].has_value()) {
                staged_content = res.values[3].content_as<std::string>();
            }
            if (res.values[4].has_value()) {
                atr_bucket_name = res.values[4].content_as<std::string>();
            }
            if (res.values[5].has_value()) {
                auto name = res.values[5].content_as<std::string>();
                std::vector<std::string> splits;
                boost::split(splits, name, [](char c) { return c == '.'; });
                atr_scope_name = splits[0];
                atr_collection_name = splits[1];
            }
            if (res.values[6].has_value()) {
                auto restore = res.values[6].content_as<nlohmann::json>();
                cas_pre_txn = restore["CAS"].get<std::string>();
                // only present in 6.5+
                revid_pre_txn = restore["revid"].get<std::string>();
                exptime_pre_txn = restore["exptime"].get<uint32_t>();
            }
            if (res.values[7].has_value()) {
                op = res.values[7].content_as<std::string>();
            }
            if (res.values[8].has_value()) {
                auto doc = res.values[8].content_as<nlohmann::json>();
                cas_from_doc = doc["CAS"].get<std::string>();
                // only present in 6.5+
                revid_from_doc = doc["revid"].get<std::string>();
                exptime_from_doc = doc["exptime"].get<uint32_t>();
                crc32_from_doc = doc["value_crc32c"].get<std::string>();
            }
            if (res.values[9].has_value()) {
                crc32_of_staging = res.values[9].content_as<std::string>();
            }
            if (res.values[10].has_value()) {
                forward_compat = res.values[10].content_as<nlohmann::json>();
            } else {
                forward_compat = nlohmann::json::object();
            }
            if (res.values[11].has_value()) {
                content = res.values[11].content_as<nlohmann::json>().dump();
            }

            transaction_links links(atr_id,
                                    atr_bucket_name,
                                    atr_scope_name,
                                    atr_collection_name,
                                    transaction_id,
                                    attempt_id,
                                    staged_content,
                                    cas_pre_txn,
                                    revid_pre_txn,
                                    exptime_pre_txn,
                                    crc32_of_staging,
                                    op,
                                    forward_compat,
                                    res.is_deleted);
            document_metadata md(cas_from_doc, revid_from_doc, exptime_from_doc, crc32_from_doc);
            return transaction_get_result(id, content, res.cas, collection, links, boost::make_optional(md));
        }

        /** @internal */
        template<typename Content>
        transaction_get_result& operator=(const transaction_get_result& other)
        {
            if (this != &other) {
                this->collection_ = other.collection_;
                this->value_ = other.value_;
                this->id_ = other.id_;
                this->links_ = other.links_;
            }
            return *this;
        }

        /**
         * @brief Collection that contains this document.
         *
         * @return reference to the collection containing this document.
         */
        CB_NODISCARD collection& collection_ref()
        {
            return collection_;
        }

        /**
         * @brief Content of the document.
         *
         * The content of the document is stored as json.  That is represented internally as
         * a nlohmann::json object.  If the documents have a c++ class that represents them, it
         * can be returned here by adding a to_json and from_json helper.
         * @code{.cpp}
         * namespace my_namespace {
         *   struct my_doc
         *   {
         *     std::string name;
         *     uint32_t age;
         *   };
         *
         *   void from_json(const nlohmann::json& j, my_doc& d)
         *   {
         *      j.at("name").get_to(d.name);
         *      j.at("age").get_to(d.age);
         *   }
         *   void to_json(nlohmann::json& j, const my_doc& d)
         *   {
         *      j = nlohmann::json({"name", d.name}, {"age", d.age});
         *   }
         * @endcode
         *
         * Then, you can do:
         * @code{.cpp}
         * ...
         * txn.run([&](attempt_context& ctx) {
         *   auto txn_doc = ctx.get("mydocid");
         *   my_namespace::my_doc& mydoc = td.content<my_doc>();
         *   ...
         * });
         * @endcode
         *
         * See @ref examples/game_server, and for more detail https://github.com/nlohmann/json#arbitrary-types-conversions
         *
         * @return content of the document.
         */
        template<typename Content>
        CB_NODISCARD Content content()
        {
            return default_json_serializer::deserialize<Content>(value_);
        }

        void content(const std::string& content)
        {
            value_ = content;
        }

        /**
         * @brief Get document id.
         *
         * @return the id of this document.
         */
        CB_NODISCARD const std::string& id() const
        {
            return id_;
        }

        /**
         * @brief Get document CAS.
         *
         * @return the CAS for this document.
         */
        CB_NODISCARD uint64_t cas() const
        {
            return cas_;
        }

        /** @internal */
        CB_NODISCARD transaction_links links() const
        {
            return links_;
        }

        /**
         * @brief Set document CAS.
         *
         * @param cas desired CAS for document.
         */
        void cas(uint64_t cas)
        {
            cas_ = cas;
        }

        /**
         * @brief Get document metadata.
         *
         * @return metadata for this document.
         */
        CB_NODISCARD const boost::optional<document_metadata>& metadata() const
        {
            return metadata_;
        }

        /** @internal */
        template<typename OStream>
        friend OStream& operator<<(OStream& os, const transaction_get_result document)
        {
            os << "transaction_get_result{id: " << document.id_ << ", cas: " << document.cas_
               << ", bucket: " << document.collection_.bucket_name() << ", coll: " << document.collection_.name()
               << ", links_: " << document.links_ << "}";
            return os;
        }
    };
} // namespace transactions
} // namespace couchbase

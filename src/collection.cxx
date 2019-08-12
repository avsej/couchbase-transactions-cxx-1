#include <iostream>

#include <libcouchbase/bucket.hxx>
#include <libcouchbase/collection.hxx>
#include <libcouchbase/result.hxx>
#include <libcouchbase/couchbase.h>
#include <libcouchbase/lookup_in_spec.hxx>
#include <utility>

extern "C" {
static void store_callback(lcb_INSTANCE *, int, const lcb_RESPSTORE *resp)
{
    couchbase::result *res = nullptr;
    lcb_respstore_cookie(resp, reinterpret_cast<void **>(&res));
    res->rc = lcb_respstore_status(resp);
    lcb_respstore_cas(resp, &res->cas);
    const char *data = nullptr;
    size_t ndata = 0;
    lcb_respstore_key(resp, &data, &ndata);
    res->key = std::string(data, ndata);
}

static void get_callback(lcb_INSTANCE *, int, const lcb_RESPGET *resp)
{
    couchbase::result *res = nullptr;
    lcb_respget_cookie(resp, reinterpret_cast<void **>(&res));
    res->rc = lcb_respget_status(resp);
    if (res->rc == LCB_SUCCESS) {
        lcb_respget_cas(resp, &res->cas);
        lcb_respget_datatype(resp, &res->datatype);
        lcb_respget_flags(resp, &res->flags);

        const char *data = nullptr;
        size_t ndata = 0;
        lcb_respget_key(resp, &data, &ndata);
        res->key = std::string(data, ndata);
        lcb_respget_value(resp, &data, &ndata);
        std::string err;
        res->value = json11::Json::parse(std::string(data, ndata), err);
    }
}

static void remove_callback(lcb_INSTANCE *, int, const lcb_RESPREMOVE *resp)
{
    couchbase::result *res = nullptr;
    lcb_respremove_cookie(resp, reinterpret_cast<void **>(&res));
    res->rc = lcb_respremove_status(resp);
    lcb_respremove_cas(resp, &res->cas);
    const char *data = nullptr;
    size_t ndata = 0;
    lcb_respremove_key(resp, &data, &ndata);
    res->key = std::string(data, ndata);
}

static void subdoc_callback(lcb_INSTANCE *, int, const lcb_RESPSUBDOC *resp)
{
    couchbase::result *res = nullptr;
    lcb_respsubdoc_cookie(resp, reinterpret_cast<void **>(&res));
    res->rc = lcb_respsubdoc_status(resp);
    lcb_respsubdoc_cas(resp, &res->cas);
    const char *data = nullptr;
    size_t ndata = 0;
    lcb_respsubdoc_key(resp, &data, &ndata);
    res->key = std::string(data, ndata);

    size_t len = lcb_respsubdoc_result_size(resp);
    res->values.resize(len);
    for (size_t idx = 0; idx < len; idx++) {
        data = nullptr;
        ndata = 0;
        lcb_respsubdoc_result_value(resp, idx, &data, &ndata);
        if (data) {
            std::string err;
            res->values[idx] = json11::Json::parse(std::string(data, ndata), err);
        }
    }
}
}

couchbase::collection::collection(bucket *bucket, std::string scope, std::string name)
    : bucket_(bucket), scope_(std::move(scope)), name_(std::move(name))
{
    lcb_install_callback3(bucket_->lcb_, LCB_CALLBACK_STORE, reinterpret_cast<lcb_RESPCALLBACK>(store_callback));
    lcb_install_callback3(bucket_->lcb_, LCB_CALLBACK_GET, reinterpret_cast<lcb_RESPCALLBACK>(get_callback));
    lcb_install_callback3(bucket_->lcb_, LCB_CALLBACK_REMOVE, reinterpret_cast<lcb_RESPCALLBACK>(remove_callback));
    lcb_install_callback3(bucket_->lcb_, LCB_CALLBACK_SDLOOKUP, reinterpret_cast<lcb_RESPCALLBACK>(subdoc_callback));
    lcb_install_callback3(bucket_->lcb_, LCB_CALLBACK_SDMUTATE, reinterpret_cast<lcb_RESPCALLBACK>(subdoc_callback));
    const char *tmp;
    lcb_cntl(bucket_->lcb_, LCB_CNTL_GET, LCB_CNTL_BUCKETNAME, &tmp);
    bucket_name_ = std::string(tmp);
}

const std::string &couchbase::collection::name() const
{
    return name_;
}

const std::string &couchbase::collection::scope() const
{
    return scope_;
}

const std::string &couchbase::collection::bucket_name() const
{
    return bucket_name_;
}

couchbase::result couchbase::collection::get(const std::string &id)
{
    lcb_CMDGET *cmd;
    lcb_cmdget_create(&cmd);
    lcb_cmdget_key(cmd, id.data(), id.size());
    lcb_cmdget_collection(cmd, scope_.data(), scope_.size(), name_.data(), name_.size());
    lcb_STATUS rc;
    result res;
    rc = lcb_get(bucket_->lcb_, reinterpret_cast<void *>(&res), cmd);
    lcb_cmdget_destroy(cmd);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to get (sched) document: ") + lcb_strerror_short(rc));
    }
    lcb_wait(bucket_->lcb_);
    return res;
}

couchbase::result couchbase::collection::store(lcb_STORE_OPERATION operation, const std::string &id, const std::string &value, uint64_t cas)
{
    lcb_CMDSTORE *cmd;
    lcb_cmdstore_create(&cmd, operation);
    lcb_cmdstore_key(cmd, id.data(), id.size());
    lcb_cmdstore_value(cmd, value.data(), value.size());
    lcb_cmdstore_cas(cmd, cas);
    lcb_cmdstore_collection(cmd, scope_.data(), scope_.size(), name_.data(), name_.size());
    lcb_STATUS rc;
    result res;
    rc = lcb_store(bucket_->lcb_, reinterpret_cast<void *>(&res), cmd);
    lcb_cmdstore_destroy(cmd);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to store (sched) document: ") + lcb_strerror_short(rc));
    }
    lcb_wait(bucket_->lcb_);
    return res;
}

couchbase::result couchbase::collection::upsert(const std::string &id, const std::string &value, uint64_t cas)
{
    return store(LCB_STORE_UPSERT, id, value, cas);
}

couchbase::result couchbase::collection::insert(const std::string &id, const std::string &value)
{
    return store(LCB_STORE_ADD, id, value, 0);
}

couchbase::result couchbase::collection::replace(const std::string &id, const std::string &value, uint64_t cas)
{
    return store(LCB_STORE_REPLACE, id, value, cas);
}

couchbase::result couchbase::collection::remove(const std::string &id, uint64_t cas)
{
    lcb_CMDREMOVE *cmd;
    lcb_cmdremove_create(&cmd);
    lcb_cmdremove_key(cmd, id.data(), id.size());
    lcb_cmdremove_cas(cmd, cas);
    lcb_cmdremove_collection(cmd, scope_.data(), scope_.size(), name_.data(), name_.size());
    lcb_STATUS rc;
    result res;
    rc = lcb_remove(bucket_->lcb_, reinterpret_cast<void *>(&res), cmd);
    lcb_cmdremove_destroy(cmd);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to remove (sched) document: ") + lcb_strerror_short(rc));
    }
    lcb_wait(bucket_->lcb_);
    return res;
}

couchbase::result couchbase::collection::mutate_in(const std::string &id, const std::vector<couchbase::mutate_in_spec> &specs)
{
    lcb_CMDSUBDOC *cmd;
    lcb_cmdsubdoc_create(&cmd);
    lcb_cmdsubdoc_key(cmd, id.data(), id.size());
    lcb_cmdsubdoc_collection(cmd, scope_.data(), scope_.size(), name_.data(), name_.size());

    lcb_SUBDOCOPS *ops;
    lcb_subdocops_create(&ops, specs.size());
    size_t idx = 0;
    for (const auto &spec : specs) {
        switch (spec.type_) {
            case MUTATE_IN_UPSERT:
                lcb_subdocops_dict_upsert(ops, idx++, spec.flags_, spec.path_.data(), spec.path_.size(), spec.value_.data(),
                                          spec.value_.size());
                break;
            case MUTATE_IN_INSERT:
                lcb_subdocops_dict_add(ops, idx++, spec.flags_, spec.path_.data(), spec.path_.size(), spec.value_.data(),
                                       spec.value_.size());
                break;
            case MUTATE_IN_FULLDOC_UPSERT:
                lcb_subdocops_fulldoc_upsert(ops, idx++, spec.flags_, spec.value_.data(), spec.value_.size());
                break;
            case MUTATE_IN_FULLDOC_INSERT:
                lcb_subdocops_fulldoc_add(ops, idx++, spec.flags_, spec.value_.data(), spec.value_.size());
                break;
        }
    }
    lcb_cmdsubdoc_operations(cmd, ops);
    lcb_STATUS rc;
    result res;
    rc = lcb_subdoc(bucket_->lcb_, reinterpret_cast<void *>(&res), cmd);
    lcb_cmdsubdoc_destroy(cmd);
    lcb_subdocops_destroy(ops);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to mutate (sched) sub-document: ") + lcb_strerror_short(rc));
    }
    lcb_wait(bucket_->lcb_);
    return res;
}

couchbase::result couchbase::collection::lookup_in(const std::string &id, const std::vector<couchbase::lookup_in_spec> &specs)
{
    lcb_CMDSUBDOC *cmd;
    lcb_cmdsubdoc_create(&cmd);
    lcb_cmdsubdoc_key(cmd, id.data(), id.size());
    lcb_cmdsubdoc_collection(cmd, scope_.data(), scope_.size(), name_.data(), name_.size());

    lcb_SUBDOCOPS *ops;
    lcb_subdocops_create(&ops, specs.size());
    size_t idx = 0;
    for (const auto &spec : specs) {
        switch (spec.type_) {
            case LOOKUP_IN_GET:
                lcb_subdocops_get(ops, idx++, spec.flags_, spec.path_.data(), spec.path_.size());
                break;
            case LOOKUP_IN_FULLDOC_GET:
                lcb_subdocops_fulldoc_get(ops, idx++, spec.flags_);
                break;
        }
    }
    lcb_cmdsubdoc_operations(cmd, ops);
    lcb_STATUS rc;
    result res;
    rc = lcb_subdoc(bucket_->lcb_, reinterpret_cast<void *>(&res), cmd);
    lcb_cmdsubdoc_destroy(cmd);
    lcb_subdocops_destroy(ops);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to lookup (sched) sub-document: ") + lcb_strerror_short(rc));
    }
    lcb_wait(bucket_->lcb_);
    return res;
}

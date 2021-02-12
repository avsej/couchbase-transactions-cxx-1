#include <couchbase/client/bucket.hxx>
#include <couchbase/client/cluster.hxx>
#include <couchbase/client/result.hxx>
#include <couchbase/support.hxx>
#include <libcouchbase/couchbase.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "logging.hxx"
#include "pool.hxx"

namespace cb = couchbase;

struct cb::instance_pool_event_counter {
    // needed for access to the bucket_counters map
    std::mutex mutex_;
    cb::pool_event_counter<lcb_st*> cluster_counter;
    std::map<std::string, cb::pool_event_counter<lcb_st*>> bucket_counters;

    // since insertion will not invalidate iterators in a map, this reference
    // is safe to use outside the lock.  A lock is only needed to prevent a reader
    // of this structure from racing a writer (this cluster object) in the creation
    // and insertion.
    cb::pool_event_counter<lcb_st*>& bucket(const std::string& name)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return bucket_counters[name];
    }
};

void
shutdown(lcb_st* lcb)
{
    if (lcb == nullptr) {
        return;
    }
    cb::client_log->trace("destroying instance {}", (void*)lcb);
    lcb_destroy(lcb);
}

lcb_st*
connect(const std::string& cluster_address,
        const std::string& user_name,
        const std::string& password,
        boost::optional<std::chrono::microseconds>& kv_timeout)
{
    lcb_st* lcb = nullptr;
    lcb_STATUS rc;
    lcb_CREATEOPTS* opts;
    lcb_createopts_create(&opts, LCB_TYPE_CLUSTER);
    lcb_createopts_connstr(opts, cluster_address.c_str(), cluster_address.size());
    rc = lcb_create(&lcb, opts);
    lcb_createopts_destroy(opts);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to create libcouchbase instance: ") + lcb_strerror_short(rc));
    }

    lcb_AUTHENTICATOR* auth = lcbauth_new();
    lcbauth_set_mode(auth, LCBAUTH_MODE_RBAC);
    rc = lcbauth_add_pass(auth, user_name.c_str(), password.c_str(), LCBAUTH_F_CLUSTER);
    if (rc != LCB_SUCCESS) {
        lcbauth_unref(auth);
        throw std::runtime_error(std::string("failed to build credentials for authenticator: ") + lcb_strerror_short(rc));
    }
    lcb_set_auth(lcb, auth);
    lcbauth_unref(auth);

    rc = lcb_connect(lcb);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to connect (sched) libcouchbase instance: ") + lcb_strerror_short(rc));
    }
    rc = lcb_wait(lcb, LCB_WAIT_DEFAULT);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("failed to connect (wait) libcouchbase instance: ") + lcb_strerror_short(rc));
    }
    if (kv_timeout) {
        uint32_t op_timeout = kv_timeout->count();
        uint32_t durability_timeout;
        lcb_STATUS rv;
        // get the durability timeout floor
        lcb_cntl(lcb, LCB_CNTL_GET, LCB_CNTL_PERSISTENCE_TIMEOUT_FLOOR, &durability_timeout);
        // set the timeout
        rv = lcb_cntl(lcb, LCB_CNTL_SET, LCB_CNTL_OP_TIMEOUT, &op_timeout);
        cb::client_log->trace("Set kv timeout to {} with result {}", op_timeout, rv);
        // set the durability timeout to match the kv timeout, _iff_ op timeout is longer than the current floor
        if (op_timeout > durability_timeout) {
            cb::client_log->trace(
              "durability_timeout {} < op_timeout {}, increasing durability timeout to match", durability_timeout, op_timeout);
            lcb_cntl(lcb, LCB_CNTL_SET, LCB_CNTL_PERSISTENCE_TIMEOUT_FLOOR, &op_timeout);
        }
    } else {
        uint32_t op_timeout;
        lcb_cntl(lcb, LCB_CNTL_GET, LCB_CNTL_OP_TIMEOUT, &op_timeout);
        cb::client_log->trace("default kv_timeout {}us", op_timeout);
        // set it!  Since this _only_ happens in the constructor, we are threadsafe.
        kv_timeout = std::chrono::microseconds(op_timeout);
    }

    rc = lcb_get_bootstrap_status(lcb);
    if (rc != LCB_SUCCESS) {
        throw std::runtime_error(std::string("bootstrap failed with error: ") + lcb_strerror_short(rc));
    }

    cb::client_log->trace("cluster connection successful, returning {}", (void*)lcb);
    return lcb;
}

cb::cluster::cluster(std::string cluster_address, std::string user_name, std::string password, const cluster_options& opts)
  : cluster_address_(std::move(cluster_address))
  , user_name_(std::move(user_name))
  , password_(std::move(password))
  , max_bucket_instances_(opts.max_bucket_instances())
  , event_counter_(opts.event_counter())
  , kv_timeout_(opts.kv_timeout())
{
    instance_pool_ = std::unique_ptr<pool<lcb_st*>>(
      new pool<lcb_st*>(opts.max_instances(), [&] { return connect(cluster_address_, user_name_, password_, kv_timeout_); }, shutdown));

    cb::client_log->info("couchbase client library {} attempting to connect to {}", VERSION_STR, cluster_address_);
    if (nullptr != event_counter_) {
        instance_pool_->set_event_handler([&](pool_event e, lcb_st* const t) { event_counter_->cluster_counter.handler(e, t); });
    }

    // TODO: ponder this - should we connect _now_, or wait until first use?
    // for now, lets get it and release back to pool
    instance_pool_->release(instance_pool_->get());
}

cb::cluster::cluster(const cluster& cluster)
  : cluster_address_(cluster.cluster_address_)
  , user_name_(cluster.user_name_)
  , password_(cluster.password_)
  , max_bucket_instances_(cluster.max_bucket_instances_)
{
    instance_pool_ = cluster.instance_pool_->clone(max_bucket_instances_);
    cb::client_log->info("couchbase client library {} attempting to connect to {}", VERSION_STR, cluster_address_);
    instance_pool_->release(instance_pool_->get());
}

std::chrono::microseconds
cb::cluster::default_kv_timeout() const
{
    return *kv_timeout_;
}

bool
cb::cluster::operator==(const cluster& other) const
{
    return this == &other;
}

cb::cluster::~cluster()
{
    cb::client_log->trace("shutting down cluster");
    open_buckets_.clear();
}

std::shared_ptr<cb::bucket>
cb::cluster::bucket(const std::string& name)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto it =
      std::find_if(open_buckets_.begin(), open_buckets_.end(), [&](const std::shared_ptr<cb::bucket>& b) { return b->name() == name; });
    if (it != open_buckets_.end()) {
        return *it;
    } else {
        // clone the pool, add lcb to it
        cb::client_log->trace("cloning pool, will create bucket {} now...", name);
        auto bucket_pool = instance_pool_->clone(max_bucket_instances_);
        if (event_counter_) {
            auto& ev = event_counter_->bucket(name);
            bucket_pool->set_event_handler([&ev](pool_event e, lcb_st* const t) { ev.handler(e, t); });
        }
        instance_pool_->swap_available(*bucket_pool, true);
        // create the bucket, push into the bucket list...
        auto b = std::shared_ptr<cb::bucket>(new cb::bucket(bucket_pool, name, default_kv_timeout()));
        open_buckets_.push_back(b);
        return b;
    }
}

extern "C" {
static void
http_callback(lcb_INSTANCE*, int, const lcb_RESPHTTP* resp)
{
    cb::result* res = nullptr;
    lcb_resphttp_cookie(resp, reinterpret_cast<void**>(&res));
    res->rc = lcb_resphttp_status(resp);
    if (res->rc == LCB_SUCCESS) {
        const char* data = nullptr;
        size_t ndata = 0;
        lcb_resphttp_body(resp, &data, &ndata);
        res->value = nlohmann::json::parse(data, data + ndata);
    }
}
}

std::list<std::string>
cb::cluster::buckets()
{
    return instance_pool_->wrap_access<std::list<std::string>>([&](lcb_st* lcb) -> std::list<std::string> {
        std::unique_lock<std::mutex> lock(mutex_);
        std::string path("/pools/default/buckets");
        lcb_CMDHTTP* cmd;
        lcb_cmdhttp_create(&cmd, lcb_HTTP_TYPE::LCB_HTTP_TYPE_MANAGEMENT);
        lcb_cmdhttp_method(cmd, lcb_HTTP_METHOD::LCB_HTTP_METHOD_GET);
        lcb_cmdhttp_path(cmd, path.data(), path.size());
        lcb_install_callback(lcb, LCB_CALLBACK_HTTP, (lcb_RESPCALLBACK)http_callback);
        cb::result res;
        lcb_http(lcb, &res, cmd);
        lcb_cmdhttp_destroy(cmd);
        lcb_wait(lcb, LCB_WAIT_DEFAULT);
        if (res.rc != LCB_SUCCESS) {
            throw std::runtime_error(std::string("failed to retrieve list of buckets: ") + res.strerror());
        }
        std::list<std::string> names;
        if (res.value) {
            for (const auto& it : *res.value) {
                names.push_back(it["name"].get<std::string>());
            }
        }
        return names;
    });
}
size_t
cb::cluster::max_instances() const
{
    return instance_pool_->max_size();
}
size_t
cb::cluster::instances() const
{
    return instance_pool_->size();
}
size_t
cb::cluster::available_instances() const
{
    return instance_pool_->available();
}

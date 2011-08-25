#include "replication/slave_stream_manager.hpp"

#include "errors.hpp"
#include <boost/bind.hpp>

#include "replication/backfill_sender.hpp"
#include "replication/backfill_out.hpp"
#include "concurrency/wait_any.hpp"

namespace replication {

slave_stream_manager_t::slave_stream_manager_t(boost::scoped_ptr<tcp_conn_t> *conn,
                                               btree_key_value_store_t *kvs,
                                               cond_t *cond,
                                               backfill_receiver_order_source_t *slave_order_source,
                                               int heartbeat_timeout) :
    backfill_receiver_t(&backfill_storer_, slave_order_source),
    stream_(NULL),
    cond_(NULL),
    subs_(boost::bind(&slave_stream_manager_t::on_signal_pulsed, this)),
    kvs_(kvs),
    backfill_storer_(kvs),
    interrupted_by_external_event_(false) {

    // Assume backfilling when starting up.
    // (which is in sync with backill_storer_, which initially takes operations
    // from the backfill queue)
    order_source->backfill_begun();

    stream_ = new repli_stream_t(*conn, this, heartbeat_timeout);

    cond_ = cond;
    if (cond_->is_pulsed()) {
        on_signal_pulsed();
    } else {
        subs_.resubscribe(cond);
    }
}

slave_stream_manager_t::~slave_stream_manager_t() {
    assert_thread();
    shutdown_cond_.wait();
}

void slave_stream_manager_t::backfill(repli_timestamp_t since_when) {

    cond_t c;

    net_backfill_t bf;
    bf.timestamp = since_when;
    if (stream_) stream_->send(&bf);

    /* Stop when the backfill finishes or the connection is closed */
    wait_any_t waiter(&backfill_done_cond_, &shutdown_cond_);
    waiter.wait_lazily_unordered();
}

void slave_stream_manager_t::reverse_side_backfill(repli_timestamp_t since_when) {

    assert_thread();

    debugf("Doing reverse_side_backfill.\n");

    backfill_sender_t sender(&stream_);

    cond_t mc;
    mc.pulse();   // So that backfill_and_realtime_stream() returns as soon as the backfill part is over
    backfill_and_realtime_stream(kvs_, since_when, &sender, &mc);
}

 /* message_callback_t interface */
void slave_stream_manager_t::hello(UNUSED net_hello_t message) {
    debugf("hello message received.\n");
}

void slave_stream_manager_t::send(UNUSED scoped_malloc<net_backfill_t>& message) {
    // TODO: Kill connection instead of crashing server, when master
    // sends garbage.
    rassert(false, "backfill message?  what?\n");
}

void slave_stream_manager_t::conn_closed() {
    assert_thread();

    /* Do this first-thing so that nothing tries to use the closed stream. The
    repli_stream_t destructor may block, which is why we set stream_ to NULL before
    we call the destructor. */
    repli_stream_t *stream_copy = stream_;
    stream_ = NULL;
    delete stream_copy;

    // If the connection closed spontaneously, then notify the cond_t so that
    // the run loop gets unstuck. cond_ could be NULL if we didn't finish our
    // constructor yet.
    if (!interrupted_by_external_event_ && cond_) {
        subs_.unsubscribe();   // So `on_signal_pulsed()` doesn't get called
        cond_->pulse();
    }

    shutdown_cond_.pulse();

    // TODO: This might fail for future versions of the order source, which
    // require a backfill to have begun before it can be done.
    order_source->backfill_done();
}

void slave_stream_manager_t::on_signal_pulsed() {
    assert_thread();

    interrupted_by_external_event_ = true;
    coro_t::spawn_now(boost::bind(&repli_stream_t::shutdown, stream_));   // Will cause conn_closed() to be called
}

}

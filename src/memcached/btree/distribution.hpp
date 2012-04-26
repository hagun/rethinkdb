#ifndef __MEMCACHED_BTREE_DISRIBUTION_HPP__
#define __MEMCACHED_BTREE_DISRIBUTION_HPP__

#include "memcached/queries.hpp"
#include "btree/slice.hpp"

distribution_result_t memcached_distribution_get(btree_slice_t *slice, int max_depth, const store_key_t &left_key, 
        exptime_t effective_time, boost::scoped_ptr<transaction_t>& txn, got_superblock_t& superblock);

#endif

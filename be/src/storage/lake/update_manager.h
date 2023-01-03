// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <unordered_map>

#include "storage/lake/lake_primary_index.h"
#include "storage/lake/rowset_update_state.h"
#include "storage/lake/tablet_metadata.h"
#include "storage/lake/types_fwd.h"
#include "util/dynamic_cache.h"
#include "util/threadpool.h"

namespace starrocks {

namespace lake {

class TxnLogPB_OpWrite;
class LocationProvider;
class Tablet;
class MetaFileBuilder;

class UpdateManager {
public:
    UpdateManager(LocationProvider* location_provider)
            : _index_cache(std::numeric_limits<size_t>::max()),
              _update_state_cache(std::numeric_limits<size_t>::max()),
              _location_provider(location_provider) {}
    ~UpdateManager() {}
    void set_cache_expire_ms(int64_t expire_ms) { _cache_expire_ms = expire_ms; }

    int64_t get_cache_expire_ms() const { return _cache_expire_ms; }

    // publish primary key tablet, update primary index and delvec, then update meta file
    Status publish_primary_key_tablet(const TxnLogPB_OpWrite& op_write, TabletMetadata* metadata, Tablet* tablet,
                                      MetaFileBuilder* builder, int64_t base_version);

    // get rowids from primary index by each upserts
    Status get_rowids_from_pkindex(Tablet* tablet, TabletMetadata* metadata,
                                   const std::vector<ColumnUniquePtr>& upserts, int64_t base_version,
                                   std::vector<std::vector<uint64_t>*>* rss_rowids);

    // get column data by rssid and rowids
    Status get_column_values(Tablet* tablet, TabletMetadata* metadata, TabletSchema* tablet_schema,
                             std::vector<uint32_t>& column_ids, bool with_default,
                             std::map<uint32_t, std::vector<uint32_t>>& rowids_by_rssid,
                             vector<std::unique_ptr<Column>>* columns);
    // get delvec by version
    Status get_del_vec(const TabletSegmentId& tsid, int64_t version, DelVectorPtr* pdelvec);

    // get latest delvec
    Status get_latest_del_vec(const TabletSegmentId& tsid, int64_t base_version, MetaFileBuilder* builder,
                              DelVectorPtr* pdelvec);

    // get delvec from tablet meta file
    Status get_del_vec_in_meta(const TabletSegmentId& tsid, int64_t meta_ver, DelVector* delvec,
                               int64_t* latest_version);
    // set delvec cache
    Status set_cached_del_vec(const std::vector<std::pair<TabletSegmentId, DelVectorPtr>>& cache_delvec_updates,
                              int64_t version);

    void expire_cache();
    void clear_cached_del_vec(const std::vector<TabletSegmentId>& tsids);

private:
    Status _do_update(std::uint32_t rowset_id, std::int32_t upsert_idx, const std::vector<ColumnUniquePtr>& upserts,
                      PrimaryIndex& index, std::int64_t tablet_id, DeletesMap* new_deletes);

private:
    // default 6min
    int64_t _cache_expire_ms = 360000;
    // primary index
    DynamicCache<uint64_t, LakePrimaryIndex> _index_cache;

    // rowset cache
    DynamicCache<string, RowsetUpdateState> _update_state_cache;

    // DelVector related states
    std::mutex _del_vec_cache_lock;
    std::unordered_map<TabletSegmentId, DelVectorPtr> _del_vec_cache;
    // use _del_vec_cache_ver to indice the valid position
    int64_t _del_vec_cache_ver{0};

    std::atomic<int64_t> _last_clear_expired_cache_millis{0};

    LocationProvider* _location_provider;
};

} // namespace lake

} // namespace starrocks
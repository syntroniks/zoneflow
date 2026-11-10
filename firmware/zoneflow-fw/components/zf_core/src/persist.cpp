// SPDX-License-Identifier: Apache-2.0
#include "zf/app/persist.hpp"

#include "zf/config/config_json.hpp"

namespace zf {

bool persistWriteOnce(PersistMailbox &box, IBlobStorage &storage)
{
    SchedulerPersist state;
    if (!box.take(state)) {
        return false;
    }
    const std::string json = persistToJson(state);
    box.reportWrite(!json.empty() && storage.write(ConfigStore::kRuntimeKey, json));
    return true;
}

} // namespace zf

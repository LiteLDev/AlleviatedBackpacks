#pragma once

// SC RecentCraftedResultStorage: per player, per recipe scope and ingredient item, the last 4
// distinct results taken (most recent first). The stonecutter list shows them first, on a
// highlighted background. Stored in the world's LevelDB (key abp:player:<uuid>:recent_results).

#include <string>
#include <string_view>
#include <vector>

class Player;

namespace abp::upgrade::stonecutter {

/// SC MAX_RECENT_RESULTS_PER_INGREDIENT.
inline constexpr int kMaxRecentResults = 4;

/// Recent result item ids for `ingredient` (item id), most recent first.
[[nodiscard]] std::vector<std::string> recentResults(Player& player, std::string_view scope, std::string_view ingredient);
/// Records that `result` was taken for `ingredient`. Returns false if it already was the latest.
bool recordResult(Player& player, std::string_view scope, std::string_view ingredient, std::string_view result);

} // namespace abp::upgrade::stonecutter

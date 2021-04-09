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

#include <iostream>
#include <random>
#include <string>

#include <couchbase/client/cluster.hxx>
#include <couchbase/transactions.hxx>

using namespace std;
using namespace couchbase;

std::string
make_uuid()
{
    static std::random_device dev;
    static std::mt19937 rng(dev());

    uniform_int_distribution<int> dist(0, 15);

    const char* v = "0123456789abcdef";
    const bool dash[] = { 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0 };

    string res;
    for (int i = 0; i < 16; i++) {
        if (dash[i])
            res += "-";
        res += v[dist(rng)];
        res += v[dist(rng)];
    }
    return res;
}

struct Player {
    int experience;
    int hitpoints;
    std::string json_type;
    int level;
    bool logged_in;
    std::string name;
    std::string uuid;
};

void
to_json(nlohmann::json& j, const Player& p)
{
    /* clang-format off */
    j = nlohmann::json{ { "experience", p.experience },
                        { "hitpoints", p.hitpoints },
                        { "jsonType", p.json_type },
                        { "level", p.level },
                        { "loggedIn", p.logged_in },
                        { "name", p.name },
                        { "uuid", p.uuid } };
    /* clang-format on */
}

void
from_json(const nlohmann::json& j, Player& p)
{
    j.at("experience").get_to(p.experience);
    j.at("hitpoints").get_to(p.hitpoints);
    j.at("jsonType").get_to(p.json_type);
    j.at("level").get_to(p.level);
    j.at("loggedIn").get_to(p.logged_in);
    j.at("name").get_to(p.name);
    j.at("uuid").get_to(p.uuid);
}

struct Monster {
    int experience_when_killed;
    int hitpoints;
    double item_probability;
    std::string json_type;
    std::string name;
    std::string uuid;
};

void
to_json(nlohmann::json& j, const Monster& m)
{
    j = nlohmann::json{ { "experienceWhenKilled", m.experience_when_killed },
                        { "hitpoints", m.hitpoints },
                        { "itemProbability", m.item_probability },
                        { "jsonType", m.json_type },
                        { "name", m.name },
                        { "uuid", m.uuid } };
}

void
from_json(const nlohmann::json& j, Monster& m)
{
    j.at("experienceWhenKilled").get_to(m.experience_when_killed);
    j.at("hitpoints").get_to(m.hitpoints);
    j.at("itemProbability").get_to(m.item_probability);
    j.at("jsonType").get_to(m.json_type);
    j.at("name").get_to(m.name);
    j.at("uuid").get_to(m.uuid);
}
class GameServer
{
  private:
    transactions::transactions& transactions_;
    std::shared_ptr<collection> collection_;

  public:
    GameServer(transactions::transactions& transactions, std::shared_ptr<collection> collection)
      : transactions_(transactions)
      , collection_(collection)
    {
    }

    CB_NODISCARD int calculate_level_for_experience(int experience) const
    {
        return experience / 100;
    }

    void player_hits_monster(const string& action_id_, int damage_, const string& player_id, const string& monster_id)
    {
        transactions_.run([&](transactions::attempt_context& ctx) {
            auto monster = ctx.get(collection_, monster_id);
            const Monster& monster_body = monster.content<Monster>();

            int monster_hitpoints = monster_body.hitpoints;
            int monster_new_hitpoints = monster_hitpoints - damage_;

            cout << "Monster " << monster_id << " had " << monster_hitpoints << " hitpoints, took " << damage_ << " damage, now has "
                 << monster_new_hitpoints << " hitpoints" << endl;

            auto player = ctx.get(collection_, player_id);

            if (monster_new_hitpoints <= 0) {
                // Monster is killed. The remove is just for demoing, and a more realistic examples would set a "dead" flag or similar.
                ctx.remove(collection_, monster);

                const Player& player_body = player.content<Player>();

                // the player earns experience for killing the monster
                int experience_for_killing_monster = monster_body.experience_when_killed;
                int player_experience = player_body.experience;
                int player_new_experience = player_experience + experience_for_killing_monster;
                int player_new_level = calculate_level_for_experience(player_new_experience);

                cout << "Monster " << monster_id << " was killed. Player " << player_id << " gains " << experience_for_killing_monster
                     << " experience, now has level " << player_new_level << endl;

                Player player_new_body = player_body;
                player_new_body.experience = player_new_experience;
                player_new_body.level = player_new_level;
                ctx.replace(collection_, player, player_new_body);
            } else {
                cout << "Monster " << monster_id << " is damaged but alive" << endl;

                Monster monster_new_body = monster_body;
                monster_new_body.hitpoints = monster_new_hitpoints;
                ctx.replace(collection_, monster, monster_new_body);
            }
            cout << "About to commit transaction" << endl;
        });
    }
};

int
main(int argc, const char* argv[])
{
    string cluster_address = "couchbase://127.0.0.1";
    string user_name = "Administrator";
    string password = "password";
    string bucket_name = "default";

    cluster cluster(cluster_address, user_name, password);

    auto bucket = cluster.bucket(bucket_name);
    auto collection = bucket->default_collection();

    string player_id = "player_data";
    Player player_data{ 14248, 23832, "player", 141, true, "Jane", make_uuid() };

    string monster_id = "a_grue";
    Monster monster_data{ 91, 400000, 0.19239324085462631, "monster", "Grue", make_uuid() };

    collection->upsert(player_id, player_data);
    cout << "Upserted sample player document: " << player_id << endl;

    collection->upsert(monster_id, monster_data);
    cout << "Upserted sample monster document: " << monster_id << endl;

    transactions::transaction_config configuration;
    configuration.durability_level(transactions::durability_level::MAJORITY);
    transactions::transactions transactions(cluster, configuration);
    GameServer game_server(transactions, collection);
    bool monster_exists = true;
    while (monster_exists) {
        cout << "Monster exists -- lets hit it!" << endl;
        game_server.player_hits_monster(make_uuid(), rand() % 80, player_id, monster_id);
        auto result = collection->get(monster_id);
        monster_exists = result.is_success();
    }
    cout << "Monster killed" << endl;

    transactions.close();

}

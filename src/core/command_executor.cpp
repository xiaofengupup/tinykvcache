#include "tinykv/core/command_executor.h"

namespace tinykv {

std::string CommandExecutor::Execute(KVStore &store, const Command &command)
{
    switch (command.type) {
        case CommandType::Ping:
            return "+PONG";
        case CommandType::Set:
            store.Set(command.key, command.value);
            return "+OK";
        case CommandType::Get: {
            std::string value;
            if (store.Get(command.key, value)) {
                return "$" + value;
            } else {
                return "$nil";
            }
        }
        case CommandType::Del: {
            if (store.Del(command.key)) {
                return "+OK";
            } else {
                return "$nil";
            }
        }
        case CommandType::Expire: {
            if (store.Expire(command.key, command.seconds)) {
                return "+OK";
            } else {
                return "$nil";
            }
        }
        case CommandType::Ttl: {
            int ttl = store.Ttl(command.key);
            return "$" + std::to_string(ttl);
        }
        case CommandType::Stats: {
            KVStore::InnerStats stats = store.Stats();
            return "+keys=" + std::to_string(stats.keys) +
               ",persistent=" + std::to_string(stats.persistentKeys) +
               ",expiring=" + std::to_string(stats.expiringKeys);
        }
        case CommandType::Quit:
            return "+BYE";
        default:
            return "-ERR unknown command";
    }
}

} // namespace tinykv
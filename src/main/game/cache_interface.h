#ifndef SOLVITAIRE_CACHE_INTERFACE_H
#define SOLVITAIRE_CACHE_INTERFACE_H

#include <cstdint>

class game_state;

class cache_interface {
public:
    virtual ~cache_interface() = default;

    // Returns true if the state was newly inserted (not already present)
    virtual bool insert(const game_state& gs) = 0;

    // Returns true if the state is in the cache
    virtual bool contains(const game_state& gs) const = 0;

    virtual void clear() = 0;
    virtual uint64_t size() const = 0;
    virtual uint64_t get_states_removed_from_cache() const = 0;
    virtual uint64_t bucket_count() const = 0;
};

#endif

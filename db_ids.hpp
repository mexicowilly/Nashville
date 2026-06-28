#pragma once

#include <cstdint>

namespace nashville
{

// Strong typedefs for database row identity.  `enum class` with a fixed
// underlying type gives us a zero-overhead distinct type: a song_id can't
// be passed where a playlist_id is wanted, and neither implicitly converts
// to a raw integer.  Construct from a rowid with song_id{ n } and recover
// the rowid with static_cast<std::int64_t>(id).
//
// These live in their own header (rather than database.hpp) so that callers
// which only need to *hold* an id — song_tab carries an optional<song_id>
// beside its undo/redo stacks — don't have to include database.hpp and pull
// in sqlite3.h with it.
enum class song_id     : std::int64_t {};
enum class playlist_id : std::int64_t {};

// A value paired with the identity of the row it came from.  Returned by
// reads that need to hand identity back to the caller (a tab loads a song
// and keeps the id for the session, so every later save is keyed by id and
// never by the — now mutable — name).
template <typename Id, typename T>
struct stored
{
    Id id;
    T  value;
};

}

#pragma once
#include "tile/tile_coord.h"
#include "tile/tile_data.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_set>
#include <atomic>

namespace mesh3d {

class TileProvider;

/* Background I/O thread for tile fetching.
   Main thread enqueues requests and drains results each frame.
   The worker thread calls TileProvider::fetch_tile() which may do
   network I/O, disk reads, and decompression. */
class AsyncLoader {
public:
    AsyncLoader() = default;
    ~AsyncLoader();

    /* Launch worker thread */
    void start();
    /* Signal worker to stop and join */
    void stop();

    /* Enqueue a tile fetch request (thread-safe, non-blocking) */
    void request(const TileCoord& coord, TileProvider* provider);

    /* Dequeue one completed result. Returns true if a result was available. */
    bool poll_result(TileData& out);

    /* Check if a tile is already queued or in-flight */
    bool is_pending(const TileCoord& coord) const;

    /* Remove all pending requests (e.g. when a provider is about to be destroyed) */
    void clear_pending();

    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    struct Request {
        TileCoord coord;
        TileProvider* provider;
    };

    mutable std::mutex m_req_mutex;
    std::condition_variable m_req_cv;
    std::deque<Request> m_requests;
    std::unordered_set<TileCoord> m_pending_set;

    std::mutex m_result_mutex;
    std::deque<TileData> m_results;

    void worker_loop();
};

} // namespace mesh3d

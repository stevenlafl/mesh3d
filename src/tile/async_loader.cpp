#include "tile/async_loader.h"
#include "tile/tile_provider.h"
#include "util/log.h"

namespace mesh3d {

AsyncLoader::~AsyncLoader() {
    stop();
}

void AsyncLoader::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread(&AsyncLoader::worker_loop, this);
    LOG_INFO("AsyncLoader: worker thread started");
}

void AsyncLoader::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_req_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    LOG_INFO("AsyncLoader: worker thread stopped");
}

void AsyncLoader::request(const TileCoord& coord, TileProvider* provider) {
    std::lock_guard<std::mutex> lock(m_req_mutex);
    if (m_pending_set.count(coord)) return; // already queued or in-flight
    m_pending_set.insert(coord);
    m_requests.push_back({coord, provider});
    m_req_cv.notify_one();
}

bool AsyncLoader::poll_result(TileData& out) {
    {
        std::lock_guard<std::mutex> lock(m_result_mutex);
        if (m_results.empty()) return false;
        out = std::move(m_results.front());
        m_results.pop_front();
    }
    /* Remove from pending set only after the result is drained.
       This prevents the main thread from re-enqueuing a tile that
       is completed but not yet uploaded to the GPU cache. */
    {
        std::lock_guard<std::mutex> lock(m_req_mutex);
        m_pending_set.erase(out.coord);
    }
    return true;
}

bool AsyncLoader::is_pending(const TileCoord& coord) const {
    std::lock_guard<std::mutex> lock(m_req_mutex);
    return m_pending_set.count(coord) > 0;
}

void AsyncLoader::clear_pending() {
    std::lock_guard<std::mutex> lock(m_req_mutex);
    m_requests.clear();
    m_pending_set.clear();
}

void AsyncLoader::worker_loop() {
    while (m_running.load()) {
        Request req;

        {
            std::unique_lock<std::mutex> lock(m_req_mutex);
            m_req_cv.wait(lock, [this] {
                return !m_requests.empty() || !m_running.load();
            });
            if (!m_running.load() && m_requests.empty()) break;
            if (m_requests.empty()) continue;
            req = std::move(m_requests.front());
            m_requests.pop_front();
        }

        /* Safety: skip if provider was nulled out (e.g. source changed) */
        if (!req.provider) {
            std::lock_guard<std::mutex> lock(m_req_mutex);
            m_pending_set.erase(req.coord);
            continue;
        }

        /* Fetch tile (may block on network/disk I/O).
           The provider checks its disk cache before downloading. */
        std::optional<TileData> result;
        try {
            result = req.provider->fetch_tile(req.coord);
        } catch (const std::exception& e) {
            LOG_ERROR("AsyncLoader: fetch_tile failed for z=%d x=%d y=%d: %s",
                      req.coord.z, req.coord.x, req.coord.y, e.what());
        } catch (...) {
            LOG_ERROR("AsyncLoader: fetch_tile failed for z=%d x=%d y=%d (unknown error)",
                      req.coord.z, req.coord.x, req.coord.y);
        }

        if (result) {
            /* Successful: queue result. Pending removal deferred to poll_result(). */
            std::lock_guard<std::mutex> lock(m_result_mutex);
            m_results.push_back(std::move(*result));
        } else {
            /* Failed: remove from pending so it can be retried later */
            std::lock_guard<std::mutex> lock(m_req_mutex);
            m_pending_set.erase(req.coord);
        }
    }
}

} // namespace mesh3d

#pragma once
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <memory>

namespace mesh3d {

class Database {
public:
    Database() = default;
    ~Database();

    bool connect(const std::string& conninfo);
    void disconnect();
    bool connected() const { return m_conn != nullptr; }

    /* Execute parameterized query. Returns null on error. */
    struct Result {
        PGresult* res = nullptr;
        ~Result() { if (res) PQclear(res); }
        bool ok() const;
        int rows() const;
        int cols() const;
        const char* get(int row, int col) const;
        /* Get binary field (BYTEA) */
        const char* get_binary(int row, int col, int* len) const;
    };

    std::unique_ptr<Result> exec(const char* sql);
    std::unique_ptr<Result> exec_params(const char* sql,
                                         const std::vector<std::string>& params);

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

private:
    PGconn* m_conn = nullptr;
};

} // namespace mesh3d

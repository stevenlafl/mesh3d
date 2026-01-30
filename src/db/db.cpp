#include "db/db.h"
#include "util/log.h"

namespace mesh3d {

Database::~Database() { disconnect(); }

bool Database::connect(const std::string& conninfo) {
    disconnect();
    m_conn = PQconnectdb(conninfo.c_str());
    if (PQstatus(m_conn) != CONNECTION_OK) {
        LOG_ERROR("DB connection failed: %s", PQerrorMessage(m_conn));
        PQfinish(m_conn);
        m_conn = nullptr;
        return false;
    }
    LOG_INFO("Connected to database");
    return true;
}

void Database::disconnect() {
    if (m_conn) {
        PQfinish(m_conn);
        m_conn = nullptr;
    }
}

bool Database::Result::ok() const {
    if (!res) return false;
    ExecStatusType s = PQresultStatus(res);
    return s == PGRES_TUPLES_OK || s == PGRES_COMMAND_OK;
}

int Database::Result::rows() const { return res ? PQntuples(res) : 0; }
int Database::Result::cols() const { return res ? PQnfields(res) : 0; }

const char* Database::Result::get(int row, int col) const {
    if (!res) return nullptr;
    if (PQgetisnull(res, row, col)) return nullptr;
    return PQgetvalue(res, row, col);
}

const char* Database::Result::get_binary(int row, int col, int* len) const {
    if (!res) return nullptr;
    if (PQgetisnull(res, row, col)) return nullptr;
    *len = PQgetlength(res, row, col);
    return PQgetvalue(res, row, col);
}

std::unique_ptr<Database::Result> Database::exec(const char* sql) {
    auto r = std::make_unique<Result>();
    r->res = PQexec(m_conn, sql);
    if (!r->ok()) {
        LOG_ERROR("Query failed: %s\n%s", PQerrorMessage(m_conn), sql);
    }
    return r;
}

std::unique_ptr<Database::Result> Database::exec_params(
    const char* sql, const std::vector<std::string>& params)
{
    std::vector<const char*> values;
    for (auto& p : params) values.push_back(p.c_str());

    auto r = std::make_unique<Result>();
    r->res = PQexecParams(m_conn, sql,
                          static_cast<int>(params.size()),
                          nullptr, values.data(), nullptr, nullptr, 0);
    if (!r->ok()) {
        LOG_ERROR("Parameterized query failed: %s\n%s", PQerrorMessage(m_conn), sql);
    }
    return r;
}

} // namespace mesh3d

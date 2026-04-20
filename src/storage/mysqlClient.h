#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class MySqlClient
{
public:
    using Row = std::unordered_map<std::string, std::string>;

    MySqlClient();
    ~MySqlClient();

    bool connect(const std::string& host,
                 int port,
                 const std::string& user,
                 const std::string& password,
                 const std::string& database,
                 int connectTimeoutMs);

    bool connected() const;
    void disconnect();

    bool begin();
    bool commit();
    bool rollback();

    bool execute(const std::string& sql);
    bool query(const std::string& sql, std::vector<Row>& rows);

    std::string escape(const std::string& value);
    std::string lastError() const;

private:
    void setLastError(const std::string& error);

private:
    void* m_impl;
    std::string m_lastError;
};

#include "storage/mysqlClient.h"

#include <sstream>

#ifdef HAVE_MYSQL_CLIENT
#include <mysql/mysql.h>
#endif

namespace
{

#ifdef HAVE_MYSQL_CLIENT
MYSQL* AsMysqlHandle(void* handle)
{
    return static_cast<MYSQL*>(handle);
}
#endif

} // namespace

MySqlClient::MySqlClient()
    : m_impl(nullptr)
{
}

MySqlClient::~MySqlClient()
{
    disconnect();
}

bool MySqlClient::connect(const std::string& host,
                          int port,
                          const std::string& user,
                          const std::string& password,
                          const std::string& database,
                          int connectTimeoutMs)
{
#ifdef HAVE_MYSQL_CLIENT
    disconnect();

    MYSQL* handle = mysql_init(nullptr);
    if (!handle)
    {
        setLastError("mysql_init failed");
        return false;
    }

    const unsigned int timeout = connectTimeoutMs > 0 ? static_cast<unsigned int>(connectTimeoutMs / 1000) : 2U;
    mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (!mysql_real_connect(handle,
                            host.c_str(),
                            user.c_str(),
                            password.c_str(),
                            database.c_str(),
                            static_cast<unsigned int>(port),
                            nullptr,
                            0))
    {
        setLastError(mysql_error(handle));
        mysql_close(handle);
        return false;
    }

    m_impl = handle;
    m_lastError.clear();
    return true;
#else
    (void)host;
    (void)port;
    (void)user;
    (void)password;
    (void)database;
    (void)connectTimeoutMs;
    setLastError("MySQL client support is not compiled in");
    return false;
#endif
}

bool MySqlClient::connected() const
{
#ifdef HAVE_MYSQL_CLIENT
    return m_impl != nullptr;
#else
    return false;
#endif
}

void MySqlClient::disconnect()
{
#ifdef HAVE_MYSQL_CLIENT
    if (m_impl)
    {
        mysql_close(AsMysqlHandle(m_impl));
        m_impl = nullptr;
    }
#else
    m_impl = nullptr;
#endif
}

bool MySqlClient::begin()
{
    return execute("BEGIN");
}

bool MySqlClient::commit()
{
    return execute("COMMIT");
}

bool MySqlClient::rollback()
{
    return execute("ROLLBACK");
}

bool MySqlClient::execute(const std::string& sql)
{
#ifdef HAVE_MYSQL_CLIENT
    if (!connected())
    {
        setLastError("MySQL connection is not ready");
        return false;
    }

    if (mysql_query(AsMysqlHandle(m_impl), sql.c_str()) != 0)
    {
        setLastError(mysql_error(AsMysqlHandle(m_impl)));
        return false;
    }

    m_lastError.clear();
    return true;
#else
    (void)sql;
    setLastError("MySQL client support is not compiled in");
    return false;
#endif
}

bool MySqlClient::query(const std::string& sql, std::vector<Row>& rows)
{
    rows.clear();

#ifdef HAVE_MYSQL_CLIENT
    if (!connected())
    {
        setLastError("MySQL connection is not ready");
        return false;
    }

    if (mysql_query(AsMysqlHandle(m_impl), sql.c_str()) != 0)
    {
        setLastError(mysql_error(AsMysqlHandle(m_impl)));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(AsMysqlHandle(m_impl));
    if (!result)
    {
        if (mysql_field_count(AsMysqlHandle(m_impl)) == 0)
        {
            return true;
        }
        setLastError(mysql_error(AsMysqlHandle(m_impl)));
        return false;
    }

    const int fieldCount = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr)
    {
        unsigned long* lengths = mysql_fetch_lengths(result);
        Row item;
        for (int i = 0; i < fieldCount; ++i)
        {
            const std::string key = fields[i].name ? fields[i].name : "";
            const std::string value = row[i] ? std::string(row[i], lengths[i]) : "";
            item[key] = value;
        }
        rows.push_back(std::move(item));
    }

    mysql_free_result(result);
    m_lastError.clear();
    return true;
#else
    (void)sql;
    setLastError("MySQL client support is not compiled in");
    return false;
#endif
}

std::string MySqlClient::escape(const std::string& value)
{
#ifdef HAVE_MYSQL_CLIENT
    if (!connected())
    {
        setLastError("MySQL connection is not ready");
        return value;
    }

    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long length = mysql_real_escape_string(AsMysqlHandle(m_impl),
                                                          escaped.data(),
                                                          value.data(),
                                                          static_cast<unsigned long>(value.size()));
    escaped.resize(length);
    return escaped;
#else
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        if (ch == '\'')
        {
            escaped += "\\'";
        }
        else if (ch == '\\')
        {
            escaped += "\\\\";
        }
        else
        {
            escaped.push_back(ch);
        }
    }
    return escaped;
#endif
}

std::string MySqlClient::lastError() const
{
    return m_lastError;
}

void MySqlClient::setLastError(const std::string& error)
{
    m_lastError = error;
}
